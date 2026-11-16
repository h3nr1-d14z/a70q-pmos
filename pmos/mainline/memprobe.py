#!/usr/bin/env python3
# Read/write physical memory through /dev/mem (downstream kernel r23+, CONFIG_DEVMEM=y).
#   memprobe.py mark  ADDR... : write a 16-byte marker "A70QMARK" + 8 hex digits of ADDR
#   memprobe.py check ADDR... : report which markers survived
#   memprobe.py dump  ADDR LEN [FILE] : raw dump (to FILE or hexdump to stdout)
#   memprobe.py text  ADDR LEN : printable text from a region
#   memprobe.py log3 : mainline kernel log (3 copies at 0x9ce00000, per-bit majority vote, patch 0014)
import mmap
import os
import sys

PAGE = 4096


def window(addr, length, write=False):
    base = addr & ~(PAGE - 1)
    off = addr - base
    size = (off + length + PAGE - 1) & ~(PAGE - 1)
    fd = os.open('/dev/mem', (os.O_RDWR if write else os.O_RDONLY) | os.O_SYNC)
    prot = mmap.PROT_READ | (mmap.PROT_WRITE if write else 0)
    m = mmap.mmap(fd, size, mmap.MAP_SHARED, prot, offset=base)
    os.close(fd)
    return m, off


def marker(addr):
    return b'A70QMARK' + b'%08x' % (addr & 0xffffffff)


def main():
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == 'mark':
        for a in args:
            addr = int(a, 16)
            try:
                m, off = window(addr, 16, True)
                m[off:off + 16] = marker(addr)[:16]
                m.flush()
                how = 'mmap'
            except OSError:
                fd = os.open('/dev/mem', os.O_RDWR | os.O_SYNC)
                os.pwrite(fd, marker(addr)[:16], addr)
                os.close(fd)
                how = 'pwrite'
            print('marked %#x (%s)' % (addr, how))
    elif cmd == 'check':
        for a in args:
            addr = int(a, 16)
            try:
                m, off = window(addr, 16)
                got = bytes(m[off:off + 16])
            except OSError:
                fd = os.open('/dev/mem', os.O_RDONLY | os.O_SYNC)
                got = os.pread(fd, 16, addr)
                os.close(fd)
            print('%#x %s %r' % (addr, 'KEPT' if got == marker(addr)[:16] else 'lost', got))
    elif cmd == 'dump':
        addr, length = int(args[0], 16), int(args[1], 0)
        m, off = window(addr, length)
        data = bytes(m[off:off + length])
        if len(args) > 2:
            open(args[2], 'wb').write(data)
        else:
            for i in range(0, len(data), 16):
                print('%08x %s' % (addr + i, data[i:i + 16].hex(' ')))
    elif cmd == 'log3':
        base, copy = 0x9ce00000, 0x50000
        fd = os.open('/dev/mem', os.O_RDONLY | os.O_SYNC)
        c = [os.pread(fd, copy, base + i * copy) for i in range(3)]
        os.close(fd)
        v = bytes((a & b) | (a & d) | (b & d) for a, b, d in zip(*c))
        if v[:8] != b'A70QLOG1':
            sys.exit('no mainline log (header %r)' % v[:16])
        n = int.from_bytes(v[8:12], 'little')
        seq = int.from_bytes(v[12:16], 'little')
        flips = sum(bin(x ^ y).count('1') for x, y in zip(c[0][:16 + n], v[:16 + n]))
        print('### mainline log: %d bytes, update #%d, %d bit flips fixed in copy 0' % (n, seq, flips))
        sys.stdout.write(v[16:16 + min(n, copy - 16)].decode('ascii', 'replace'))
    elif cmd == 'text':
        addr, length = int(args[0], 16), int(args[1], 0)
        m, off = window(addr, length)
        data = bytes(m[off:off + length])
        end = data.find(b'\0')
        sys.stdout.write(data[:end if end >= 0 else len(data)].decode('ascii', 'replace'))


if __name__ == '__main__':
    main()
