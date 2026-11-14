// LD_PRELOAD shim: run upstream-FastRPC userspace (hexagonrpcd, which targets the mainline
// <misc/fastrpc.h> ABI on /dev/fastrpc-*) on the downstream msm "adsprpc" driver of the a70q 4.14
// kernel (/dev/adsprpc-smd). ioctls on file descriptors opened from /dev/adsprpc-* are translated;
// everything else is passed through.
//
//   upstream                        downstream (drivers/char/adsprpc_shared.h)
//   INVOKE      _IOWR('R',3)   ->   INVOKE_ATTRS _IOWR('R',7)   (remote_arg + fds + attrs)
//   INIT_ATTACH _IO('R',4)     ->   INIT _IOWR('R',6) flags FASTRPC_INIT_ATTACH (0)
//   ATTACH_SNS  _IO('R',8)     ->   INIT _IOWR('R',6) flags FASTRPC_INIT_ATTACH_SENSORS (3)
//   MMAP        _IOWR('R',6)   ->   MMAP _IOWR('R',2)           (same layout on arm64)
//   MUNMAP      _IOWR('R',7)   ->   MUNMAP _IOWR('R',3)         (same layout on arm64)
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

struct up_invoke_args { uint64_t ptr; uint64_t length; int32_t fd; uint32_t attr; };
struct up_invoke { uint32_t handle; uint32_t sc; uint64_t args; };
struct up_req_mmap { int32_t fd; uint32_t flags; uint64_t vaddrin; uint64_t size; uint64_t vaddrout; };
struct up_req_munmap { uint64_t vaddrout; uint64_t size; };
#define UP_INVOKE          _IOWR('R', 3, struct up_invoke)
#define UP_INIT_ATTACH     _IO('R', 4)
#define UP_MMAP            _IOWR('R', 6, struct up_req_mmap)
#define UP_MUNMAP          _IOWR('R', 7, struct up_req_munmap)
#define UP_INIT_ATTACH_SNS _IO('R', 8)

struct dn_buf { void *pv; size_t len; };
union dn_arg { struct dn_buf buf; struct { int fd; uint32_t offset; } dma; uint32_t h; };
struct dn_invoke { uint32_t handle; uint32_t sc; union dn_arg *pra; };
struct dn_invoke_attrs { struct dn_invoke inv; int *fds; unsigned int *attrs; };
struct dn_init { uint32_t flags; uintptr_t file; uint32_t filelen; int32_t filefd;
		 uintptr_t mem; uint32_t memlen; int32_t memfd; };
struct dn_mmap { int fd; uint32_t flags; uintptr_t vaddrin; size_t size; uintptr_t vaddrout; };
struct dn_munmap { uintptr_t vaddrout; size_t size; };
#define DN_MMAP         _IOWR('R', 2, struct dn_mmap)
#define DN_MUNMAP       _IOWR('R', 3, struct dn_munmap)
#define DN_INIT         _IOWR('R', 6, struct dn_init)
#define DN_INVOKE_ATTRS _IOWR('R', 7, struct dn_invoke_attrs)
#define DN_GETINFO      _IOWR('R', 8, uint32_t)

#define MAXFD 1024
static unsigned char is_adsprpc[MAXFD];
static int verbose = -1;

static int (*real_ioctl)(int, int, ...);
static int (*real_open)(const char *, int, ...);
static int (*real_openat)(int, const char *, int, ...);

static void init(void) {
	if (!real_ioctl) {
		real_ioctl = dlsym(RTLD_NEXT, "ioctl");
		real_open = dlsym(RTLD_NEXT, "open");
		real_openat = dlsym(RTLD_NEXT, "openat");
		verbose = getenv("FASTRPC_SHIM_DEBUG") != NULL;
	}
}

static void track(int fd, const char *path) {
	if (fd >= 0 && fd < MAXFD && path && strstr(path, "adsprpc")) {
		is_adsprpc[fd] = 1;
		// the downstream driver binds a file to a DSP and allocates its session only on
		// FASTRPC_IOCTL_GETINFO(domain) (libadsprpc does this right after open); the upstream
		// driver picks the DSP from the device node. Domain: 0 ADSP, 1 MDSP, 2 SDSP, 3 CDSP.
		const char *d = getenv("FASTRPC_SHIM_DOMAIN");
		uint32_t info = d ? (uint32_t)atoi(d) : 0;
		int r = real_ioctl(fd, DN_GETINFO, &info);
		if (verbose) fprintf(stderr, "shim: fd %d = %s, GETINFO(domain) -> %d (%s) info 0x%x\n",
				     fd, path, r, r < 0 ? strerror(errno) : "ok", info);
	}
}

