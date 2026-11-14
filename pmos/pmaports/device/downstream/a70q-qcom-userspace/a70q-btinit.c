// a70q-btinit: bring up the WCN3988 ("Apache") Bluetooth core of the Galaxy A70 on the
// downstream 4.14 kernel and hand it to the kernel's HCI UART (H4) line discipline.
//
// The 4.14 hci_qca driver only knows ROME, and the Android device tree has no serdev node, so
// the firmware download that upstream btqca/hci_qca do for WCN399x is done here in userspace:
//   power on (/dev/btpower) -> vote the GENI UART clock (TIOCPMGET) -> read version @115200
//   -> switch to 3 Mbaud (vendor cmd 0xFC48) -> download rampatch + NVM (EDL cmd 0xFC00/0x1E,
//   243-byte segments; NVM patched: baud rate, deep sleep off because H4 has no IBS)
//   -> HCI_Reset -> TIOCSETD N_HCI + HCIUARTSETPROTO H4 -> stay alive holding the tty and the
//   clock vote (closing the tty drops the vote and stops the UART).
//
//   a70q-btinit [-t /dev/ttyHS0] [-p rampatch.tlv] [-n nvm.bin]
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#define BT_CMD_PWR_CTRL 0xbfad
#define TIOCPMGET_ 0x544D
#define TIOCPMPUT_ 0x544E
#define N_HCI_ 15
#define HCIUARTSETPROTO_ _IOW('U', 200, int)
#define HCI_UART_H4 0

#define EDL_OPCODE 0xFC00
#define EDL_PATCH_VER_REQ_CMD 0x19
#define EDL_PATCH_TLV_REQ_CMD 0x1E
#define EDL_TVL_DNLD_RES_EVT 0x04
#define SEG 243
#define QCA_BAUD_OPCODE 0xFC48
#define QCA_BAUDRATE_3000000 14

// operating speed (-b): QCA baud index + termios speed
static int op_idx = 13;
static speed_t op_speed = B2000000;

static int tty = -1, pwr = -1;
static volatile sig_atomic_t stop;

static void on_sig(int s) { (void)s; stop = 1; }

static int set_speed(int fd, speed_t sp, int flow) {
	struct termios t;
	if (tcgetattr(fd, &t)) return -1;
	cfmakeraw(&t);
	cfsetspeed(&t, sp);
	t.c_cflag |= CLOCAL | CREAD;
	t.c_cflag &= ~CSTOPB;
	if (flow) t.c_cflag |= CRTSCTS; else t.c_cflag &= ~CRTSCTS;
	return tcsetattr(fd, TCSANOW, &t);
}

static int rdx(int fd, uint8_t *b, int n, int ms) {
	int got = 0;
	while (got < n) {
		struct pollfd p = { fd, POLLIN, 0 };
		if (poll(&p, 1, ms) <= 0) return got;
		int r = read(fd, b + got, n - got);
		if (r <= 0) return got;
		got += r;
	}
	return got;
}

// read one HCI event packet; returns event code, fills params/len; -1 on timeout
static int read_event(uint8_t *par, int *plen, int ms) {
	uint8_t h[3];
	for (;;) {
		if (rdx(tty, h, 1, ms) != 1) return -1;
		if (h[0] == 0x04) break;           // skip anything that is not an event packet
	}
	if (rdx(tty, h + 1, 2, 500) != 2) return -1;
	*plen = h[2];
	if (rdx(tty, par, h[2], 500) != h[2]) return -1;
	return h[1];
}

static int send_cmd(uint16_t op, const uint8_t *p, int n) {
	uint8_t b[4 + 260];
	b[0] = 0x01; b[1] = op & 0xff; b[2] = op >> 8; b[3] = n;
	memcpy(b + 4, p, n);
	return write(tty, b, 4 + n) == 4 + n ? 0 : -1;
}

