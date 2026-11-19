#!/usr/bin/env python3
"""Dump the register tables of a Qualcomm CamX/Chromatix sensor module (.bin).

Layout found on the Galaxy A70 sensor modules (Parameter Parser V1.4.0):
  * a regSetting array of N entries is serialized as records of little-endian u64:
      [registerAddr, nData, idData, regAddrType, regDataType, operation, nDelay, idDelay, 0, idNext]
    the last record of the array has only the first 8 words.
    (regAddrType/regDataType: 1 = byte, 2 = word; operation: 0 = write, 1 = burst write)
  * then the out-of-line arrays, in record order: registerData[nData] then delayUs[nDelay],
    u32 elements, each array padded to 8 bytes.
  A burst write (operation 1) writes all its data words to the same (auto-incrementing) address.
Usage: chromatix-regs.py module.bin [out.txt]
Output lines: "addr value [delay N]" (bursts expanded to one line per data word).
"""
import struct
import sys


def u64(d, o):
    return struct.unpack_from('<Q', d, o)[0]


def u32(d, o):
    return struct.unpack_from('<I', d, o)[0]


def is_record(d, o):
    if o + 64 > len(d):
        return False
    a, nd, idd, at, dt, op, ndl, iddl = (u64(d, o + 8 * i) for i in range(8))
    return (a <= 0xffff and 1 <= nd <= 0x4000 and at in (1, 2) and dt in (1, 2) and
            op in (0, 1, 2) and ndl <= 0x4000 and iddl == idd + 1)


def align8(n):
    return (n + 7) & ~7


def blocks(d):
    o = 0
    while o < len(d) - 64:
        if not is_record(d, o):
            o += 8
            continue
        start = o
        recs = []
        while True:
            recs.append(tuple(u64(d, o + 8 * i) for i in range(8)))
            # a following record continues the array (80-byte stride); otherwise this is the last
            if u64(d, o + 64) == 0 and is_record(d, o + 80):
                o += 80
            else:
                o += 64
                break
        pool = o
        entries = []
        for r in recs:
            nd, ndl = r[1], r[6]
            data = [u32(d, pool + 4 * i) for i in range(nd)]
            pool += align8(4 * nd)
            delays = [u32(d, pool + 4 * i) for i in range(ndl)]
            pool += align8(4 * ndl)
            entries.append((r, data, delays))
        if len(recs) >= 2:
            yield start, o, entries
        o = pool


def main():
    d = open(sys.argv[1], 'rb').read()
    out = open(sys.argv[2], 'w') if len(sys.argv) > 2 else sys.stdout
    for n, (start, pool, entries) in enumerate(blocks(d)):
        writes = sum(len(e[1]) for e in entries)
        print(f'# block {n}: {len(entries)} entries, {writes} writes, records @0x{start:x}, '
              f'data @0x{pool:x}', file=out)
        for r, data, delays in entries:
            width = 4 if r[4] == 2 else 2
            for i, v in enumerate(data):
                line = f'0x{r[0]:04x} 0x{v:0{width}x}'
                if i == len(data) - 1 and delays and delays[0]:
                    line += f' delay {delays[0]}'
                print(line, file=out)
        print(file=out)


if __name__ == '__main__':
    main()
