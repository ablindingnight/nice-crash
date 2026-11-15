"""Replay a FIFO dump (NICECRASH_FIFODUMP) with the same rules as sdk/gxinterp.cpp; report the first desync."""
import struct, sys
b = open(sys.argv[1], 'rb').read()
cp = [0] * 256
def vsize(vat):
    lo, hi, a, bb, c = cp[0x50], cp[0x60], cp[0x70 + vat], cp[0x80 + vat], cp[0x90 + vat]
    cs = [1, 1, 2, 2, 4, 0, 0, 0]; col = [2, 3, 4, 2, 3, 4, 0, 0]
    at = lambda t, d: 0 if t == 0 else d if t == 1 else 1 if t == 2 else 2
    s = (lo & 1) + sum((lo >> (1 + i)) & 1 for i in range(8))
    s += at((lo >> 9) & 3, (3 if a & 1 else 2) * cs[(a >> 1) & 7])
    s += at((lo >> 11) & 3, (9 if (a >> 9) & 1 else 3) * cs[(a >> 10) & 7])
    s += at((lo >> 13) & 3, col[(a >> 14) & 7]) + at((lo >> 15) & 3, col[(a >> 18) & 7])
    tc = [((a >> 21) & 1, (a >> 22) & 7), (bb & 1, (bb >> 1) & 7), ((bb >> 9) & 1, (bb >> 10) & 7), ((bb >> 18) & 1, (bb >> 19) & 7),
          ((bb >> 27) & 1, (bb >> 28) & 7), ((c >> 5) & 1, (c >> 6) & 7), ((c >> 14) & 1, (c >> 15) & 7), ((c >> 23) & 1, (c >> 24) & 7)]
    for i, (n, f) in enumerate(tc):
        s += at((hi >> (i * 2)) & 3, (2 if n else 1) * cs[f])
    return s
pos = 0; fl = 0
while pos < len(b):
    n = struct.unpack_from('<I', b, pos)[0]; d = b[pos + 4:pos + 4 + n]; pos += 4 + n
    p = 0; last = []
    while p < len(d):
        op = d[p]; start = p; p += 1
        if op in (0, 0x48, 0x44): kind = 'nop'
        elif op == 0x08: cp[d[p]] = struct.unpack_from('>I', d, p + 1)[0]; kind = 'CP %02x=%08x' % (d[p], cp[d[p]]); p += 5
        elif op == 0x10:
            h = struct.unpack_from('>I', d, p)[0]; p += 4 + ((h >> 16) + 1) * 4; kind = 'XF %04x x%d' % (h & 0xFFFF, (h >> 16) + 1)
        elif op in (0x20, 0x28, 0x30, 0x38): p += 4; kind = 'IDX'
        elif op == 0x40: p += 8; kind = 'CALLDL'
        elif op == 0x61: kind = 'BP %02x' % d[p]; p += 4
        elif 0x80 <= op < 0xC0:
            cnt = struct.unpack_from('>H', d, p)[0]; vs = vsize(op & 7); p += 2 + vs * cnt
            kind = 'DRAW prim %02x vat %d n %d vsize %d (VCD %08x/%08x VAT %08x %08x %08x)' % (op & 0xF8, op & 7, cnt, vs, cp[0x50], cp[0x60], cp[0x70 + (op & 7)], cp[0x80 + (op & 7)], cp[0x90 + (op & 7)])
        else:
            print('flush %d: unknown opcode %02x at %d; previous commands:' % (fl, op, start))
            for k, s0 in last[-6:]: print('   %s  @%d: %s' % (k, s0, d[s0:s0 + 24].hex(' ')))
            print('   here: %s' % d[start:start + 32].hex(' '))
            sys.exit(0)
        last.append((kind, start))
    fl += 1
print('no desync in %d flushes' % fl)
