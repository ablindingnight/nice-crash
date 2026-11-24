"""DUMP_DATA tables read from the player's main.dol at run time (no DOL bytes in the build).

The decomp writes a table whose bytes it doesn't carry as

    // DUMP_DATA lbl_802F8F08 u32
    static const u32 lbl_802F8F08[256] = {
    #include "dumpdata/lbl_802F8F08.inc"
    };

and its tools/datagen.py fills the .inc from main.dol at build time. The port instead replaces the definition with a
view of the table at its Wii address, where runtime/dolload.cpp loads the DOL's data sections:

    #define lbl_802F8F08 (*(const u32 (*)[256])0x802F8F08)

Every use (indexing, sizeof, decay to a pointer) means the same as before. Other units that name the table
(`extern` declarations) bind to the same address through link.py's asm-only data path. The multi-byte fields are
byte-swapped once at startup: their runs come from the marker's type (datagen's layout rules), written to
build/dumptables/<part>.tsv for link.py's port_dolfix_table. Pointer fields (cstr, cstrptr, tableptr) hold Wii
addresses, which the loaded data image makes valid host pointers, so they're swapped like any other word.
"""
import importlib.util, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.normpath(os.path.join(HERE, '..', '..'))
MANIFEST_DIR = os.path.join(PORT, 'build', 'dumptables')

_dg = None
_syms = None


def datagen():
    """The decomp's tools/datagen.py as a module (only its parsing and layout code is used; it never reads a DOL)."""
    global _dg, _syms
    if _dg is None:
        import decomp
        path = os.path.join(decomp.root(), 'tools', 'datagen.py')
        spec = importlib.util.spec_from_file_location('datagen', path)
        _dg = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(_dg)
        from pathlib import Path
        _syms = _dg.load_symbols(Path(os.path.join(decomp.root(), 'config', 'REXE01', 'symbols.txt')))
    return _dg, _syms


