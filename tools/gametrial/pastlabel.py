"""Finds `lbl_X + 0xN` / `lbl_X)[N]`-style accesses whose offset reaches past the label's retail size
(symbols.txt). They rely on the retail data order, which a host compiler does not keep for source-defined arrays.
usage: pastlabel.py [file...]   (default: every prepared game unit)"""
import glob, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import decomp  # noqa: E402
SYMS = os.path.join(decomp.root(), 'config', 'REXE01', 'symbols.txt')
size = {}
for l in open(SYMS, encoding='utf-8'):
    m = re.match(r'^(lbl_[0-9A-F]{8}) = \S+; // type:object size:0x([0-9A-F]+)', l)
    if m:
        size[m.group(1)] = int(m.group(2), 16)
ACC = re.compile(r'\b(lbl_[0-9A-F]{8})\)?\s*\+\s*(0x[0-9A-Fa-f]+|\d+)\b')
files = sys.argv[1:] or glob.glob(os.path.join(PORT, 'build', 'gamesrc', 'game', '*.c*'))
for f in sorted(files):
    t = open(f, encoding='utf-8', errors='replace').read()
    defined = set(re.findall(r'(?m)^(?:static\s+)?(?:const\s+)?[\w ]+?\*?\s*(lbl_[0-9A-F]{8})\s*\[[^\]]*\]\s*=', t))
    for n, line in enumerate(t.split('\n'), 1):
        for m in ACC.finditer(line):
            lab, off = m.group(1), int(m.group(2), 0)
            if lab in size and off >= size[lab]:
                print('%s:%d %s+0x%X (size 0x%X)%s  %s' % (os.path.basename(f), n, lab, off, size[lab],
                      ' DEFINED-IN-SOURCE' if lab in defined else '', line.strip()[:110]))
