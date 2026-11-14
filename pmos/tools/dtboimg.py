#!/usr/bin/env python3
"""Android DTBO image (magic 0xd7b7ab1e) unpack / repack.

  dtboimg.py unpack dtbo.img outdir        -> outdir/e0.dtbo ... + outdir/table.txt
  dtboimg.py pack   dtbo.img outdir new.img -> new.img with outdir/e*.dtbo, header/ids
                                              copied from the original dtbo.img
Entries are packed back-to-back like the Samsung original (not page-aligned).
"""
import os, struct, sys

MAGIC = 0xd7b7ab1e

def read(img):
    d = open(img, 'rb').read()
    magic, total, hsz, esz, cnt, off, pg, ver = struct.unpack('>8I', d[:32])
    assert magic == MAGIC, hex(magic)
    ents = [struct.unpack('>8I', d[off + i * esz: off + (i + 1) * esz]) for i in range(cnt)]
    return d, (magic, total, hsz, esz, cnt, off, pg, ver), ents

def unpack(img, out):
    d, h, ents = read(img)
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, 'table.txt'), 'w') as t:
        t.write('header %s\n' % (h,))
        for i, e in enumerate(ents):
            open(os.path.join(out, 'e%d.dtbo' % i), 'wb').write(d[e[1]:e[1] + e[0]])
            t.write('e%d size=%d off=%d id=%#x rev=%#x custom=%s\n' % (i, e[0], e[1], e[2], e[3], e[4:]))
    print('%d entries -> %s' % (len(ents), out))

def pack(img, src, new):
    _, h, ents = read(img)
    magic, _, hsz, esz, cnt, off, pg, ver = h
    blobs = [open(os.path.join(src, 'e%d.dtbo' % i), 'rb').read() for i in range(cnt)]
    pos = off + cnt * esz
    table, body = b'', b''
    for e, b in zip(ents, blobs):
        table += struct.pack('>8I', len(b), pos, *e[2:])
        body += b
        pos += len(b)
    out = struct.pack('>8I', magic, pos, hsz, esz, cnt, off, pg, ver) + table + body
    open(new, 'wb').write(out)
    print('%s: %d bytes, %d entries' % (new, len(out), cnt))

if __name__ == '__main__':
    if len(sys.argv) == 4 and sys.argv[1] == 'unpack':
        unpack(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 5 and sys.argv[1] == 'pack':
        pack(sys.argv[2], sys.argv[3], sys.argv[4])
    else:
        sys.exit(__doc__)
