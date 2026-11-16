"""Port source prep: copy excite-truck/src/game into port/build/gamesrc with MECHANICAL dialect rewrites only
(MWCC accepts, clang rejects, meaning is identical). Never touches the decomp. Anything that changes meaning
goes to docs/SOURCE_NOTES.md as a write-up for the decomp sessions instead.

Each rule: (name, regex, replacement). Counts per rule are printed so we can see what the port depends on.
"""
import glob, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import decomp  # noqa: E402
ET = decomp.root()  # the pinned decomp snapshot (tools/gametrial/decomp.py)
OUT = os.path.join(PORT, 'build', 'gamesrc')

RULES = [
    # The GX write-gather pipe at 0xCC008000 -> the host FIFO object (shim/include/port_fifo.h).
    ('wgpipe-union', re.compile(r'\(\*\(volatile\s+WGPipe\s*\*\)\s*0xCC008000\)'), 'port_wgpipe'),
    ('wgpipe-u8', re.compile(r'\(\*\(volatile\s+u8\s*\*\)\s*0xCC008000\)'), 'port_wgpipe.uc'),
    # libm prototypes the decomp wrote with float parameters (`f64 sqrt(f32)`, Bink's `pow(f32, f64)`): on PPC a
    # float and a double argument both travel in an FPR, on x86 cdecl a float pushes 4 bytes where libm reads 8, so
    # the result is garbage (the car integrator's quaternion normalise: 1/sqrt -> inf, 0*inf -> NaN). Real types.
    ('libm-double-params', re.compile(r'\b(f64|double)(\s+)(sqrt|pow|sin|cos|tan|atan2?|asin|acos|exp|log|log10|fmod|floor|ceil|fabs)\s*\(\s*(?:f32|float)\s*(\)|,)'),
     r'\1\2\3(f64\4'),
    # The allocation operators must take size_t; on the Wii size_t is unsigned long, on x86 unsigned int.
    ('operator-new-size_t', re.compile(r'operator new(\[\])?\(\s*unsigned long\b'), r'operator new\1(size_t'),
    # 32-bit `long` -> `int`: one type on MWCC (u32 is unsigned long there), two types to clang where the port's
    # u32 is unsigned int (to match x86 size_t). Same size on both targets; `long long` and `long double` untouched.
    ('long32-unsigned', re.compile(r'\bunsigned\s+long\b(?!\s+(?:long|double)\b)'), 'unsigned int'),
    ('long32-signed', re.compile(r'\bsigned\s+long\b(?!\s+(?:long|double)\b)'), 'signed int'),
    ('long32-plain', re.compile(r'(?<!\blong\s)(?<![\w])long\b(?!\s+(?:long|double|int)\b)(?=\s*[\w\*&\)\(,;>])'), 'int'),
    # Templates declared inside an extern "C" block: MWCC gives them C++ linkage anyway; say so explicitly.
    # PowerPC srw gives 0 for a shift count of 32; x86 masks the count, so `0xFFFFFFFF >> (32 - n)` with n == 0
    # keeps all ones on the host. Bink's bit readers build masks this way with n == 0 for one-symbol trees.
    ('ppc-srw-mask', re.compile(r'(0xFFFFFFFFU?|\bones)\s*>>\s*\(32\s*-\s*((?:[^()]|\([^()]*\))+)\)'),
     r'port_srw(\1, 32 - (\2))'),
    # File-scope `inline` functions (the game's weak helpers: Dot, Max, ...): MWCC kept an out-of-line weak copy that
    # units declaring the function (without the body) call; clang emits an inline function only where it is not
    # inlined, so those references went unresolved (stubbed: a float stub returns NaN). `used` emits every copy as
    # COMDAT; the linker keeps one.
    ('inline-emit', re.compile(r'(?m)^(\s*)inline(\s+)(?!namespace)'), r'\1inline __attribute__((used))\2'),
    ('template-cxx-linkage', re.compile(r'(?m)^template\s*<'), 'extern "C++" template <'),
]


NL = chr(10)