// wait for Command Complete (0x0e) of op or a QCA vendor event (0xff); returns status
static int wait_cc(uint16_t op, int ms, uint8_t *vse, int *vlen) {
	uint8_t par[260];
	int len;
	for (;;) {
		int ev = read_event(par, &len, ms);
		if (ev < 0) return -ETIMEDOUT;
		if (ev == 0x0e && len >= 4 && (par[1] | par[2] << 8) == op) return par[3];
		if (ev == 0xff) {
			if (vse) { memcpy(vse, par, len); *vlen = len; }
			if (op == EDL_OPCODE) return 0;
		}
	}
}

static int last_patch = -1;
static int read_version(void) {
	uint8_t c = EDL_PATCH_VER_REQ_CMD, v[260];
	int vl = 0;
	tcflush(tty, TCIFLUSH);
	if (send_cmd(EDL_OPCODE, &c, 1)) return -1;
	if (wait_cc(EDL_OPCODE, 1000, v, &vl) < 0 || vl < 14) return -1;
	// cresp, rtype, product_id(le32), patch_ver(le16), rom_ver(le16), soc_id(le32)
	printf("btinit: product 0x%08x patch 0x%04x rom 0x%04x soc 0x%08x\n",
		v[2] | v[3] << 8 | v[4] << 16 | (unsigned)v[5] << 24, v[6] | v[7] << 8, v[8] | v[9] << 8,
		v[10] | v[11] << 8 | v[12] << 16 | (unsigned)v[13] << 24);
	last_patch = v[6] | v[7] << 8;
	return 0;
}

static uint8_t *load(const char *path, int *len) {
	int fd = open(path, O_RDONLY);
	struct stat st;
	if (fd < 0 || fstat(fd, &st)) { perror(path); return NULL; }
	uint8_t *b = malloc(st.st_size);
	if (!b || read(fd, b, st.st_size) != st.st_size) { perror(path); return NULL; }
	close(fd);
	*len = st.st_size;
	return b;
}

// NVM: sequence of { le16 tag, le16 len, le32 r1, le32 r2, data[len] } after a 4-byte TLV header
static void patch_nvm(uint8_t *b, int len) {
	int i = 4, type = b[0], end = 4 + (b[1] | b[2] << 8 | b[3] << 16);
	// files with more than one tag set have an enclosing header of type 4 (as upstream btqca:
	// only the first, type 2, set is parsed)
	if (type == 4 && len >= 8) {
		type = b[4];
		end = 8 + (b[5] | b[6] << 8 | b[7] << 16);
		i = 8;
	}
	if (type != 2) { printf("btinit: NVM type %d not patched\n", type); return; }
	if (end > len) end = len;
	len = end;
	while (i + 12 <= len) {
		int tag = b[i] | b[i + 1] << 8, tl = b[i + 2] | b[i + 3] << 8;
		uint8_t *d = b + i + 12;
		if (i + 12 + tl > len) break;
		if (tag == 2 && tl == 6)
			printf("btinit: NVM BD address %02x:%02x:%02x:%02x:%02x:%02x\n", d[5], d[4], d[3], d[2], d[1], d[0]);
		if (tag == 17 && tl > 2) {                 // EDL_TAG_ID_HCI
			// data[0] bit 7 = software in-band sleep (upstream hci_qca sets it); H4 has no
			// IBS, the controller would send wake/sleep bytes that break H4 framing -> clear
			// data[2] = UART baud rate (WCN3988 < WCN3991 in btqca's soc order)
			printf("btinit: NVM tag 17 ibs 0x%02x -> 0x%02x, baud %d -> %d\n",
				d[0], d[0] & ~0x80, d[2], op_idx);
			d[0] &= ~0x80;
			d[2] = op_idx;
		} else if (tag == 27 && tl > 0) {          // EDL_TAG_ID_DEEP_SLEEP: no IBS with H4
			printf("btinit: NVM tag 27 deep sleep 0x%02x -> 0x%02x\n", d[0], d[0] & ~0x80);
			d[0] &= ~0x80;
		}
		i += 12 + tl;
	}
}

