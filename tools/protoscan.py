import glob, os, re, sys, collections
root = sys.argv[1]
DECL = re.compile(r'(?<![\w.>])(fn_[0-9A-F]{8}|lbl_[0-9A-F]{8})\s*\(([^()]*)\)\s*[;{]')
def kinds(params):
    params = re.sub(r'/\*.*?\*/', '', params).strip()
    if params in ('', 'void'):
        return ()
    out = []
    for p in params.split(','):
        p = p.strip()
        if p == '...':
            out.append('...')
        elif '*' in p or '[' in p or re.search(r'\b(\w+Ptr|PFN\w*)\b', p):
            out.append('P')
        elif re.search(r'\b(f32|f64|float|double)\b', p):
            out.append('F')
        else:
            out.append('I')
    return tuple(out)
seen = collections.defaultdict(list)
for f in sorted(glob.glob(os.path.join(root, 'game', '*.c*'))):
    t = open(f, encoding='utf-8', errors='replace').read()
    for m in DECL.finditer(t):
        line = t.count('\n', 0, m.start()) + 1
        # skip calls: a declaration/definition has a type word before the name
        pre = t[max(0, m.start() - 80):m.start()]
        if not re.search(r'(\b(void|int|s32|u32|u8|s8|u16|s16|f32|f64|bool|BOOL|char|\w+\*|\w+)\s*\**\s*)$', pre) or re.search(r'(=|return|\(|,)\s*$', pre):
            continue
        k = kinds(m.group(2))
        seen[m.group(1)].append((k, os.path.basename(f), line, m.group(2).strip()))
n = 0
for name, ds in sorted(seen.items()):
    ks = {d[0] for d in ds if '...' not in d[0]}
    if len(ks) < 2:
        continue
    lens = {len(k) for k in ks}
    pos_conflict = False
    for i in range(max(lens)):
        vals = {k[i] for k in ks if i < len(k)}
        if 'P' in vals and ('I' in vals or 'F' in vals):
            pos_conflict = True
    if not pos_conflict:
        continue
    n += 1
    print(name)
    for k, f, l, p in ds:
        print('   %-22s %5d  (%s)' % (f, l, p))
print('\n%d functions declared with a pointer in one unit and a number in another at the same position' % n, file=sys.stderr)
