# Symbolise catchrun output: every 8-hex-digit address after "eip", "ret", "stack +off" or "at" becomes
# symbol+offset from build/NiceCrashGame.map (lld-link "Publics by Value", Rva+Base column).
# Usage: python -I tools/dbg/symbolize.py <log> [map]
import bisect, re, sys
log = sys.argv[1]
mp = sys.argv[2] if len(sys.argv) > 2 else "build/NiceCrashGame.map"
syms = []
pub = False
for line in open(mp, encoding="utf-8", errors="replace"):
    if "Publics by Value" in line:
        pub = True
        continue
    if not pub:
        continue
    p = line.split()
    if len(p) >= 3 and ":" in p[0]:
        try:
            a = int(p[2], 16)
        except ValueError:
            continue
        if a >= 0x401000:
            syms.append((a, p[1]))
syms.sort()
keys = [a for a, _ in syms]
def name(a):
    i = bisect.bisect_right(keys, a) - 1
    if i < 0: return "?"
    return f"{syms[i][1]}+{a - syms[i][0]:x}"
for line in open(log, encoding="utf-8", errors="replace"):
    if "[catchrun]" not in line: continue
    line = line.rstrip()
    m = re.search(r"(eip|ret|at) ([0-9a-f]{8})|stack \+[0-9a-f]+ ([0-9a-f]{8})", line)
    a = m and int(m.group(2) or m.group(3), 16)
    print(line + (f"   {name(a)}" if a and 0x401000 <= a < 0x2000000 else ""))