# Hand-written MWCC-layout vtables (uiapi, widgets, collisionsound): {0, 0 (rtti, offset), dtor(this, flag), virt1,
# ...} as `void*` or `u32` arrays. Clang code calls them with the Itanium layout: the vptr points at slot 0 and a
# destructor has two slots (complete, deleting). Rewrite each table to {0, 0, D1, D0, virt1, ...} with D1/D0 thunks
# calling the MWCC destructor with flag 0/1, and make every `= lbl_X;` vptr store point at slot 0 (lbl_X + 2).
# Entries are free functions taking `this` first, which works because game methods are cdecl on the host (the
# Cygwin triple in trial.py). Every table found so far starts with a destructor (this, flag); checked by hand.
# The two header words may be written with casts ({(void*)0, (void*)0, ...}).
MWCC_VTABLE = re.compile(r'(?m)^(void\*|u32)\s+(lbl_[0-9A-F]{8})\[(\d+)\]\s*=\s*\{\s*(?:\((?:void\*|u32)\)\s*)?0\s*,\s*'
                         r'(?:\((?:void\*|u32)\)\s*)?0\s*,\s*'
                         r'\((?:void\*|u32)\)\s*(\w+)\s*(,.*)?\};')


# Bitfields: MWCC (big-endian PowerPC) allocates them from the most significant bit of each storage unit, x86
# compilers from the least significant. Code that also reads the unit as a whole word (sfline writes a path's
# `count << 16 | flags`, aiobst reads `count : 16`), colour words split into `a : 8, rgb : 24`, and bitfields over
# file data (Bink's audio flags) then see other bits. Emulate MWCC: within each storage unit declare the fields in
# reverse order, with an unnamed pad first when the unit is not full, so every field keeps its MWCC bit position.
BF_LINE = re.compile(r'^(\s*)((?:unsigned\s+int|signed\s+int|unsigned|signed|u8|s8|u16|s16|u32|s32|int|BOOL))\s+(\w*)\s*:\s*(\d+)\s*;(.*)$')
BF_WIDTH = {'u8': 8, 's8': 8, 'u16': 16, 's16': 16}


def msb_bitfields(t):
    lines = t.split('\n')
    out, i, n = [], 0, 0
    while i < len(lines):
        m = BF_LINE.match(lines[i])
        if not m:
            out.append(lines[i])
            i += 1
            continue
        ty = m.group(2)
        run = []
        while i < len(lines):
            m2 = BF_LINE.match(lines[i])
            if not m2 or m2.group(2) != ty:
                break
            run.append(m2)
            i += 1
        width = BF_WIDTH.get(ty, 32)
        units, cur, used = [], [], 0
        for f in run:
            w = int(f.group(4))
            if used + w > width or w == 0:
                units.append((cur, used))
                cur, used = [], 0
            if w:
                cur.append(f)
                used += w
        units.append((cur, used))
        for fields, used in units:
            if not fields:
                continue
            ind = fields[0].group(1)
            if used < width:
                out.append('%s%s : %d;  // port: MWCC bitfield order (unused low bits)' % (ind, ty, width - used))
            for f in reversed(fields):
                out.append('%s%s %s : %s;%s' % (f.group(1), ty, f.group(3), f.group(4), f.group(5)))
            n += 1
    return '\n'.join(out), n


# MWCC tables whose destructor is NOT at slot 0x08: name -> byte offset of the destructor slot.
MWCC_DTOR_AT = {'lbl_80332F40': 0x80}  # replay Recorder (playcar): SetGear at 0x08, destructor last


def mwcc_vtables(t):
    names = []

    def rep(m):
        ty, name, n, dtor, rest = m.groups()
        names.append(name)
        cast = '(%s)' % ty
        if name in MWCC_DTOR_AT:
            # The destructor is not at 0x08 (e.g. the replay Recorder: SetGear at 0x08, destructor last at 0x80):
            # the host pair goes at that slot instead.
            items = [s.strip() for s in ('%s%s%s' % (cast, dtor, rest or '')).split(',')]
            k = MWCC_DTOR_AT[name] // 4 - 2
            fn = re.sub(r'^\([^)]*\)\s*', '', items[k])
            items[k:k + 1] = ['%s%s_D1' % (cast, name), '%s%s_D0' % (cast, name)]
            return ('static void %s_D1(void* t) { ((void* (*)(void*, int))%s)(t, 0); }\n'
                    'static void %s_D0(void* t) { ((void* (*)(void*, int))%s)(t, 1); }\n'
                    '%s %s[%d] = {0, 0, %s};' % (name, fn, name, fn, ty, name, int(n) + 1, ', '.join(items)))
        return ('static void %s_D1(void* t) { ((void* (*)(void*, int))%s)(t, 0); }\n'
                'static void %s_D0(void* t) { ((void* (*)(void*, int))%s)(t, 1); }\n'
                '%s %s[%d] = {0, 0, %s%s_D1, %s%s_D0%s};' % (
                    name, dtor, name, dtor, ty, name, int(n) + 1, cast, name, cast, name, rest or ''))
    t = MWCC_VTABLE.sub(rep, t)
    for name in names:
        t = re.sub(r'=\s*%s\s*;' % name, '= %s + 2;' % name, t)
    return t, len(names)