static int download(const char *name, uint8_t *b, int len) {
	int off = 0, segs = 0, acked = 0;
	while (off < len) {
		int n = len - off > SEG ? SEG : len - off;
		uint8_t c[2 + SEG];
		c[0] = EDL_PATCH_TLV_REQ_CMD; c[1] = n;
		memcpy(c + 2, b + off, n);
		if (send_cmd(EDL_OPCODE, c, n + 2)) { perror("write"); return -1; }
		off += n; segs++;
		// WCN399x may skip the per-segment events; the last segment is always acknowledged
		int last = off >= len;
		uint8_t v[260]; int vl = 0;
		int r = wait_cc(EDL_OPCODE, last ? 3000 : 30, v, &vl);
		if (r == 0 && vl >= 3 && v[1] == EDL_TVL_DNLD_RES_EVT) {
			acked++;
			if (v[2]) { fprintf(stderr, "btinit: %s segment %d status 0x%02x\n", name, segs, v[2]); return -1; }
		} else if (last && r < 0) {
			fprintf(stderr, "btinit: %s: no ack for the last segment\n", name);
			return -1;
		}
	}
	printf("btinit: %s: %d bytes, %d segments, %d acks\n", name, len, segs, acked);
	return 0;
}

int main(int argc, char **argv) {
	setvbuf(stdout, NULL, _IOLBF, 0);
	const char *dev = "/dev/ttyHS0";
	const char *patch = "/lib/firmware/qca/apbtfw11.tlv", *nvm = "/lib/firmware/qca/apnv11.bin";
	int o;
	while ((o = getopt(argc, argv, "t:p:n:b:")) != -1) {
		if (o == 't') dev = optarg; else if (o == 'p') patch = optarg; else if (o == 'n') nvm = optarg;
		else if (o == 'b') {
			int b = atoi(optarg);
			if (b == 3000000) { op_idx = 14; op_speed = B3000000; }
			else if (b == 2000000) { op_idx = 13; op_speed = B2000000; }
			else if (b == 1000000) { op_idx = 11; op_speed = B1000000; }
			else if (b == 921600) { op_idx = 10; op_speed = B921600; }
			else { fprintf(stderr, "btinit: -b 3000000|2000000|1000000|921600\n"); return 2; }
		}
		else { fprintf(stderr, "usage: %s [-t tty] [-p rampatch] [-n nvm] [-b baud]\n", argv[0]); return 2; }
	}
	int plen, nlen;
	uint8_t *pb = load(patch, &plen), *nb = load(nvm, &nlen);
	if (!pb || !nb) return 1;
	patch_nvm(nb, nlen);

	signal(SIGTERM, on_sig); signal(SIGINT, on_sig); signal(SIGHUP, on_sig);
	pwr = open("/dev/btpower", O_RDWR);
	if (pwr < 0 || ioctl(pwr, BT_CMD_PWR_CTRL, 1) < 0) { perror("btpower"); return 1; }
	usleep(300000);
	tty = open(dev, O_RDWR | O_NOCTTY);
	if (tty < 0) { perror(dev); return 1; }
	if (ioctl(tty, TIOCPMGET_, 0) < 0) perror("TIOCPMGET (UART clock vote)");
	// btpower "off" does not really cut the BT core (rails shared with WLAN): after an earlier
	// run the controller may still be patched and running at 3 or 3.2 Mbaud -> probe the speed
	speed_t found = 0;
	const speed_t try[] = { B115200, B3000000, B2000000, B1000000, B921600 };
	const char *tryname[] = { "115200", "3000000", "2000000", "1000000", "921600" };
	for (int k = 0; k < 5 && !found; k++) {
		set_speed(tty, try[k], 1);
		usleep(20000);
		// right after power-on the first command(s) may be lost or answered partially
		for (int a = 0; a < (k ? 1 : 3) && !found; a++) {
			tcflush(tty, TCIOFLUSH);
			send_cmd(0x0c03, NULL, 0);                 // HCI_Reset
			int rr = wait_cc(0x0c03, 500, NULL, NULL);
			printf("btinit: HCI_Reset @%s try %d: %d\n", tryname[k], a + 1, rr);
			if (rr == 0 || read_version() == 0) found = try[k];
		}
	}
	if (!found) {
		// 3.2 Mbaud (index 17, the stock NVM value) is not a standard termios speed: set it
		// through termios2 BOTHER
		struct termios2 { tcflag_t c_iflag, c_oflag, c_cflag, c_lflag; cc_t c_line; cc_t c_cc[19]; speed_t c_ispeed, c_ospeed; } t2;
		if (ioctl(tty, 0x802C542A /* TCGETS2 */, &t2) == 0) {
			t2.c_cflag &= ~CBAUD; t2.c_cflag |= 0010000 /* BOTHER */;
			t2.c_ispeed = t2.c_ospeed = 3200000;
			ioctl(tty, 0x402C542B /* TCSETS2 */, &t2);
			usleep(20000);
			tcflush(tty, TCIOFLUSH);
			send_cmd(0x0c03, NULL, 0);
			int rr = wait_cc(0x0c03, 600, NULL, NULL);
			printf("btinit: HCI_Reset @3200000: %d\n", rr);
			if (rr == 0) found = 1;
		}
	}
	if (!found) { fprintf(stderr, "btinit: controller does not answer at any known speed\n"); goto fail; }
	if (found != B115200) {
		if (read_version() == 0 && last_patch != 0x0001) {
			printf("btinit: controller already patched (patch 0x%04x), skipping download\n", last_patch);
			uint8_t b3 = op_idx;
			send_cmd(QCA_BAUD_OPCODE, &b3, 1);
			tcdrain(tty);
			usleep(20000);
			set_speed(tty, op_speed, 1);
			usleep(20000);
			goto attach;
		}
		fprintf(stderr, "btinit: controller at high speed but unpatched; switching to 115200\n");
		uint8_t b0 = 0; // QCA_BAUDRATE_115200
		send_cmd(QCA_BAUD_OPCODE, &b0, 1);
		tcdrain(tty);
		usleep(20000);
		set_speed(tty, B115200, 1);
		usleep(20000);
	}
	if (read_version()) { fprintf(stderr, "btinit: no answer from the controller @115200\n"); goto fail; }

	// switch to 3 Mbaud: no flow control while the controller changes speed (as hci_qca does)
	uint8_t baud = op_idx;
	set_speed(tty, B115200, 0);
	send_cmd(QCA_BAUD_OPCODE, &baud, 1);
	tcdrain(tty);
	usleep(20000);
	set_speed(tty, op_speed, 1);
	usleep(20000);
	if (read_version()) { fprintf(stderr, "btinit: no answer after switching to 3 Mbaud\n"); goto fail; }

	if (download("rampatch", pb, plen) || download("nvm", nb, nlen)) goto fail;
attach:
	usleep(50000);
	tcflush(tty, TCIFLUSH);
	send_cmd(0x0c03, NULL, 0);                         // HCI_Reset
	int r = wait_cc(0x0c03, 2000, NULL, NULL);
	if (r) { fprintf(stderr, "btinit: HCI_Reset failed (%d)\n", r); goto fail; }
	printf("btinit: controller ready, attaching H4 line discipline\n");

	int ld = N_HCI_;
	if (ioctl(tty, TIOCSETD, &ld) < 0) { perror("TIOCSETD N_HCI"); goto fail; }
	if (ioctl(tty, HCIUARTSETPROTO_, HCI_UART_H4) < 0) { perror("HCIUARTSETPROTO"); goto fail; }
	printf("btinit: hci device registered; holding the UART\n");
	fflush(stdout);
	while (!stop) pause();
	ld = 0;                                            // N_TTY: detaches hci0
	ioctl(tty, TIOCSETD, &ld);
	ioctl(tty, TIOCPMPUT_, 0);
	close(tty);
	ioctl(pwr, BT_CMD_PWR_CTRL, 0);
	return 0;
fail:
	ioctl(tty, TIOCPMPUT_, 0);
	close(tty);
	ioctl(pwr, BT_CMD_PWR_CTRL, 0);
	return 1;
}
