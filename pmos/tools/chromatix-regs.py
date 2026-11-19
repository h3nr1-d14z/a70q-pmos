#!/usr/bin/env python3
"""Dump the register tables of a Qualcomm CamX/Chromatix sensor module (.bin).

Layout found on the Galaxy A70 sensor modules (Parameter Parser V1.4.0):
  * every regSetting array is serialized as N records of 10 little-endian u64:
      [registerAddr, 1, idData, regAddrType, regDataType, operation, 1, idDelay, 0, idX]
    (regAddrType/regDataType 1 = byte, 2 = word; operation 0 = write)
  * the record block is directly followed by the out-of-line data, one 16-byte slot per record:
      [registerData (u64), delayUs (u64)]
Usage: chromatix-regs.py module.bin [out.txt]
"""
import struct
import sys


def u64(d, o):
    return struct.unpack_from('<Q', d, o)[0]


def is_record(d, o):
    if o + 80 > len(d):
        return False
    a, c1, _, at, dt, op, c2 = (u64(d, o + 8 * i) for i in range(7))
    return (a <= 0xffff and c1 == 1 and c2 == 1 and at in (1, 2) and dt in (1, 2) and
            op in (0, 1, 2) and u64(d, o + 16) + 1 == u64(d, o + 56))


def blocks(d):
    o = 0
    while o < len(d) - 80:
        if not is_record(d, o):
            o += 8
            continue
        start = o
        recs = []
        while is_record(d, o):
            recs.append(tuple(u64(d, o + 8 * i) for i in range(10)))
            o += 80
        # the 16-byte data slots start one slot before the end of the record walk (checked
        # against the known Samsung init prologue 0xfcfc <- 0x4000, 0x6028 <- 0x2000/0x4000)
        pool = o - 16
        if len(recs) >= 2:
            yield start, pool, recs
        o = pool + 16 * len(recs)


def main():
    d = open(sys.argv[1], 'rb').read()
    out = open(sys.argv[2], 'w') if len(sys.argv) > 2 else sys.stdout
    for n, (start, pool, recs) in enumerate(blocks(d)):
        print(f'# block {n}: {len(recs)} writes, records @0x{start:x}, data @0x{pool:x}', file=out)
        for i, r in enumerate(recs):
            val, delay = u64(d, pool + 16 * i), u64(d, pool + 16 * i + 8)
            w = 2 if r[4] == 2 else 1
            print(f'0x{r[0]:04x} 0x{val:0{2 * w}x}' + (f' delay {delay}' if delay else ''), file=out)
        print(file=out)


if __name__ == '__main__':
    main()
