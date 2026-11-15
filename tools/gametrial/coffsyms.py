"""Read COFF (i386) object symbol tables: defined/undefined externals per object.

Usage: python coffsyms.py <obj dir> [--json out.json]
Prints totals; with --json writes {obj: {"def": [...], "undef": [...], "common": {name: size}}}.
"""
import json, os, struct, sys


def read_syms(path):
    b = open(path, 'rb').read()
    machine, nsec, _t, symptr, nsym, optsz, _ch = struct.unpack_from('<HHIIIHH', b, 0)
    strtab = symptr + nsym * 18
    out = {'def': [], 'undef': [], 'common': {}}
    i = 0
    while i < nsym:
        e = symptr + i * 18
        raw = b[e:e + 8]
        if raw[:4] == b'\0\0\0\0':
            off = struct.unpack_from('<I', raw, 4)[0]
            name = b[strtab + off:b.index(b'\0', strtab + off)].decode('latin-1')
        else:
            name = raw.rstrip(b'\0').decode('latin-1')
        value, secno, typ, cls, naux = struct.unpack_from('<IhHBB', b, e + 8)
        if cls == 2:  # IMAGE_SYM_CLASS_EXTERNAL
            if secno == 0 and value == 0:
                out['undef'].append(name)
            elif secno == 0:
                out['common'][name] = value
            else:
                out['def'].append(name)
        elif cls == 105:  # weak external
            out['undef'].append(name)
        i += 1 + naux
    return out


def main():
    d = sys.argv[1]
    res = {}
    for f in sorted(os.listdir(d)):
        if f.endswith('.o'):
            res[f] = read_syms(os.path.join(d, f))
    defined = set()
    for r in res.values():
        defined.update(r['def'])
        defined.update(r['common'])
    undef = set()
    for r in res.values():
        undef.update(s for s in r['undef'] if s not in defined)
    print('objects %d, defined %d, unresolved %d' % (len(res), len(defined), len(undef)))
    if '--json' in sys.argv:
        json.dump({'objs': res, 'unresolved': sorted(undef)}, open(sys.argv[sys.argv.index('--json') + 1], 'w'))


if __name__ == '__main__':
    main()