# Byte-reversing helpers that turn the disc's little-endian data into the Wii's big-endian order. On an x86 host
# that data is already native, so their bodies become empty (checked: each only reverses bytes in place).
LE_SWAP_HELPERS = ['Swap4', 'Swap4Loop', 'Swap32', 'Swap16', 'SwapBytes', 'gxSwap32', 'SwfSwap32', 'SwfSwap16', 'SwapL',
                   'PSwap16', 'PSwap32', 'SwapN', 'GSwap32', 'Swap2', 'SwapUV', 'SwapName']
SWAP_DEF = re.compile(r'static inline void (%s)\(([^)]*)\)\s*\{' % '|'.join(LE_SWAP_HELPERS))


def neutralise_swaps(t):
    out, pos, n = [], 0, 0
    for m in SWAP_DEF.finditer(t):
        if m.start() < pos:
            continue
        depth, i = 1, m.end()
        while depth:
            depth += {'{': 1, '}': -1}.get(t[i], 0)
            i += 1
        out.append(t[pos:m.end()])
        out.append(' /* port: little-endian host, disc data already native */ (void)' +
                   m.group(2).split()[-1].lstrip('*&') + '; }')
        pos = i
        n += 1
    out.append(t[pos:])
    return ''.join(out), n


# `lbl_X + N` past the end of a byte array the unit defines (inputbind's string pool: "Joytick Up" + 0x126C...):
# on the Wii it lands on later strings because the linker keeps retail .data order; a host compiler places each
# array on its own. Redirect such accesses to the retail address, where runtime/dolload.cpp loads the DOL's data.
_LABEL_SIZE = None


def label_sizes():
    global _LABEL_SIZE
    if _LABEL_SIZE is None:
        _LABEL_SIZE = {}
        for l in open(os.path.join(ET, 'config', 'REXE01', 'symbols.txt'), encoding='utf-8'):
            m = re.match(r'^(lbl_([0-9A-F]{8})) = \S+; // type:object size:0x([0-9A-F]+)', l)
            if m:
                _LABEL_SIZE[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16))
    return _LABEL_SIZE


BYTE_ARRAY_DEF = re.compile(r'(?m)^(?:static\s+)?(?:const\s+)?(char|u8|s8|unsigned char)\s+(lbl_[0-9A-F]{8})\s*\[[^\]]*\]\s*=')


def past_label(t):
    n = 0
    for ty, lab in BYTE_ARRAY_DEF.findall(t):
        if lab not in label_sizes():
            continue
        addr, size = label_sizes()[lab]

        def rep(m):
            nonlocal n
            off = int(m.group(1), 0)
            if off < size:
                return m.group(0)
            n += 1
            return '((%s *)0x%08X + 0x%X)' % (ty, addr, off)
        t = re.sub(r'\b%s\s*\+\s*(0x[0-9A-Fa-f]+|\d+)\b' % lab, rep, t)
    return t, n


# Byte tables the code reads as big-endian words (Bink's dequantisation tables: u8 initialisers, read as s32).
# Their initialisers are rewritten with each group of `n` bytes reversed, so the words read right on x86.
WORD_TABLES = {}  # Bink's dequant tables: the decomp declares them s32 now (af0384038)


