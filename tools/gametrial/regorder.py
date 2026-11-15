"""Find declarations that order integer and floating-point parameters differently from the function's definition.

On the Wii (PowerPC EABI) integer-class arguments go in r3..r10 and floating-point ones in f1..f8, each class in its
own order, so `f(int, u32, float, float)` and `f(int, float, float, u32)` call the same function identically and
code built from either declaration matches. On x86 (cdecl) every argument is on the stack in declaration order, so
the callee reads its parameters from the wrong slots (widgets' fn_802354A0 call put the colour -1 into x: NaN).

Reports each declaration whose parameter list differs from the definition's but has the same integer-class and
float-class sequences ("PPC-equivalent", silently wrong on x86), plus count/class mismatches separately.
Output: tab-separated lines for prep.py's adapter step, and a readable report (--report).
usage: regorder.py [--report] [src dir]   (default: port/build/gamesrc/game)
"""
import collections, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
args = [a for a in sys.argv[1:] if not a.startswith('--')]
SRC = args[0] if args else os.path.normpath(os.path.join(HERE, '..', '..', 'build', 'gamesrc', 'game'))

KW = {'if', 'while', 'for', 'switch', 'return', 'sizeof', 'case', 'else', 'do', 'new', 'delete', 'defined'}
DECL = re.compile(r'(?m)^[ \t]*(?:extern\s+(?:"C"\s+)?)?(?:static\s+)?(?:inline\s+)?(?:const\s+)?[A-Za-z_][\w:<>]*'
                  r'(?:\s*[\*&]+\s*|\s+)(?:[\*&]\s*)*([A-Za-z_]\w*)\s*\(([^()]*)\)\s*(;|\{)')
FLOATS = {'float', 'f32', 'double', 'f64', 'Float', 'real'}


def strip_comments(t):
    t = re.sub(r'/\*.*?\*/', lambda m: ' ' * len(m.group(0)), t, flags=re.S)
    return re.sub(r'//[^\n]*', lambda m: ' ' * len(m.group(0)), t)


def classes(params):
    """'i'/'f' per parameter, or None when unknown (variadic, by-value struct we cannot classify)."""
    p = params.strip()
    if p in ('', 'void'):
        return ''
    if '...' in p:
        return None
    out = ''
    for a in p.split(','):
        a = re.sub(r'\b(const|volatile|register|struct|class|unsigned|signed)\b', ' ', a).strip()
        if '*' in a or '&' in a or '[' in a:
            out += 'i'
            continue
        words = a.split()
        ty = words[0] if words else ''
        out += 'f' if ty in FLOATS else 'i'
    return out


def scan(texts):
    """texts: {unit file name: source}. Returns the PPC-equivalent mismatches as dicts with the declaration's span
    in the (unstripped) source, its return-type prefix and parameter text, and the definition's classes/params."""
    decls = collections.defaultdict(list)
    for f, raw in texts.items():
        t = strip_comments(raw)
        for m in DECL.finditer(t):
            name, params, end = m.group(1), m.group(2), m.group(3)
            if name in KW or '::' in t[max(0, m.start(1) - 2):m.start(1)]:
                continue
            cl = classes(params)
            if cl is None:
                continue
            decls[name].append({'file': f, 'start': m.start(), 'end': m.end(), 'name': name, 'params': params,
                                'prefix': raw[m.start():m.start(1)], 'cls': cl, 'def': end == '{'})
    out = []
    for name, ds in decls.items():
        defs = [d for d in ds if d['def']]
        if len(defs) != 1:
            continue
        df = defs[0]
        for d in ds:
            if d['def'] or d['file'] == df['file'] or d['cls'] == df['cls']:
                continue
            if d['cls'].replace('f', '') == df['cls'].replace('f', '') and d['cls'].count('f') == df['cls'].count('f'):
                out.append(dict(d, defcls=df['cls'], defparams=df['params'], deffile=df['file']))
    return out


def main():
    report = '--report' in sys.argv
    decls = collections.defaultdict(list)  # name -> [(file, line, classes, params, is_def)]
    for f in sorted(os.listdir(SRC)):
        if not f.endswith(('.cpp', '.c')):
            continue
        t = strip_comments(open(os.path.join(SRC, f), encoding='utf-8', errors='replace').read())
        for m in DECL.finditer(t):
            name, params, end = m.group(1), m.group(2), m.group(3)
            if name in KW or '::' in t[max(0, m.start(1) - 2):m.start(1)]:
                continue
            if not re.match(r'(fn|func)_[0-9A-Fa-f]{8}$', name) and name[0].islower():
                pass
            cl = classes(params)
            if cl is None:
                continue
            line = t.count('\n', 0, m.start()) + 1
            decls[name].append((f, line, cl, ' '.join(params.split()), end == '{'))
    swapped, other = [], []
    for name, ds in sorted(decls.items()):
        defs = [d for d in ds if d[4]]
        if len(defs) != 1:
            continue
        df = defs[0]
        for d in ds:
            if d[4] or d[0] == df[0] or d[2] == df[2]:
                continue
            same_classes = d[2].replace('f', '') == df[2].replace('f', '') and d[2].count('f') == df[2].count('f')
            (swapped if same_classes else other).append((name, d, df))
    if report:
        print('# Integer/float parameter order differs from the definition: %d declarations (%d functions)\n'
              % (len(swapped), len({s[0] for s in swapped})))
        print('PPC-equivalent: same calls on the Wii, wrong argument slots on x86.\n')
        for name, d, df in swapped:
            print('- %s  %s:%d `(%s)` vs definition %s:%d `(%s)`' % (name, d[0], d[1], d[3], df[0], df[1], df[3]))
        print('\n# Other class/count mismatches: %d (see ARITY.md for counts)\n' % len(other))
        for name, d, df in other:
            print('- %s  %s:%d `%s` vs %s:%d `%s`' % (name, d[0], d[1], d[2], df[0], df[1], df[2]))
    else:
        for name, d, df in swapped:
            print('\t'.join([d[0], str(d[1]), name, d[2], df[2], df[0]]))


if __name__ == '__main__':
    main()