int open(const char *path, int flags, ...) {
	init();
	mode_t mode = 0;
	if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap); }
	int fd = real_open(path, flags, mode);
	track(fd, path);
	return fd;
}

int openat(int dirfd, const char *path, int flags, ...) {
	init();
	mode_t mode = 0;
	if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap); }
	int fd = real_openat(dirfd, path, flags, mode);
	track(fd, path);
	return fd;
}

// REMOTE_SCALARS: inbufs<<16 | outbufs<<8 | inhandles<<4 | outhandles
static int nargs(uint32_t sc) {
	return ((sc >> 16) & 0xff) + ((sc >> 8) & 0xff) + ((sc >> 4) & 0xf) + (sc & 0xf);
}

static int do_invoke(int fd, struct up_invoke *u) {
	int n = nargs(u->sc);
	struct up_invoke_args *ua = (void *)(uintptr_t)u->args;
	union dn_arg *pra = n ? calloc(n, sizeof(*pra)) : NULL;
	int *fds = n ? malloc(n * sizeof(int)) : NULL;
	unsigned int *attrs = n ? calloc(n, sizeof(unsigned int)) : NULL;
	int bufs = ((u->sc >> 16) & 0xff) + ((u->sc >> 8) & 0xff);
	for (int i = 0; i < n; i++) {
		if (i < bufs) {
			pra[i].buf.pv = (void *)(uintptr_t)ua[i].ptr;
			pra[i].buf.len = ua[i].length;
		} else {
			pra[i].h = (uint32_t)ua[i].fd;      // remote handles
		}
		fds[i] = ua[i].fd;
		attrs[i] = ua[i].attr;
	}
	struct dn_invoke_attrs d = { { u->handle, u->sc, pra }, fds, attrs };
	int r = real_ioctl(fd, DN_INVOKE_ATTRS, &d);
	if (verbose) fprintf(stderr, "shim: invoke handle %u sc 0x%08x -> %d (%s)\n",
			     u->handle, u->sc, r, r < 0 ? strerror(errno) : "ok");
	free(pra); free(fds); free(attrs);
	return r;
}

int ioctl(int fd, int req, ...) {
	init();
	va_list ap;
	va_start(ap, req);
	void *arg = va_arg(ap, void *);
	va_end(ap);
	if (fd < 0 || fd >= MAXFD || !is_adsprpc[fd])
		return real_ioctl(fd, req, arg);

	unsigned int r = (unsigned int)req;
	if (r == (unsigned int)UP_INVOKE)
		return do_invoke(fd, arg);
	if (r == (unsigned int)UP_INIT_ATTACH || r == (unsigned int)UP_INIT_ATTACH_SNS) {
		struct dn_init in = { .flags = r == (unsigned int)UP_INIT_ATTACH ? 0 : 3, .filefd = -1, .memfd = -1 };
		int ret = real_ioctl(fd, DN_INIT, &in);
		if (verbose) fprintf(stderr, "shim: init flags %u -> %d (%s)\n", in.flags, ret, ret < 0 ? strerror(errno) : "ok");
		return ret;
	}
	if (r == (unsigned int)UP_MMAP) {
		struct up_req_mmap *u = arg;
		struct dn_mmap d = { u->fd, u->flags, u->vaddrin, u->size, 0 };
		int ret = real_ioctl(fd, DN_MMAP, &d);
		u->vaddrout = d.vaddrout;
		if (verbose) fprintf(stderr, "shim: mmap flags 0x%x size %llu -> %d vaddrout 0x%llx\n",
				     u->flags, (unsigned long long)u->size, ret, (unsigned long long)u->vaddrout);
		return ret;
	}
	if (r == (unsigned int)UP_MUNMAP) {
		struct up_req_munmap *u = arg;
		struct dn_munmap d = { u->vaddrout, u->size };
		return real_ioctl(fd, DN_MUNMAP, &d);
	}
	if (verbose) fprintf(stderr, "shim: unsupported upstream ioctl 0x%x\n", r);
	errno = ENOTTY;
	return -1;
}
