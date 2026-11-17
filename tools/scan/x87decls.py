# x87 ABI hazards: a function returning float/double declared elsewhere with a non-float return (or the reverse).
# On 32-bit x86 a float result travels on the x87 stack; a caller that thinks the result is not a float never pops it
# (or pops nothing that was pushed), and after 8 such leaks every float load gives NaN.
# Usage: python -I tools/scan/x87decls.py [build/gamesrc]
import os, re, sys, collections
root = sys.argv[1] if len(sys.argv) > 1 else os.path.join('build', 'gamesrc')
FLOAT = {'f32', 'f64', 'float', 'double', 'f32*'}
pat = re.compile(r'^\s*(?:extern\s+"C"\s+)?(?:static\s+)?(?:inline\s+)?([A-Za-z_][\w:<>]*\s*\**)\s+(fn_8[0-9A-Fa-f]{7})\s*\(([^;{]*)\)\s*([;{])')
decl = collections.defaultdict(list)
for dp, _, fs in os.walk(root):
    for f in fs:
        if not f.endswith(('.cpp', '.h', '.c')):
            continue
        p = os.path.join(dp, f)
        for n, line in enumerate(open(p, encoding='utf-8', errors='replace'), 1):
            m = pat.match(line)
            if not m:
                continue
            ret = m.group(1).replace(' ', '')
            if ret in ('return', 'else', 'case', 'new', 'delete', 'goto'):
                continue
            decl[m.group(2)].append((ret, m.group(4) == '{', os.path.relpath(p, root), n))
def isfloat(r):
    return r in ('f32', 'f64', 'float', 'double')
bad = 0
for fn, ds in sorted(decl.items()):
    defs = [d for d in ds if d[1]]
    if not defs:
        continue
    dret = defs[0][0]
    for r, isdef, f, n in ds:
        if isdef:
            continue
        if isfloat(dret) != isfloat(r):
            bad += 1
            print(f'{fn}: defined {dret} ({defs[0][2]}:{defs[0][3]}), declared {r} ({f}:{n})')
print(f'{bad} mismatched declarations', file=sys.stderr)
