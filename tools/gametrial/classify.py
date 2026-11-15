"""Classify the port link's unresolved symbols using the decomp's symbols.txt and splits.txt (read-only).

Usage: python classify.py build/syms.json  -> prints a summary, writes build/unresolved.tsv
Columns: symbol (as in the object), base name, kind (func/data/?), unit, section, address, size, category
Categories: sdk (libs/RVL SDK, MSL, runtime, mp3), game-unbuilt (a src/game unit that did not compile or whose
symbol comes from asm), game-data (data label), compiler (x86 helpers), unknown.
"""
import collections, json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import decomp  # noqa: E402
CFG = os.path.join(decomp.root(), 'config', 'REXE01')


def load_symbols():
    syms = {}
    rx = re.compile(r'^(\S+) = (\.?\w+):0x([0-9A-Fa-f]+); // type:(\w+)(?: size:0x([0-9A-Fa-f]+))?')
    for line in open(os.path.join(CFG, 'symbols.txt'), encoding='utf-8'):
        m = rx.match(line)
        if m:
            name, sec, addr, typ, size = m.groups()
            syms.setdefault(name, (sec, int(addr, 16), typ, int(size, 16) if size else 0))
    return syms


def load_splits():
    units = []
    cur = None
    for line in open(os.path.join(CFG, 'splits.txt'), encoding='utf-8'):
        if line.strip().endswith(':') and not line.startswith('\t') and not line.startswith('Sections'):
            cur = line.strip()[:-1]
        elif cur and 'start:' in line:
            p = line.split()
            units.append((int(p[1].split(':')[1], 16), int(p[2].split(':')[1], 16), cur, p[0]))
    units.sort()
    return units


def unit_of(units, addr):
    lo, hi = 0, len(units)
    while lo < hi:
        mid = (lo + hi) // 2
        if units[mid][0] <= addr:
            lo = mid + 1
        else:
            hi = mid
    if lo and units[lo - 1][0] <= addr < units[lo - 1][1]:
        return units[lo - 1][2]
    return '?'


def itanium_leaf(sym):
    """Very small Itanium demangler: returns the unqualified name (leaf) and the enclosing scopes."""
    s = sym.lstrip('_')
    if not s.startswith('Z'):
        return sym.lstrip('_'), []
    s = s[1:]
    parts = []
    if s.startswith('N'):
        s = s[1:]
        while s and s[0] in 'KVr':
            s = s[1:]
        while s and s[0] != 'E':
            m = re.match(r'(\d+)', s)
            if not m:
                if s.startswith('C1') or s.startswith('C2'):
                    parts.append(parts[-1] if parts else '?')
                    s = s[2:]
                    continue
                if s.startswith('D0') or s.startswith('D1') or s.startswith('D2'):
                    parts.append('~' + (parts[-1] if parts else '?'))
                    s = s[2:]
                    continue
                break
            n = int(m.group(1))
            parts.append(s[len(m.group(1)):len(m.group(1)) + n])
            s = s[len(m.group(1)) + n:]
        return (parts[-1] if parts else sym), parts[:-1]
    m = re.match(r'(\d+)', s)
    if m:
        n = int(m.group(1))
        return s[len(m.group(1)):len(m.group(1)) + n], []
    return sym, []


def main():
    data = json.load(open(sys.argv[1]))
    syms = load_symbols()
    # MWCC names in symbols.txt: "Name__Fv", "Name__5Class..." -> index by leaf name too.
    by_leaf = collections.defaultdict(list)
    for n, v in syms.items():
        leaf = n.split('__')[0] if '__' in n[1:] else n
        by_leaf[leaf].append((n, v))
    units = load_splits()
    rows = []
    for sym in data['unresolved']:
        leaf, scope = itanium_leaf(sym)
        hit = None
        if leaf in syms:
            hit = (leaf, syms[leaf])
        elif by_leaf.get(leaf):
            cands = by_leaf[leaf]
            if scope:
                cls = scope[-1]
                c2 = [c for c in cands if ('__%d%s' % (len(cls), cls)) in c[0]]
                cands = c2 or cands
            hit = cands[0]
        if hit:
            name, (sec, addr, typ, size) = hit
            unit = unit_of(units, addr)
            if unit.startswith('game/'):
                cat = 'game-data' if typ == 'object' else 'game-code'
            else:
                cat = 'sdk-data' if typ == 'object' else 'sdk-code'
            rows.append((sym, '::'.join(scope + [leaf]), typ, unit, sec, '%08X' % addr, size, cat))
        else:
            cat = 'compiler' if re.match(r'^_*(_alldiv|_aulldiv|_allmul|__chkstk|___chkstk|_alloca|__udivdi3|__divdi3)', sym) else 'unknown'
            rows.append((sym, '::'.join(scope + [leaf]), '?', '?', '?', '', 0, cat))
    rows.sort(key=lambda r: (r[7], r[3], r[5]))
    with open(os.path.join(PORT, 'build', 'unresolved.tsv'), 'w', encoding='utf-8') as f:
        f.write('symbol\tname\tkind\tunit\tsection\taddress\tsize\tcategory\n')
        for r in rows:
            f.write('\t'.join(str(x) for x in r) + '\n')
    c = collections.Counter(r[7] for r in rows)
    print(dict(c))
    for cat in ('sdk-code', 'sdk-data', 'game-code', 'game-data'):
        u = collections.Counter(r[3] for r in rows if r[7] == cat)
        print('\n%s by unit (top 25 of %d units):' % (cat, len(u)))
        for k, v in u.most_common(25):
            print('  %4d %s' % (v, k))
    print('\nunknown sample:', [r[1] for r in rows if r[7] == 'unknown'][:40])


if __name__ == '__main__':
    main()
