#!/usr/bin/env python3
"""Minimal Android boot image v1 tool (a70q). No external deps.

  bootimg.py info   <boot.img>
  bootimg.py unpack <boot.img> <outdir>          -> kernel, ramdisk, header.txt
  bootimg.py pack   --kernel K [--ramdisk R] --like <ref boot.img> -o out.img
                    [--cmdline STR] [--os-version 15.0.0] [--spl 2026-06]
     header fields (addrs, page size, board name, cmdline, os_version) are copied
     from --like unless overridden. Output = header + payload, page aligned,
     no AVB/SEANDROIDENFORCE footer (flash with a verification-disabled vbmeta).
"""
import argparse, hashlib, os, struct, sys

FMT = '<8s10I16s512s32s1024sIQI'   # boot_img_hdr_v1, 1648 bytes
NAMES = ('magic kernel_size kernel_addr ramdisk_size ramdisk_addr second_size second_addr '
         'tags_addr page_size header_version os_version name cmdline id extra_cmdline '
         'recovery_dtbo_size recovery_dtbo_offset header_size').split()


def parse(path):
    f = open(path, 'rb').read()
    if f[:8] != b'ANDROID!':
        sys.exit(f'{path}: not an Android boot image')
    h = dict(zip(NAMES, struct.unpack(FMT, f[:struct.calcsize(FMT)])))
    if h['header_version'] != 1:
        sys.exit(f'{path}: header v{h["header_version"]} unsupported')
    ps = h['page_size']
    al = lambda n: (n + ps - 1) // ps * ps
    ko = ps
    ro = ko + al(h['kernel_size'])
    h['_kernel'] = f[ko:ko + h['kernel_size']]
    h['_ramdisk'] = f[ro:ro + h['ramdisk_size']]
    h['_payload_end'] = ro + al(h['ramdisk_size']) + al(h['second_size'])
    h['_file'] = f
    return h


def dec_os(v):
    o, l = v >> 11, v & 0x7ff
    return f'{o >> 14}.{(o >> 7) & 0x7f}.{o & 0x7f}', f'{(l >> 4) + 2000}-{l & 0xf:02d}'


def enc_os(ver, spl):
    a, b, c = (int(x) for x in ver.split('.'))
    y, m = (int(x) for x in spl.split('-')[:2])
    return (((a << 14) | (b << 7) | c) << 11) | (((y - 2000) << 4) | m)


def cstr(b):
    return b.split(b'\0', 1)[0].decode(errors='replace')


def info(path):
    h = parse(path)
    ver, spl = dec_os(h['os_version'])
    f = h['_file']; end = h['_payload_end']
    tail = 'AVB footer' if f[end:end + 4] == b'AVB0' else \
           'SEANDROIDENFORCE' if f[end:end + 16] == b'SEANDROIDENFORCE' else \
           'none' if not f[end:].strip(b'\0') else 'unknown data'
    for k in NAMES:
        if k in ('magic', 'id', 'extra_cmdline'):
            continue
        v = h[k]
        print(f'{k:22s}', cstr(v) if isinstance(v, bytes) else hex(v))
    print(f'{"os_version/spl":22s} {ver} / {spl}')
    print(f'{"cmdline length":22s} {len(cstr(h["cmdline"]))}/511')
    print(f'{"id sha1 ok":22s} {compute_id(h["_kernel"], h["_ramdisk"]) == h["id"][:20]}')
    print(f'{"after payload":22s} {tail} (payload end {end}, file {len(f)})')
    k = h['_kernel']; gz = k.find(b'\x1f\x8b\x08')
    print(f'{"kernel format":22s} {"gzip@"+str(gz) if gz >= 0 else "raw?"}')


def compute_id(kernel, ramdisk, second=b'', dtbo=b''):
    s = hashlib.sha1()
    for blob in (kernel, ramdisk, second, dtbo):
        s.update(blob); s.update(struct.pack('<I', len(blob)))
    return s.digest()


def pack(a):
    ref = parse(a.like)
    kernel = open(a.kernel, 'rb').read()
    ramdisk = open(a.ramdisk, 'rb').read() if a.ramdisk else b''
    ps = ref['page_size']
    osv = ref['os_version']
    if a.os_version or a.spl:
        v0, s0 = dec_os(osv)
        osv = enc_os(a.os_version or v0, a.spl or s0)
    cmd = a.cmdline.encode() if a.cmdline is not None else ref['cmdline'].split(b'\0', 1)[0]
    if len(cmd) > 511:
        sys.exit(f'cmdline {len(cmd)} bytes > 511 (would be truncated)')
    hdr = struct.pack(FMT, b'ANDROID!', len(kernel), ref['kernel_addr'], len(ramdisk),
                      ref['ramdisk_addr'], 0, ref['second_addr'], ref['tags_addr'], ps, 1, osv,
                      ref['name'], cmd, compute_id(kernel, ramdisk).ljust(32, b'\0'),
                      b'', 0, 0, struct.calcsize(FMT))
    pad = lambda b: b + b'\0' * (-len(b) % ps)
    out = pad(hdr) + pad(kernel) + pad(ramdisk)
    open(a.o, 'wb').write(out)
    print(f'wrote {a.o} ({len(out)} bytes)')


def unpack(path, outdir):
    h = parse(path)
    os.makedirs(outdir, exist_ok=True)
    open(os.path.join(outdir, 'kernel'), 'wb').write(h['_kernel'])
    open(os.path.join(outdir, 'ramdisk'), 'wb').write(h['_ramdisk'])
    print(f'kernel {len(h["_kernel"])}  ramdisk {len(h["_ramdisk"])} -> {outdir}')


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    sp = p.add_subparsers(dest='cmd', required=True)
    sp.add_parser('info').add_argument('img')
    u = sp.add_parser('unpack'); u.add_argument('img'); u.add_argument('outdir')
    k = sp.add_parser('pack')
    k.add_argument('--kernel', required=True); k.add_argument('--ramdisk')
    k.add_argument('--like', required=True); k.add_argument('-o', required=True)
    k.add_argument('--cmdline'); k.add_argument('--os-version'); k.add_argument('--spl')
    a = p.parse_args()
    {'info': lambda: info(a.img), 'unpack': lambda: unpack(a.img, a.outdir), 'pack': lambda: pack(a)}[a.cmd]()
