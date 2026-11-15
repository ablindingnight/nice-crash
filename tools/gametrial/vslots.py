"""Find classes declared in several units where one unit has a virtual destructor and another declares a plain
virtual (placeholder) in the same slot. MWCC gives a virtual destructor one vtable slot, the Itanium ABI two, so
virtual calls from those units disagree on the PC. Prints class, slot, and each unit's view."""
import collections, glob, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, '..', '..', 'build', 'gamesrc', 'game'))
decl = re.compile(r'(?m)^(?:class|struct)\s+(\w+)\s*(?::\s*(?:public\s+)?(\w+))?\s*\{')
views = collections.defaultdict(list)
for f in sorted(glob.glob(os.path.join(SRC, '*.cpp'))):
    t = re.sub(r'//[^\n]*', '', open(f, encoding='utf-8', errors='replace').read())
    for m in decl.finditer(t):
        d, i = 1, m.end()
        while d and i < len(t):
            d += {'{': 1, '}': -1}.get(t[i], 0)
            i += 1
        body = t[m.end():i - 1]
        virts = []
        for v in re.finditer(r'virtual\s+[^;{]*?(~?\w+)\s*\(', body):
            virts.append(v.group(1))
        if virts:
            views[m.group(1)].append((os.path.basename(f), m.group(2), virts))
n = 0
for cls, vs in sorted(views.items()):
    if len(vs) < 2:
        continue
    for slot in range(max(len(v[2]) for v in vs)):
        names = {u: (v[slot] if slot < len(v) else None) for u, b, v in vs}
        dt = [u for u, x in names.items() if x and x.startswith('~')]
        other = [u for u, x in names.items() if x and not x.startswith('~')]
        if dt and other:
            n += 1
            print('%-24s slot %d: destructor in %s; plain virtual in %s' % (
                cls, slot, ','.join(dt), ', '.join('%s(%s)' % (u, names[u]) for u in other)))
            break
print('%d classes' % n, file=sys.stderr)
