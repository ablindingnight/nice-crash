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
    # The allocation operators must take size_t; on the Wii size_t is unsigned long, on x86 unsigned int.
    ('operator-new-size_t', re.compile(r'operator new(\[\])?\(\s*unsigned long\b'), r'operator new\1(size_t'),
]


def main():
    counts = {r[0]: 0 for r in RULES}
    files = sorted(glob.glob(os.path.join(ET, 'src', 'game', '*.c*')))
    files += sorted(glob.glob(os.path.join(ET, 'include', '**', '*.h'), recursive=True))
    for src in (f for f in files if os.path.isfile(f)):
        t = open(src, encoding='utf-8', errors='surrogateescape').read()
        for name, rx, rep in RULES:
            t, n = rx.subn(rep, t)
            counts[name] += n
        srcroot = os.path.join(ET, 'src')
        rel = os.path.relpath(src, srcroot if src.startswith(srcroot + os.sep) else ET)
        dst = os.path.join(OUT, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        old = open(dst, encoding='utf-8', errors='surrogateescape').read() if os.path.exists(dst) else None
        if old != t:
            open(dst, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(t)
    for k, v in counts.items():
        print('%-24s %d' % (k, v))


if __name__ == '__main__':
    main()