def word_tables(fn, t):
    n = 0
    for lab, w in WORD_TABLES.get(fn, []):
        m = re.search(r'(?m)^(const\s+u8\s+%s\s*\[\d*\]\s*=\s*\{)([^}]*)\}' % lab, t)
        if not m:
            sys.exit('word table %s not found in %s' % (lab, fn))
        vals = [v.strip() for v in m.group(2).split(',') if v.strip()]
        if len(vals) % w:
            sys.exit('word table %s: %d bytes, not a multiple of %d' % (lab, len(vals), w))
        out = []
        for i in range(0, len(vals), w):
            out += vals[i:i + w][::-1]
        body = ''.join(('\n    ' if i % 16 == 0 else ' ') + v + ',' for i, v in enumerate(out))
        t = t[:m.start(2)] + '  /* port: words in host byte order */' + body + '\n' + t[m.end(2):]
        n += 1
    return t, n


# Declarations that order int and float parameters differently from the C definition (tools/gametrial/regorder.py):
# equivalent on PowerPC (separate register files), wrong argument slots on x86. Each becomes a static inline adapter
# with the caller's own parameter list, forwarding to the definition's symbol with the arguments in its order.
TYPE_WORDS = {'int', 'unsigned', 'signed', 'char', 'short', 'long', 'float', 'double', 'void', 'bool', 'BOOL',
              'u8', 'u16', 'u32', 'u64', 's8', 's16', 's32', 's64', 'f32', 'f64', 'const', 'volatile'}


def split_param(a, i):
    """(type, name) of one parameter; unnamed ones get a generated name."""
    a = a.strip()
    m = re.match(r'^(.*?[\s\*&])([A-Za-z_]\w*)\s*$', a)
    if m and m.group(2) not in TYPE_WORDS and re.search(r'[A-Za-z_]', m.group(1)):
        return m.group(1).strip(), m.group(2)
    return a, 'p%d' % i


def regorder_adapters(texts):
    sys.path.insert(0, HERE)
    import regorder
    found = regorder.scan(texts)
    by_file = {}
    for d in found:
        by_file.setdefault(d['file'], []).append(d)
    n = 0
    for f, ds in by_file.items():
        t = texts[f]
        done = set()
        for d in sorted(ds, key=lambda x: -x['start']):  # from the end, so earlier offsets stay valid
            first = min(x['start'] for x in ds if x['name'] == d['name'])
            if d['start'] != first:
                t = t[:d['start']] + '/* port: regorder adapter for %s is above */' % d['name'] + t[d['end']:]
                continue
            variadic = d['cls'].endswith('.')
            if variadic and f.endswith('.c'):
                continue  # forwarding varargs needs C++ (a variadic template); no C unit has one today
            ps = [split_param(a, i) for i, a in enumerate(d['params'].split(','))
                  if a.strip() != '...']
            defps = [a.strip() for a in d['defparams'].split(',') if a.strip() != '...']
            ints = [i for i, c in enumerate(d['cls']) if c == 'i']
            flts = [i for i, c in enumerate(d['cls']) if c == 'f']
            order, real = [], []
            for j, c in enumerate(d['defcls'].rstrip('.')):
                k = (ints if c == 'i' else flts).pop(0)
                order.append(ps[k][1])
                if c == 'f':
                    dt = re.sub(r'\b(const|volatile)\b', '', defps[j]).split()[0]
                    real.append({'float': 'float', 'f32': 'float', 'double': 'double', 'f64': 'double'}.get(dt, ps[k][0]))
                else:
                    real.append(ps[k][0])
            ret = re.sub(r'\b(extern|static|inline)\b|"C"', ' ', d['prefix']).strip()
            cxx = not f.endswith('.c')
            if variadic:
                # The fixed arguments in the definition's order, the variable ones forwarded as they come.
                body = ('extern "C" %s __port_ro_%s(%s, ...) __asm__("_%s");\n'
                        'extern "C++" { template <class... PortVa> static inline %s %s(%s, PortVa... va) '
                        '{ %s__port_ro_%s(%s, va...); } }' % (
                            ret, d['name'], ', '.join(real), d['name'],
                            ret, d['name'], ', '.join('%s %s' % p for p in ps),
                            '' if ret == 'void' else 'return ', d['name'], ', '.join(order)))
                t = t[:d['start']] + '/* port: regorder adapter, variadic (int/float order differs from %s) */\n' % (
                    d['deffile']) + body + t[d['end']:]
                n += 1
                continue
            body = ('%s%s __port_ro_%s(%s) __asm__("_%s");\n'
                    'static inline %s %s(%s) { %s__port_ro_%s(%s); }' % (
                        'extern "C" ' if cxx else '', ret, d['name'], ', '.join(real), d['name'],
                        ret, d['name'], ', '.join('%s %s' % p for p in ps),
                        '' if ret == 'void' else 'return ', d['name'], ', '.join(order)))
            t = t[:d['start']] + '/* port: regorder adapter (int/float parameter order differs from %s) */\n' % (
                d['deffile']) + body + t[d['end']:]
            n += 1
        texts[f] = t
    return texts, n


