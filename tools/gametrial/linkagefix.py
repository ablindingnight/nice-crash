"""Collect "declaration of X has a different language linkage" errors from the trial logs into linkage_fix.txt
(file, line, name). The error is reported at the later, C++-scope declaration of a name that an earlier
extern "C" block already declared; prep.py prefixes that line with `extern "C"` (what MWCC effectively did).
The prefix adds no lines, so the recorded line numbers stay valid. Entries accumulate across runs."""
import os, re
HERE = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(HERE, 'linkage_fix.txt')
rows = set()
if os.path.exists(out):
    rows = {tuple(l.rstrip('\n').split('\t')) for l in open(out) if l.count('\t') == 2}
rnote = re.compile(r"[\\/]([\w.]+\.(?:cpp|c)):(\d+):\d+: note: previous declaration is here")
rx = re.compile(r"[\\/]([\w.]+\.(?:cpp|c)):(\d+):\d+: error: declaration of '(\w+)' has a different language linkage")
for f in os.listdir(os.path.join(HERE, 'out')):
    if f.endswith('.log'):
        log = open(os.path.join(HERE, 'out', f), encoding='utf-8', errors='replace').read()
        for m in rx.finditer(log):
            rows.add(m.groups())
            # The later declaration already says extern "C" (a fix from an earlier run, or the source's own): the
            # C++-scope one is the EARLIER declaration the note points at, so prefix that one too.
            tail = log[m.end():m.end() + 600]
            src = re.match(r'\n\s*\d+ \| ([^\n]*)', tail)
            note = rnote.search(tail)
            if src and 'extern "C"' in src.group(1) and note and note.group(1) == m.group(1):
                rows.add((m.group(1), note.group(2), m.group(3)))
open(out, 'w').write(''.join('%s\t%s\t%s\n' % r for r in sorted(rows, key=lambda r: (r[0], int(r[1])))))
print('%d linkage fixes' % len(rows))