def swap_runs(type_, addr, size):
    """[(addr, length, elem)] for the multi-byte fields of a table of `size` bytes at `addr`."""
    dg, _ = datagen()
    lay, row = dg.layout(dg.parse_type(type_))
    runs = []
    for r in range(size // row):
        for name, count, off in lay:
            e = dg.SCALARS[name][0]
            if e == 1:
                continue
            a, n = addr + r * row + off, e * max(count, 1)
            if runs and runs[-1][2] == e and runs[-1][0] + runs[-1][1] == a:
                runs[-1][1] += n
            else:
                runs.append([a, n, e])
    return [tuple(x) for x in runs]


DEF_RE = r'(?P<decl>[^;{}=]*?)\s*=\s*\{\s*#include\s+"dumpdata/%s\.inc"\s*\}\s*;'


def rewrite(text, where=''):
    """(new text, [(label, addr, size, type)]). Raises SystemExit when a marked table can't be rewritten."""
    if 'dumpdata/' not in text:
        return text, []
    dg, syms = datagen()
    tables = []
    top_macros = []
    pos = 0
    out = text
    while True:
        mk = None
        for line_m in re.finditer(r'(?m)^[ \t]*//\s*DUMP_DATA\b.*$', out[pos:]):
            mk = line_m
            break
        if not mk:
            break
        start = pos + mk.start()
        mm = dg.MARKER_RE.match(mk.group(0))
        if not mm:
            raise SystemExit('dumptables: %s: unreadable marker %r' % (where, mk.group(0)))
        label, type_ = mm.group(1), mm.group(2)
        opts = dict(o.split('=', 1) for o in mm.group(3).split())
        marker = dg.Marker(label, type_, int(opts['addr'], 16) if 'addr' in opts else None, None, 0,
                           int(opts['count'], 0) if 'count' in opts else None, int(opts.get('group', '0'), 0))
        dm = re.compile(DEF_RE % re.escape(label)).match(out, pos + mk.end())
        if not dm:
            raise SystemExit('dumptables: %s: no `= { #include "dumpdata/%s.inc" };` after the marker' % (where, label))
        decl = dm.group('decl').strip()
        nm = re.match(r'^(?P<type>.*?)\b(?P<name>(?:\w+::)*\w+)\s*(?P<dims>(?:\[[^\]]*\]\s*)+)$', decl, re.S)
        if not nm:
            raise SystemExit('dumptables: %s: %s: declaration not understood: %r' % (where, label, decl))
        ctype = re.sub(r'\b(static|extern)\s+', '', nm.group('type')).strip()
        leaf = nm.group('name').split('::')[-1]
        addr, symsize = dg.resolve(marker, syms)
        size = dg.table_size(marker, symsize)
        row = dg.layout(dg.parse_type(type_))[1]
        if size % row:
            raise SystemExit('dumptables: %s: %s: size 0x%X is not a multiple of the row 0x%X' % (where, label, size, row))
        n = size // row
        dims = re.findall(r'\[([^\]]*)\]', nm.group('dims'))
        if marker.group:
            if not dims[0].strip():
                dims[0] = str(n // marker.group)
        elif not dims[0].strip() or ('::' in nm.group('name') and len(dims) == 1):
            dims[0] = str(n)  # a class static's bound (Util::MICROTONE_MAX) isn't visible outside the class
        dimtxt = ''.join('[%s]' % d.strip() for d in dims)
        macro = '#define %s (*(%s (*)%s)0x%08X)' % (leaf, ctype, dimtxt, addr)
        check = 'typedef char port_dd_size_%s[sizeof(%s) == 0x%X ? 1 : -1];' % (re.sub(r'\W', '_', label), leaf, size)
        note = '/* port: DUMP_DATA %s read from main.dol at run time (tools/gametrial/dumptables.py) */' % label
        # Used before its definition (a class static's methods come first): the macro goes after the includes.
        before = re.sub(r'(?m)^[ \t]*extern\b[^;{}()=]*\b%s\s*\[[^;=]*;' % re.escape(leaf), '', out[:start])
        early = re.search(r'(?<![\w.>])%s\b' % re.escape(leaf), re.sub(r'//[^\n]*|/\*.*?\*/', '', before, flags=re.S))
        if early:
            top_macros.append((macro, leaf))
            repl = '\n%s\n%s' % (note, check)
        else:
            repl = '\n%s\n%s\n%s' % (note, macro, check)
        out = out[:dm.start('decl')] + repl + out[dm.end():]
        pos = dm.start('decl') + len(repl)
        tables.append((label, addr, size, type_, leaf))
    # Other declarations of the tables in this file (forward `extern`s): the macro stands for them.
    for label, addr, size, type_, leaf in tables:
        out = re.sub(r'(?m)^[ \t]*extern\b[^;{}()=#]*\b%s\s*\[[^;=]*;[ \t]*\n?' % re.escape(leaf), '', out)
    if top_macros:
        incs = list(re.finditer(r'(?m)^[ \t]*#[ \t]*include\b[^\n]*\n', out))
        if not incs:
            raise SystemExit('dumptables: %s: no #include to put the early macros after' % where)
        uses = [re.search(r'(?<![\w.>])%s\b' % re.escape(lf), out) for _, lf in top_macros]
        first_use = min(u.start() for u in uses if u) if any(uses) else len(out)
        incs = [i for i in incs if i.end() <= first_use] or incs[:1]
        at = incs[-1].end()
        out = out[:at] + '\n'.join(m for m, _ in top_macros) + '\n' + out[at:]
    if 'dumpdata/' in out:
        raise SystemExit('dumptables: %s: a dumpdata include is left after the rewrite' % where)
    return out, [(l, a, s, t) for l, a, s, t, _ in tables]


GEN_SIZES = {'s8': 1, 'u8': 1, 'char': 1, 's16': 2, 'u16': 2, 's32': 4, 'u32': 4, 'f32': 4, 'f64': 8,
             'HuffTable': 4}  # sdk_mp3's HuffTable is one pointer (u16** nodes)
GEN_DEF = re.compile(r'(?ms)^(?P<q>(?:MP3_STATIC\s+)?(?:const\s+)?)(?P<t>\w+)\s*(?P<p>\*?)\s*(?P<p2>(?:const\s*)?\**)\s*'
                     r'(?P<name>lbl_(?P<a>[0-9A-F]{8}))\s*(?P<dims>(?:\[\d+\]\s*)+)=\s*\{.*?\};[ \t]*$')


def rewrite_generated(text, where=''):
    """A generated table file (the decomp's tools/mp3/gentables.py output: `[MP3_STATIC] [const] T lbl_X[..] = {..};`)
    -> the same views at the Wii addresses, without the bytes. Returns (text, [(label, addr, size, runs)])."""
    tables = []

    def one(m):
        t, ptr = m.group('t'), bool(m.group('p') or m.group('p2'))
        e = 4 if ptr else GEN_SIZES.get(t)
        if e is None:
            raise SystemExit('dumptables: %s: %s: element type %s not understood' % (where, m.group('name'), t))
        dims = [int(d) for d in re.findall(r'\[(\d+)\]', m.group('dims'))]
        n = 1
        for d in dims:
            n *= d
        addr, size = int(m.group('a'), 16), n * e
        ctype = (('const ' if 'const' in m.group('q') else '') + t + ('*' if ptr else '')).strip()
        tables.append((m.group('name'), addr, size, [(addr, size, e)] if e > 1 else []))
        return ('#define %s (*(%s (*)%s)0x%08X)  /* port: read from main.dol at run time (dumptables.py) */' %
                (m.group('name'), ctype, ''.join('[%d]' % d for d in dims), addr))
    out = GEN_DEF.sub(one, text)
    left = [l for l in out.splitlines() if re.match(r'\s*-?(0x)?[0-9]', l)]
    if left:
        raise SystemExit('dumptables: %s: table bytes left after the rewrite: %r' % (where, left[0][:80]))
    return out, tables


def write_runs(part, tables):
    """build/dumptables/<part>.tsv from (label, addr, size, runs) rows."""
    os.makedirs(MANIFEST_DIR, exist_ok=True)
    lines = ['%s\t%08X\t%X\t-\t%s' % (l, a, s, ' '.join('%08X:%X:%d' % r for r in runs))
             for l, a, s, runs in sorted(tables, key=lambda t: t[1])]
    path = os.path.join(MANIFEST_DIR, part + '.tsv')
    t = '\n'.join(lines) + '\n'
    if not os.path.exists(path) or open(path, encoding='utf-8').read() != t:
        open(path, 'w', encoding='utf-8', newline='\n').write(t)


def write_manifest(part, tables):
    """build/dumptables/<part>.tsv: label, address, size, type, then the swap runs (addr:len:elem)."""
    os.makedirs(MANIFEST_DIR, exist_ok=True)
    lines = []
    for label, addr, size, type_ in sorted(tables, key=lambda t: t[1]):
        runs = ' '.join('%08X:%X:%d' % r for r in swap_runs(type_, addr, size))
        lines.append('%s\t%08X\t%X\t%s\t%s' % (label, addr, size, type_, runs))
    path = os.path.join(MANIFEST_DIR, part + '.tsv')
    t = '\n'.join(lines) + '\n'
    if not os.path.exists(path) or open(path, encoding='utf-8').read() != t:
        open(path, 'w', encoding='utf-8', newline='\n').write(t)


def read_manifests():
    """[(addr, size, [(addr, len, elem)])] from every build/dumptables/*.tsv."""
    out = []
    if not os.path.isdir(MANIFEST_DIR):
        return out
    for f in sorted(os.listdir(MANIFEST_DIR)):
        if not f.endswith('.tsv'):
            continue
        for line in open(os.path.join(MANIFEST_DIR, f), encoding='utf-8'):
            p = line.rstrip('\n').split('\t')
            if len(p) < 4:
                continue
            runs = [tuple(int(x, 16) if i < 2 else int(x) for i, x in enumerate(r.split(':')))
                    for r in (p[4].split() if len(p) > 4 else [])]
            out.append((int(p[1], 16), int(p[2], 16), runs))
    return out