def load_patches():
    pats = []
    for pf in sorted(p for p in glob.glob(os.path.join(PORT, 'patches', '*.patch.txt'))
                     if not os.path.basename(p).startswith('hbm-')):  # hbm-*: tools/gametrial/hbmbuild.py
        t = open(pf, encoding='utf-8').read()
        for blk in t.split('@@@ ')[1:]:
            head, body = blk.split(NL, 1)
            fn, why = [x.strip() for x in head.split('|', 1)]
            old = body.split('<<<' + NL, 1)[1].split(NL + '===' + NL, 1)[0]
            new = body.split(NL + '===' + NL, 1)[1].split(NL + '>>>', 1)[0]
            pats.append((os.path.basename(pf), fn, why, old, new))
    return pats


def main():
    patches = load_patches()
    applied = 0
    failed = []
    linkfix = {}
    lf = os.path.join(HERE, 'linkage_fix.txt')
    if os.path.exists(lf):
        for l in open(lf):
            p = l.rstrip('\n').split('\t')
            if len(p) == 3:
                linkfix.setdefault(p[0], []).append((int(p[1]), p[2]))
    counts = {r[0]: 0 for r in RULES}
    files = sorted(glob.glob(os.path.join(ET, 'src', 'game', '*.c*')))
    files += sorted(glob.glob(os.path.join(ET, 'include', '**', '*.h'), recursive=True))
    outputs = []
    for src in (f for f in files if os.path.isfile(f)):
        t = open(src, encoding='utf-8', errors='surrogateescape').read()
        for name, rx, rep in RULES:
            t, n = rx.subn(rep, t)
            counts[name] += n
        if src.replace(os.sep, '/').find('/src/game/') >= 0:
            t, n = mwcc_vtables(t)
            counts['mwcc-vtables'] = counts.get('mwcc-vtables', 0) + n
        if src.replace(os.sep, '/').find('/src/game/') >= 0:
            t, n = past_label(t)
            counts['past-label-retail'] = counts.get('past-label-retail', 0) + n
            t, n = word_tables(os.path.basename(src), t)
            counts['word-tables'] = counts.get('word-tables', 0) + n
        t, n = neutralise_swaps(t)
        counts['le-swap-helpers'] = counts.get('le-swap-helpers', 0) + n
        if src.replace(os.sep, '/').find('/src/game/') >= 0:
            t, n = msb_bitfields(t)
            counts['msb-bitfields'] = counts.get('msb-bitfields', 0) + n
        for pf, fn, why, old, new in patches:
            if src.replace(os.sep, '/').endswith('/src/game/' + fn) or (
                    fn.startswith('include/') and src.replace(os.sep, '/').endswith('/' + fn)):
                n = t.count(old)
                if n != 1:
                    msg = 'patch %s for %s matched %d times (want 1): %s' % (pf, fn, n, why)
                    if os.environ.get('PREP_ALLFAIL'):  # pin bumps: list every stale patch, then stop
                        print(msg)
                        failed.append(msg)
                        continue
                    sys.exit(msg)
                t = t.replace(old, new)
                applied += 1
        # Linkage fixes (linkagefix.py): the later C++-scope declaration of a name an extern "C" block declared.
        base = os.path.basename(src)
        if base in linkfix and src.replace(os.sep, '/').find('/src/game/') >= 0:
            lines = t.split(NL)
            for ln, name in linkfix[base]:
                i = ln - 1
                if 0 <= i < len(lines) and re.search(r'\b%s\b' % name, lines[i]) and \
                        not lines[i].lstrip().startswith('extern "C"') and not lines[i].lstrip().startswith('static'):
                    lines[i] = re.sub(r'^(\s*)(?:extern\s+)?', r'\1extern "C" ', lines[i], count=1)
                    counts['linkage-fix'] = counts.get('linkage-fix', 0) + 1
            t = NL.join(lines)
        srcroot = os.path.join(ET, 'src')
        rel = os.path.relpath(src, srcroot if src.startswith(srcroot + os.sep) else ET)
        outputs.append((os.path.join(OUT, rel), t))
    if failed:
        sys.exit("%d stale patches" % len(failed))
    # Integer/float parameter order (regorder.py): needs every unit, so it runs once all are prepared.
    game = {os.path.basename(d): i for i, (d, _) in enumerate(outputs) if os.sep + 'game' + os.sep in d}
    if os.environ.get('NICECRASH_NOREGORDER'):  # bisecting aid
        texts, n = {b: outputs[i][1] for b, i in game.items()}, 0
    else:
        texts, n = regorder_adapters({b: outputs[i][1] for b, i in game.items()})
    counts['regorder-adapters'] = n
    # Structs passed by value where the definition takes a pointer (byvalref.py): the PPC ABI passes aggregates
    # by address, x86 copies them; declare those parameters `const T&`.
    import byvalref
    incs = [t for d, t in outputs if os.sep + 'include' + os.sep in d]
    fixes, _ = byvalref.scan(texts, incs)
    texts, n, skipped = byvalref.apply(texts, fixes)
    counts['byval-to-ref'] = n
    for name, f in skipped:
        print('prep: byvalref: %s in %s (C unit) passes a struct by value to a pointer parameter: fix by hand' % (name, f))
    # Declarations that widen a narrow return type (retwidth.py): MWCC extends u8/u16 returns in r3, x86 leaves
    # garbage above AL/AX; declare the definition's type.
    import retwidth
    texts, n = retwidth.apply(texts, retwidth.scan(texts))
    counts['ret-narrow'] = n
    for b, i in game.items():
        outputs[i] = (outputs[i][0], texts[b])
    for dst, t in outputs:
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        old = open(dst, encoding='utf-8', errors='surrogateescape').read() if os.path.exists(dst) else None
        if old != t:
            open(dst, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(t)
    # Sources the decomp no longer has (merged or deleted units, e.g. unk_80243460.c into mroll): remove their
    # stale copies, or the build keeps compiling them.
    wanted = {os.path.normcase(os.path.abspath(d)) for d, _ in outputs}
    stale = [f for f in glob.glob(os.path.join(OUT, '**', '*'), recursive=True)
             if os.path.isfile(f) and f.endswith(('.c', '.cpp', '.h', '.hpp', '.inc'))
             and os.path.normcase(os.path.abspath(f)) not in wanted]
    for f in stale:
        os.remove(f)
    counts['stale-removed'] = len(stale)
    if applied != len(patches):
        sys.exit('only %d of %d patches applied' % (applied, len(patches)))
    print('%-24s %d' % ('patches', applied))
    for k, v in counts.items():
        print('%-24s %d' % (k, v))
    dumpdata()


def dumpdata():
    """Data tables the decomp no longer carries (`#include "dumpdata/<label>.inc"`): generated from the player's
    own main.dol by the decomp's tools/datagen.py into build/dumpinc/dumpdata (on trial.py's include path)."""
    tool = os.path.join(decomp.root(), 'tools', 'datagen.py')
    if not os.path.exists(tool):
        return
    dol = os.environ.get('NICECRASH_DOL')
    if not dol:
        ini = os.path.join(os.environ.get('LOCALAPPDATA', ''), 'NiceCrash', 'dump.ini')
        if os.path.exists(ini):
            for line in open(ini, encoding='utf-8'):
                if line.startswith('dol='):
                    dol = line[4:].strip()
    if not dol or not os.path.exists(dol):
        sys.exit("prep: the dumpdata tables need the player's main.dol (NICECRASH_DOL or dump.ini)")
    out = os.path.join(PORT, 'build', 'dumpinc', 'dumpdata')
    r = subprocess.run([sys.executable, '-I', tool, '--version', 'REXE01', '--dol', dol,
                        '--symbols', os.path.join(decomp.root(), 'config', 'REXE01', 'symbols.txt'), '--out', out],
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit('prep: datagen failed:\n' + r.stdout[-2000:] + r.stderr[-2000:])
    print('%-24s %d' % ('dumpdata tables', len(os.listdir(out))))


if __name__ == '__main__':
    main()
