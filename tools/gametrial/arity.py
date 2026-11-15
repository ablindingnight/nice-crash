"""Find functions whose declarations and definition disagree on the number of parameters across game units.

On the Wii (MWCC, PPC register ABI) a caller that declares `void f(void)` and calls `f()` still passes whatever is
in r3, so code that relies on it matches and works. On x86 the callee reads garbage. extern "C" names are not
mangled, so the linker cannot catch it. Usage: python arity.py [src dir]  (default: port/build/gamesrc/game)
"""
import collections, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.normpath(os.path.join(HERE, '..', '..', 'build', 'gamesrc', 'game'))

KW = {'if', 'while', 'for', 'switch', 'return', 'sizeof', 'case', 'else', 'do', 'new', 'delete', 'defined'}
DECL = re.compile(r'(?m)^[ \t]*(?:extern\s+(?:"C"\s+)?)?(?:static\s+)?(?:inline\s+)?(?:const\s+)?[A-Za-z_][\w:<>]*'
                  r'(?:\s*[\*&]+\s*|\s+)(?:[\*&]\s*)*([A-Za-z_]\w*)\s*\(([^()]*)\)\s*(;|\{)')


def nparams(p):
    p = re.sub(r'/\*.*?\*/', '', p).strip()
    if p in ('', 'void'):
        return 0
    if '...' in p:
        return -1  # variadic: skip
    return p.count(',') + 1


def strip_comments(t):
    t = re.sub(r'/\*.*?\*/', lambda m: ' ' * len(m.group(0)), t, flags=re.S)
    return re.sub(r'//[^\n]*', '', t)


def main():
    decls = collections.defaultdict(list)  # name -> [(file, line, n, is_def)]
    for f in sorted(os.listdir(SRC)):
        if not f.endswith(('.cpp', '.c')):
            continue
        t = strip_comments(open(os.path.join(SRC, f), encoding='utf-8', errors='replace').read())
        for m in DECL.finditer(t):
            name, params, end = m.group(1), m.group(2), m.group(3)
            if name in KW or '::' in t[max(0, m.start(1) - 2):m.start(1)]:
                continue
            n = nparams(params)
            if n < 0:
                continue
            line = t.count('\n', 0, m.start()) + 1
            decls[name].append((f, line, n, end == '{'))
    bad = []
    for name, lst in decls.items():
        defs = [d for d in lst if d[3]]
        if len(defs) != 1:
            continue  # unknown or overloaded/static-per-file: skip
        dn = defs[0][2]
        for f, line, n, is_def in lst:
            if not is_def and n != dn and f != defs[0][0] or (not is_def and f == defs[0][0] and n != dn):
                bad.append((name, defs[0][0], dn, f, line, n))
    bad.sort()
    for b in bad:
        print('%-28s defined in %-18s with %d params; declared in %s:%d with %d' % b)
    print('%d mismatching declarations' % len(bad), file=sys.stderr)


if __name__ == '__main__':
    main()
