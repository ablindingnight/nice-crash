"""Find declarations that say a function returns a full int where its definition returns a narrower integer.

On the Wii, MWCC's callee leaves an 8/16-bit return value zero- or sign-extended in r3, so a caller that declared
`s32 f(...)` for `u8 f(...)` reads the right number. On x86 the callee returns the byte in AL and the upper bits of
EAX are whatever was there: the caller reads garbage (texture.cpp's `u8 fn_802CAFF0` declared `s32` in model.cpp
made texture priorities 0xFF32, which overflowed the model draw bins and wiped the draw queue).

prep.py rewrites each such declaration's return type to the definition's type, which is what the Wii computed.
usage: retwidth.py [src dir]   (default: port/build/gamesrc/game) -> report only
"""
import collections, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import regorder  # noqa: E402  (strip_comments, KW)

NARROW = {'u8', 's8', 'u16', 's16', 'bool', 'char', 'unsigned char', 'signed char', 'short', 'unsigned short',
          'signed short', 'vu8', 'vu16', 'vs8', 'vs16'}
WIDE = {'int', 's32', 'u32', 'long', 'unsigned', 'unsigned int', 'signed int', 'unsigned long', 'signed', 'BOOL',
        'vs32', 'vu32'}
CANON = {'u8': 'unsigned char', 's8': 'signed char', 'u16': 'unsigned short', 's16': 'short', 'vu8': 'unsigned char',
         'vu16': 'unsigned short', 'vs8': 'signed char', 'vs16': 'short'}
FUNC = re.compile(r'(?m)^([ \t]*(?:extern\s+(?:"C"\s+)?)?(?:static\s+)?(?:inline\s+)?)'
                  r'((?:unsigned|signed)\s+(?:char|short|int|long)\b|unsigned\b|signed\b|[A-Za-z_]\w*)'
                  r'(\s+)([A-Za-z_]\w*)\s*\(([^()]*)\)\s*(;|\{)')


def scan(texts):
    """texts: {unit: source}. Returns fixes: dicts with file, the return type's span, the new type."""
    decls = collections.defaultdict(list)
    for f, raw in texts.items():
        t = regorder.strip_comments(raw)
        for m in FUNC.finditer(t):
            ret, name, end = ' '.join(m.group(2).split()), m.group(4), m.group(6)
            if name in regorder.KW or ret in ('return', 'else', 'case', 'goto', 'typedef', 'new', 'delete'):
                continue
            static = 'static' in m.group(1)
            decls[name].append({'file': f, 'start': m.start(2), 'end': m.end(2), 'ret': ret, 'def': end == '{',
                                'static': static})
    fixes = []
    for name, ds in decls.items():
        defs = [d for d in ds if d['def']]
        if len(defs) != 1 or defs[0]['static'] or defs[0]['ret'] not in NARROW:
            continue
        df = defs[0]
        for d in ds:
            if d['def'] or d['static'] or d['file'] == df['file'] or d['ret'] not in WIDE:
                continue
            # Plain C spellings: the declaring unit may not have the SDK typedefs (u8, s16, ...).
            new = CANON.get(df['ret'], df['ret'])
            if new == 'bool' and not d['file'].endswith(('.cpp', '.cc')):
                new = 'unsigned char'
            fixes.append(dict(d, name=name, new=new, deffile=df['file']))
    return fixes


FLOATS = {'f32': 'float', 'f64': 'double', 'float': 'float', 'double': 'double'}


def scan_x87(texts):
    """Declarations whose return type disagrees with the definition on whether it is a float. On 32-bit x86 a float
    result travels on the x87 register stack: a caller that declared it void/int never pops it (one register leaked
    per call; after 8 every float load is NaN: carbody's `void fn_801E9DD0` for ground.cpp's f32 broke Excite Race
    physics), and a caller that declared f32 for a void function pops a register nothing pushed. On the PPC both just
    ignore f1. Returns fixes in scan()'s shape: the declaration gets the definition's return type."""
    decls = collections.defaultdict(list)
    for f, raw in texts.items():
        t = regorder.strip_comments(raw)
        for m in FUNC.finditer(t):
            ret, name, end = ' '.join(m.group(2).split()), m.group(4), m.group(6)
            if name in regorder.KW or ret in ('return', 'else', 'case', 'goto', 'typedef', 'new', 'delete'):
                continue
            decls[name].append({'file': f, 'start': m.start(2), 'end': m.end(2), 'ret': ret, 'def': end == '{',
                                'static': 'static' in m.group(1)})
    fixes = []
    for name, ds in decls.items():
        if not name.startswith('fn_8'):  # address names are unique; a plain name (End) can be two functions
            continue
        defs = [d for d in ds if d['def'] and not d['static']]
        if len(defs) != 1:
            continue
        df = defs[0]
        dfloat = df['ret'] in FLOATS
        for d in ds:
            if d['def'] or d['static'] or d['file'] == df['file']:
                continue
            if (d['ret'] in FLOATS) == dfloat:
                continue
            new = FLOATS.get(df['ret'], 'void' if df['ret'] == 'void' else CANON.get(df['ret'], df['ret']))
            fixes.append(dict(d, name=name, new=new, deffile=df['file']))
    return fixes


def apply(texts, fixes):
    by_file = collections.defaultdict(list)
    for fx in fixes:
        by_file[fx['file']].append(fx)
    n = 0
    for f, fs in by_file.items():
        t = texts[f]
        for fx in sorted(fs, key=lambda x: -x['start']):
            t = t[:fx['start']] + fx['new'] + t[fx['end']:]
            n += 1
        texts[f] = t
    return texts, n


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    src = args[0] if args else os.path.normpath(os.path.join(HERE, '..', '..', 'build', 'gamesrc', 'game'))
    texts = {f: open(os.path.join(src, f), encoding='utf-8', errors='replace').read()
             for f in sorted(os.listdir(src)) if f.endswith(('.cpp', '.c'))}
    fixes = scan(texts)
    for fx in fixes:
        print('%s\t%s\t%s -> %s\tdefinition in %s' % (fx['name'], fx['file'], fx['ret'], fx['new'], fx['deffile']))
    print('# %d declarations widen a narrow return type' % len(fixes))


if __name__ == '__main__':
    main()
