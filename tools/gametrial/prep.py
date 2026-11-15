"""Port source prep: copy excite-truck/src/game into port/build/gamesrc with MECHANICAL dialect rewrites only
(MWCC accepts, clang rejects, meaning is identical). Never touches the decomp. Anything that changes meaning
goes to docs/SOURCE_NOTES.md as a write-up for the decomp sessions instead.

Each rule: (name, regex, replacement). Counts per rule are printed so we can see what the port depends on.
"""
import glob, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.normpath(os.path.join(HERE, '..', '..'))
ET = os.path.normpath(os.path.join(PORT, '..', 'excite-truck'))
OUT = os.path.join(PORT, 'build', 'gamesrc')

RULES = [
    # The GX write-gather pipe at 0xCC008000 -> the host FIFO object (shim/include/port_fifo.h).
    ('wgpipe-union', re.compile(r'\(\*\(volatile\s+WGPipe\s*\*\)\s*0xCC008000\)'), 'port_wgpipe'),
    ('wgpipe-u8', re.compile(r'\(\*\(volatile\s+u8\s*\*\)\s*0xCC008000\)'), 'port_wgpipe.uc'),
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
    ('template-cxx-linkage', re.compile(r'(?m)^template\s*<'), 'extern "C++" template <'),
]


NL = chr(10)

# Hand-written MWCC-layout vtables (uiapi, widgets, collisionsound): {0, 0 (rtti, offset), dtor(this, flag), virt1,
# ...} as `void*` or `u32` arrays. Clang code calls them with the Itanium layout: the vptr points at slot 0 and a
# destructor has two slots (complete, deleting). Rewrite each table to {0, 0, D1, D0, virt1, ...} with D1/D0 thunks
# calling the MWCC destructor with flag 0/1, and make every `= lbl_X;` vptr store point at slot 0 (lbl_X + 2).
# Entries are free functions taking `this` first, which works because game methods are cdecl on the host (the
# Cygwin triple in trial.py). Every table found so far starts with a destructor (this, flag); checked by hand.
MWCC_VTABLE = re.compile(r'(?m)^(void\*|u32)\s+(lbl_[0-9A-F]{8})\[(\d+)\]\s*=\s*\{\s*0\s*,\s*0\s*,\s*'
                         r'\((?:void\*|u32)\)\s*(\w+)\s*(,.*)?\};')


def mwcc_vtables(t):
    names = []

    def rep(m):
        ty, name, n, dtor, rest = m.groups()
        names.append(name)
        cast = '(%s)' % ty
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


def load_patches():
    pats = []
    for pf in sorted(glob.glob(os.path.join(PORT, 'patches', '*.patch.txt'))):
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
        for pf, fn, why, old, new in patches:
            if src.replace(os.sep, '/').endswith('/src/game/' + fn):
                n = t.count(old)
                if n != 1:
                    sys.exit('patch %s for %s matched %d times (want 1): %s' % (pf, fn, n, why))
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
        dst = os.path.join(OUT, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        old = open(dst, encoding='utf-8', errors='surrogateescape').read() if os.path.exists(dst) else None
        if old != t:
            open(dst, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(t)
    if applied != len(patches):
        sys.exit('only %d of %d patches applied' % (applied, len(patches)))
    print('%-24s %d' % ('patches', applied))
    for k, v in counts.items():
        print('%-24s %d' % (k, v))


if __name__ == '__main__':
    main()
