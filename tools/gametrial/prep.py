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
]


NL = chr(10)

# Byte-reversing helpers that turn the disc's little-endian data into the Wii's big-endian order. On an x86 host
# that data is already native, so their bodies become empty (checked: each only reverses bytes in place).
LE_SWAP_HELPERS = ['Swap4', 'Swap4Loop', 'Swap32', 'Swap16', 'SwapBytes', 'gxSwap32', 'SwfSwap32', 'SwfSwap16', 'SwapL']
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
    counts = {r[0]: 0 for r in RULES}
    files = sorted(glob.glob(os.path.join(ET, 'src', 'game', '*.c*')))
    files += sorted(glob.glob(os.path.join(ET, 'include', '**', '*.h'), recursive=True))
    for src in (f for f in files if os.path.isfile(f)):
        t = open(src, encoding='utf-8', errors='surrogateescape').read()
        for name, rx, rep in RULES:
            t, n = rx.subn(rep, t)
            counts[name] += n
        t, n = neutralise_swaps(t)
        counts['le-swap-helpers'] = counts.get('le-swap-helpers', 0) + n
        for pf, fn, why, old, new in patches:
            if src.replace(os.sep, '/').endswith('/src/game/' + fn):
                n = t.count(old)
                if n != 1:
                    sys.exit('patch %s for %s matched %d times (want 1): %s' % (pf, fn, n, why))
                t = t.replace(old, new)
                applied += 1
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
