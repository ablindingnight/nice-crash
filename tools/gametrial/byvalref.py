"""Find declarations that pass a struct BY VALUE where the function's definition takes a pointer or reference.

The Wii's PowerPC EABI passes every aggregate argument by reference (the caller makes a copy and passes its
address in a GPR), so `void f(void* e, ValVec3 p)` and `void f(void* e, const V3* p)` call the same function
identically, and the decomp has such pairs (stuntpart's fn_8028E584 vs partman's definition). On x86 cdecl a
by-value struct is copied onto the stack, so the callee reads its coordinates as a pointer.

prep.py rewrites each such parameter in the declaration to `const T&` (C++ units): an address at the ABI level,
which is what the Wii passed. The reverse (pointer declared, struct by value defined) is reported only.
usage: byvalref.py [--report] [src dir]   (default: port/build/gamesrc/game)
"""
import collections, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import regorder  # noqa: E402  (DECL, strip_comments, KW)

SCALARS = {'void', 'char', 'short', 'int', 'long', 'float', 'double', 'bool', 'unsigned', 'signed', 'u8', 's8',
           'u16', 's16', 'u32', 's32', 'u64', 's64', 'f32', 'f64', 'BOOL', 'size_t', 'uintptr_t', 'intptr_t',
           'vu8', 'vu16', 'vu32', 'vs32', 'Float', 'real', 'wchar_t', 'Mtx', 'Mtx44', 'MtxPtr', 'ROMtx'}
AGG = re.compile(r'\b(?:struct|class|union)\s+([A-Za-z_]\w*)\s*(?::[^{;]*)?\{')
TYPEDEF_AGG = re.compile(r'\btypedef\s+(?:struct|union)\s*\w*\s*\{')


def aggregates(texts):
    """Names declared as struct/class/union (with a body) in any text."""
    names = set()
    for t in texts:
        names.update(AGG.findall(t))
        for m in TYPEDEF_AGG.finditer(t):
            depth, i = 1, m.end()
            while i < len(t) and depth:
                depth += {'{': 1, '}': -1}.get(t[i], 0)
                i += 1
            n = re.match(r'\s*(\w+)', t[i:])
            if n:
                names.add(n.group(1))
    return names


def split_params(params):
    p = params.strip()
    if p in ('', 'void') or '...' in p or '(' in p:
        return None
    return [a.strip() for a in p.split(',')]


def byvalue_type(a, aggs):
    """The aggregate type name when parameter text `a` is a struct passed by value, else None."""
    if '*' in a or '&' in a or '[' in a:
        return None
    words = re.sub(r'\b(const|volatile|struct|class|union|register)\b', ' ', a).split()
    if not words:
        return None
    ty = words[0]
    if ty in SCALARS or ty not in aggs:
        return None
    return ty


def is_indirect(a):
    return '*' in a or '&' in a or '[' in a


def scan(texts, extra_texts=()):
    """texts: {unit: source}. Returns (fixes, reverse): fixes are dicts with the declaration span and the parameter
    indexes to turn into references; reverse lists (name, file, index) where the definition takes the value."""
    aggs = aggregates(list(texts.values()) + list(extra_texts))
    decls = collections.defaultdict(list)
    for f, raw in texts.items():
        t = regorder.strip_comments(raw)
        for m in regorder.DECL.finditer(t):
            name, params, end = m.group(1), m.group(2), m.group(3)
            if name in regorder.KW or '::' in t[max(0, m.start(1) - 2):m.start(1)]:
                continue
            ps = split_params(params)
            if ps is None:
                continue
            decls[name].append({'file': f, 'start': m.start(2), 'end': m.end(2), 'params': ps, 'def': end == '{'})
    fixes, reverse = [], []
    for name, ds in decls.items():
        defs = [d for d in ds if d['def']]
        if len(defs) != 1:
            continue
        df = defs[0]
        for d in ds:
            if d['def'] or d['file'] == df['file'] or len(d['params']) != len(df['params']):
                continue
            idx = [i for i, (a, b) in enumerate(zip(d['params'], df['params']))
                   if byvalue_type(a, aggs) and is_indirect(b)]
            if idx:
                fixes.append(dict(d, name=name, idx=idx, deffile=df['file']))
            for i, (a, b) in enumerate(zip(d['params'], df['params'])):
                if is_indirect(a) and byvalue_type(b, aggs):
                    reverse.append((name, d['file'], i))
    return fixes, reverse


def apply(texts, fixes):
    """Rewrites the parameters (C++ units only). Returns (texts, count, skipped C declarations)."""
    by_file = collections.defaultdict(list)
    for fx in fixes:
        by_file[fx['file']].append(fx)
    n, skipped = 0, []
    for f, fs in by_file.items():
        if not f.endswith(('.cpp', '.cc')):
            skipped += [(fx['name'], f) for fx in fs]
            continue
        t = texts[f]
        for fx in sorted(fs, key=lambda x: -x['start']):
            ps = list(fx['params'])
            for i in fx['idx']:
                a = re.sub(r'^\s*const\s+', '', ps[i])
                words = a.split()
                ps[i] = 'const %s& %s' % (words[0], ' '.join(words[1:])) if len(words) > 1 else 'const %s&' % words[0]
            t = t[:fx['start']] + ', '.join(ps) + t[fx['end']:]
            n += 1
        texts[f] = t
    return texts, n, skipped


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    src = args[0] if args else os.path.normpath(os.path.join(HERE, '..', '..', 'build', 'gamesrc', 'game'))
    inc = os.path.normpath(os.path.join(src, '..', 'include'))
    texts = {f: open(os.path.join(src, f), encoding='utf-8', errors='replace').read()
             for f in sorted(os.listdir(src)) if f.endswith(('.cpp', '.c'))}
    extra = [open(os.path.join(r, f), encoding='utf-8', errors='replace').read()
             for r, _, fs in os.walk(inc) for f in fs if f.endswith('.h')]
    fixes, reverse = scan(texts, extra)
    for fx in fixes:
        print('%s\t%s\t(%s)\tdefinition in %s' % (fx['name'], fx['file'], ', '.join(fx['params']), fx['deffile']))
    print('# %d declarations pass a struct by value to a pointer/reference parameter' % len(fixes))
    for r in reverse:
        print('reverse\t%s\t%s\targ %d' % r)
    print('# %d reverse cases (pointer declared, value defined)' % len(reverse))


if __name__ == '__main__':
    main()
