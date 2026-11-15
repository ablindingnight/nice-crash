"""First link of the game for 32-bit x86 Windows: every unit that compiles + port runtime + generated stubs.

  python link.py            compile (trial.py --obj), classify, generate build/stubs.s, link build/NiceCrashGame.exe

Stubs: every unresolved function becomes a label that calls port_unimpl("<name>"); every unresolved data label
becomes zeroed storage of its symbols.txt size (data owned by asm in the matching build; real values to come).
"""
import json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.normpath(os.path.join(HERE, '..', '..'))
B = os.path.join(PORT, 'build')
sys.path.insert(0, HERE)
import trial  # noqa: E402
from classify import itanium_leaf  # noqa: E402


def classify_leaf(d):
    leaf, scope = itanium_leaf(d)
    return leaf if not scope else None  # free functions only
import coffsyms  # noqa: E402


def zig(*args, **kw):
    return subprocess.run([trial.find_zig(), *args], cwd=PORT, capture_output=True, text=True, errors='replace',
                          env=dict(os.environ, ZIG_LOCAL_CACHE_DIR=os.path.join(B, 'zigcache')),
                          creationflags=0x08000000, **kw)


BUILTIN8 = set('xyd')  # long long, unsigned long long, double: 8 bytes on the stack


def _skip_name(s, i):
    """Skip one <name> or <type> component starting at s[i]; return the new index (simplified Itanium grammar)."""
    if s[i].isdigit():
        j = i
        while s[j].isdigit():
            j += 1
        i = j + int(s[i:j])
        if i < len(s) and s[i] == 'I':
            i = _skip_templ(s, i)
        return i
    if s[i] == 'N':
        i += 1
        while s[i] in 'KVr':
            i += 1
        while s[i] != 'E':
            if s[i] == 'S':
                i = _skip_subst(s, i)
            elif s[i] in 'CD':
                i += 2
            elif s[i] == 'I':
                i = _skip_templ(s, i)
            else:
                i = _skip_name(s, i)
        return i + 1
    if s[i] == 'S':
        return _skip_subst(s, i)
    raise ValueError


def _skip_subst(s, i):
    j = s.index('_', i)
    return j + 1


def _skip_templ(s, i):
    depth, i = 1, i + 1
    while depth:
        if s[i] == 'I':
            depth += 1
        elif s[i] == 'E':
            depth -= 1
        i += 1
    return i


def _param_sizes(s, i):
    """Stack sizes of the parameters in an Itanium mangled parameter list starting at s[i]."""
    sizes = []
    while i < len(s):
        c = s[i]
        if c in 'PR':  # pointer / reference to anything: 4 bytes
            i += 1
            while s[i] in 'KVr':
                i += 1
            if s[i] == 'F':  # function type: skip to its E
                depth = 1
                i += 1
                while depth:
                    depth += {'F': 1, 'E': -1}.get(s[i], 0)
                    i += 1
            elif s[i] in 'NS' or s[i].isdigit():
                i = _skip_name(s, i)
            elif s[i] in 'PR':
                inner = _param_sizes(s[:i] + s[i:], i)  # nested pointer: consume exactly one type
                return None if inner is None else sizes + [4] + inner[1:]
            else:
                i += 1
            sizes.append(4)
        elif c == 'K' or c == 'V':
            i += 1
        elif c == 'z':
            return None  # variadic: cdecl
        elif c == 'v':
            i += 1
        elif c.isalpha() and c.islower():
            sizes.append(8 if c in BUILTIN8 else 12 if c == 'e' else 4)
            i += 1
        elif c in 'NS' or c.isdigit():
            i = _skip_name(s, i)
            sizes.append(4)  # class by value: unknown size, assume one word
        else:
            return None
    return sizes


THISCALL = False  # game methods are cdecl on the host (port_prefix.h); stubs then need no callee pops


def thiscall_pop(sym):
    """Bytes a __thiscall member function pops (i386 MinGW: non-static member functions are thiscall, callee
    cleans the stack). None for anything else (free functions, C names, varargs)."""
    if not THISCALL or not sym.startswith('__ZN'):
        return None
    s = sym[3:]
    try:
        if s.startswith('N12_GLOBAL__N_1') or s.startswith('NSt'):
            return None
        end = _skip_name(s, 0)
        sizes = _param_sizes(s, end)
    except (ValueError, IndexError):
        return None
    if sizes is None:
        return None
    return sum(sizes)


def read_dol_data():
    """Big-endian word reader over the player's main.dol data sections (path from the importer's dump.ini), or None.
    Used at link time only to recognise retail vtables; nothing from the DOL is written into the build."""
    # Read dump.ini through cmd.exe: a Microsoft Store Python sees a virtualised %LOCALAPPDATA%.
    dolp = os.environ.get('NICECRASH_DOL')
    if not dolp:
        r = subprocess.run(['cmd.exe', '/c', 'type', '%LOCALAPPDATA%\\NiceCrash\\dump.ini'], capture_output=True,
                           text=True, errors='replace', creationflags=0x08000000)
        for l in r.stdout.splitlines():
            if l.startswith('dol='):
                dolp = l[4:].strip()
    if not dolp or not os.path.exists(dolp):
        return None
    b = open(dolp, 'rb').read()
    import struct
    secs = []
    for i in range(7, 18):
        off, addr, size = (struct.unpack_from('>I', b, x)[0] for x in (i * 4, 0x48 + i * 4, 0x90 + i * 4))
        if size:
            secs.append((addr, size, off))

    def word(a):
        for addr, size, off in secs:
            if addr <= a < addr + size - 3:
                return struct.unpack_from('>I', b, off + a - addr)[0]
        return None
    return word


ELEM_SIZES = {'u8': 1, 's8': 1, 'char': 1, 'unsigned char': 1, 'signed char': 1, 'bool': 1,
              'u16': 2, 's16': 2, 'short': 2, 'unsigned short': 2, 'wchar_t': 2,
              'u32': 4, 's32': 4, 'int': 4, 'unsigned int': 4, 'unsigned': 4, 'f32': 4, 'float': 4, 'BOOL': 4,
              'f64': 8, 'double': 8, 'u64': 8, 's64': 8, 'long long': 8, 'unsigned long long': 8}
DECL = re.compile(r'\bextern\s+(?:"C"\s+)?((?:(?:const|volatile|unsigned|signed)\s+)*[A-Za-z_]\w*)\s+'
                  r'(?:const\s+)?(\**)\s*(?:const\s+)?([A-Za-z_]\w*)\s*(\[[^\]]*\])?\s*;')


def decl_elem_sizes():
    """Element size of each extern data declaration in the prepared game source (1 = bytes, never swapped), so
    DOL-backed labels are byte-swapped per element. Conflicting declarations or struct types give None."""
    import glob
    out = {}
    for f in glob.glob(os.path.join(B, 'gamesrc', '**', '*.c*'), recursive=True) + \
            glob.glob(os.path.join(B, 'gamesrc', '**', '*.h'), recursive=True):
        if not os.path.isfile(f):  # e.g. the directory include/Runtime.PPCEABI.H
            continue
        t = open(f, encoding='utf-8', errors='replace').read()
        for m in DECL.finditer(t):
            ty = re.sub(r'\b(const|volatile)\s+', '', m.group(1)).strip()
            name = m.group(3)
            e = 4 if m.group(2) else ELEM_SIZES.get(ty)
            if name in out and out[name] != e:
                e = None
            out[name] = e
    return out


def rsp(cmd):
    """lld-link with its arguments in a response file (the command line outgrew Windows' 32K limit)."""
    path = os.path.join(B, 'link.rsp')
    with open(path, 'w', encoding='utf-8') as f:
        for a in cmd[2:]:
            f.write('"%s"\n' % a.replace('"', '\\"'))
    return cmd[:2] + ['@' + path]


def gas_name(s):
    return '"%s"' % s


def main():
    if '--nocompile' not in sys.argv:
        subprocess.run([sys.executable, '-I', os.path.join(HERE, 'trial.py'), '--jobs', '12', '--obj'], check=True)
    objdir = os.path.join(HERE, 'out', 'obj')
    good = []
    units = set(os.listdir(os.path.join(B, 'gamesrc', 'game')))  # objects of removed units stay out of the link
    for f in sorted(os.listdir(objdir)):
        log = os.path.join(HERE, 'out', f[:-2] + '.log')
        if f.endswith('.o') and f[:-2] in units and os.path.getsize(os.path.join(objdir, f)) and \
                not re.search(r' error: ', open(log, encoding='utf-8', errors='replace').read()):
            good.append(os.path.join(objdir, f))
    # The decomp's SDK compiled for the host (tools/gametrial/sdkbuild.py): GX today.
    if '--nocompile' not in sys.argv:
        subprocess.run([sys.executable, '-I', os.path.join(HERE, 'sdkbuild.py')], check=True, capture_output=True)
    sdkdir = os.path.join(B, 'sdkobj')
    good += sorted(os.path.join(sdkdir, f) for f in os.listdir(sdkdir) if f.endswith('.o')) if os.path.isdir(sdkdir) else []
    # Port runtime objects.
    rt = []
    import glob as _g
    srcs = ['runtime/port_main.cpp', 'runtime/unimpl.cpp'] + sorted(
        os.path.relpath(p, PORT).replace(os.sep, '/') for p in _g.glob(os.path.join(PORT, 'runtime', '*.cpp')) +
        _g.glob(os.path.join(PORT, 'sdk', '*.cpp')) + [os.path.join(PORT, 'lib', 'portconfig.cpp'), os.path.join(PORT, 'lib', 'texture.cpp')] if os.path.basename(p) not in ('port_main.cpp', 'unimpl.cpp'))
    for src in srcs:
        o = os.path.join(B, 'rt_' + src.replace('/', '_') + '.o')
        r = zig('c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O1', '-g', '-c', '-I', 'lib', '-I', 'sdk', '-fno-sanitize=all', src, '-o', o)
        if r.returncode:
            print(r.stderr)
            sys.exit(1)
        rt.append(o)
    exe = os.path.join(B, 'NiceCrashGame.exe')
    # zig's driver will not pass /force:multiple (MWCC weak inline copies are plain duplicate definitions in the
    # decomp). Learn zig's lld-link line once from a probe link, then run lld-link ourselves with the flag.
    probe = zig('build-exe', '-target', 'x86-windows-gnu', '-O', 'ReleaseFast', '-lc', '-lc++', '-lopengl32',
                '-lgdi32', '-luser32', '-lbcrypt', '-ldbghelp', '-lwinmm', rt[1], '--name', 'probe', '--verbose-link')
    line = next((l for l in (probe.stdout + probe.stderr).splitlines() if l.startswith('lld-link ')), None)
    if not line:
        print(probe.stderr[:3000])
        sys.exit(1)
    args = line.split()[1:]
    # Keep zig's options and its CRT/import libraries; drop the probe's own outputs and objects.
    keep = [a for a in args if (a.startswith('-') and not a.startswith(('-OUT:', '-PDB', '-IMPLIB:')))
            or a.lower().endswith(('.lib', '.obj'))]
    base = [trial.find_zig(), 'lld-link', *keep, '-OUT:' + exe, '-PDB:' + exe[:-4] + '.pdb', '/force:multiple', '/LARGEADDRESSAWARE', '/DYNAMICBASE:NO',
            '/demangle:no', *good, *rt]
    # Pass 1 without stubs: the linker tells us exactly what nobody (game, runtime, CRT, Windows) defines.
    r1 = subprocess.run(rsp(base), cwd=PORT, capture_output=True, text=True, errors='replace', creationflags=0x08000000)
    linker_undef = set(re.findall(r'undefined symbol: (\S+)', r1.stdout + r1.stderr))
    # Unresolved symbols across game + runtime.
    allsyms = {os.path.basename(o): coffsyms.read_syms(o) for o in good + rt}
    unresolved = sorted(linker_undef)
    json.dump({'objs': allsyms, 'unresolved': unresolved}, open(os.path.join(B, 'syms.json'), 'w'))
    subprocess.run([sys.executable, '-I', os.path.join(HERE, 'classify.py'), os.path.join(B, 'syms.json')],
                   check=True, capture_output=True)
    rows = [l.rstrip('\n').split('\t') for l in open(os.path.join(B, 'unresolved.tsv'), encoding='utf-8')][1:]
    asm = ['\t.text']
    data = ['\t.bss']
    rodata = ['\t.section .rdata,"dr"']
    nf = nd = 0
    fixes = []
    # Bridges: units that call a C++ function by its MWCC-mangled name ("Mat3Mul__FP4Mat3...") or as a plain C
    # name get a jump to the host's (Itanium-mangled) definition when exactly one function of that name exists.
    by_base = {}
    for o, s in allsyms.items():
        for d in s['def']:
            if not d.startswith('__Z'):
                continue
            leaf = classify_leaf(d)
            if leaf:
                by_base.setdefault(leaf, set()).add(d)
    by_leaf = {}
    for o, s in allsyms.items():
        for d in s['def']:
            if d.startswith('__Z'):
                leaf, scopes = itanium_leaf(d)
                by_leaf.setdefault((leaf, tuple(scopes)), set()).add(d)
            elif d.startswith('_') and re.match(r'_(fn|func)_[0-9A-F]{8}$', d):
                by_leaf.setdefault((d[1:], ()), set()).add(d)
    # Members called by their MWCC names from C code ("Slot1C__8UIWidgetFii", "__ct__8UIWidgetFiiiii": the
    # decomp's transitional extern "C" aliases) bind to the C++ definition (Itanium name) of the same class and
    # member; overloads are told apart by the parameter encoding, which both manglings spell alike for these types.
    def norm_mwcc_params(p):
        p = p.replace('Ui', 'j').replace('Uc', 'h').replace('Us', 't').replace('Ul', 'j').replace('Sc', 'a')
        p = p.replace('l', 'i').replace('PC', 'PK').replace('RC', 'RK')
        return '' if p == 'v' else p

    def mwcc_member_target(sym):
        m = re.match(r'^_(__ct|__dt|[A-Za-z]\w*?)__(\d+)(.*)$', sym)
        if not m:
            return None
        meth, n, tail = m.group(1), int(m.group(2)), m.group(3)
        cls, params = tail[:n], tail[n:]
        if not params.startswith('F'):
            return None
        params = params[1:]
        leaf = {'__ct': cls, '__dt': '~' + cls}.get(meth, meth)
        cands = sorted(by_leaf.get((leaf, (cls,)), set()))
        if leaf == cls:
            cands = [c for c in cands if 'C1E' in c] or cands
        if leaf.startswith('~'):
            cands = [c for c in cands if 'D1E' in c] or cands
        if len(cands) > 1:
            want = norm_mwcc_params(params)
            cands = [c for c in cands if c.split('E', 1)[-1] in (want, want or 'v')]
        return cands[0] if len(cands) == 1 else None

    nb = 0
    bridges = []  # (undefined symbol, definition it binds to, the decomp's unit for it, how)
    # Retail vtables among the asm-only data (two zero words, then .text addresses): translated to host tables.
    defs = set()
    for s_ in allsyms.values():
        defs.update(s_['def'])
    text_names = {}
    for line in open(os.path.join(trial.ET, 'config', 'REXE01', 'symbols.txt'), encoding='utf-8'):
        m = re.match(r'^(\S+) = \.text:0x([0-9A-Fa-f]+);', line)
        if m:
            text_names.setdefault(int(m.group(2), 16), m.group(1))
    dol = read_dol_data()
    elems = decl_elem_sizes()
    nvt = 0
    ptrfix = []
    data_rw = ['\t.data']
    missing = {}

    def dol_vtable(addr, size):
        if not dol:
            return None
        a0 = int(addr, 16)
        words = [dol(a0 + 4 * i) for i in range(max(int(size), 8) // 4)]
        if None in words[:3] or words[0] or words[1] or not (0x80004000 <= words[2] < 0x802ED060):
            return None
        out = []
        for w in words[2:]:
            if not (0x80004000 <= w < 0x802ED060):
                break
            out.append(w)
        return out if len(out) >= 2 else None

    def host_func(a):
        name = text_names.get(a, 'fn_%08X' % a)
        if '_' + name in defs:
            return '_' + name
        m = re.match(r'^(\w+?)__(\d+)(\w+?)F', name)  # MWCC "Method__<len>ClassF..."
        if m:
            leaf, cls = m.group(1), m.group(3)[:int(m.group(2))]
            hits = [d for d in defs if d.startswith('__Z') and itanium_leaf(d) == (leaf, [cls])]
            if len(hits) == 1:
                return hits[0]
        lab = missing.setdefault(a, 'Lvtmiss_%08X' % a)
        return lab

    for sym, name, kind, unit, sec, addr, size, cat in rows:
        if sym in ('_port_dolfix_table', '_port_dolptr_table'):
            continue  # generated below
        mbase = sym.lstrip('_').split('__F')[0] if not sym.startswith('__Z') else None
        target = None
        if kind != 'object' and mbase and len(by_base.get(mbase, ())) == 1:
            target = next(iter(by_base[mbase]))
        elif kind != 'object' and sym.startswith('__Z'):
            # A C++ declaration whose parameter types differ from the definition's (u32 vs int: mcsys
            # fn_802E6BD8 as declared by achieve/ident) has another Itanium name; MWCC's mangling of the same
            # pair differs too, but the retail call binds by address. Bridge when exactly one function of that
            # name (and scope) is defined; argument order is regorder.py's business.
            leaf, scopes = itanium_leaf(sym)
            cands = by_leaf.get((leaf, tuple(scopes)), set())
            if len(cands) == 1:
                target = next(iter(cands))
        if not target and kind != 'object':
            target = mwcc_member_target(sym)
        if not target and kind != 'object':
            # An address name (fn_802228B0) whose function the decomp has since named (a class ctor, ...): both
            # are the same retail address, so bind to whatever symbols.txt now calls it.
            m = re.match(r'^_fn_([0-9A-F]{8})$', sym)
            renamed = text_names.get(int(m.group(1), 16)) if m else None
            if renamed and renamed != sym[1:]:
                if '_' + renamed in defs:
                    target = '_' + renamed
                else:
                    target = mwcc_member_target('_' + renamed)
        if target and re.search(r'[CD][12]E', target):
            # MWCC constructors/destructors return `this`; Itanium ones on x86 return nothing. Callers written
            # against the MWCC form use the result (`image = fn_80222530(image, ...)`), so call the member with a
            # copy of the arguments (12 words; cdecl, the caller pops) and return the first one.
            asm += ['\t.globl %s' % gas_name(sym), '%s:' % gas_name(sym), '\tpushl %ebp', '\tmovl %esp, %ebp',
                    '\tsubl $48, %esp']
            for i in range(12):
                asm += ['\tmovl %d(%%ebp), %%eax' % (8 + 4 * i), '\tmovl %%eax, %d(%%esp)' % (4 * i)]
            asm += ['\tcall %s' % gas_name(target), '\tmovl 8(%ebp), %eax', '\tleave', '\tret']
            nb += 1
            bridges.append((sym, target, unit, 'ctor/dtor (returns this)'))
            continue
        if target:
            asm += ['\t.globl %s' % gas_name(sym), '%s:' % gas_name(sym), '\tjmp %s' % gas_name(target)]
            nb += 1
            bridges.append((sym, target, unit, 'jump'))
            continue
        if re.match(r'(sdk_|gxr_|gxi_|port_)', name):
            sys.exit('link: the port\'s own symbol %s is undefined (declaration/linkage mismatch?)' % name)
        vt = dol_vtable(addr, size) if kind == 'object' and addr else None
        if vt:
            # A retail MWCC vtable in asm-only data: build the host (Itanium-layout) table from the same functions.
            lab = 'Lvt%d' % nvt
            entries = []
            for k, a in enumerate(vt):
                if k == 0:  # MWCC destructor dtor(this, flag) -> complete (flag 0) and deleting (flag 1) thunks
                    target = host_func(a)
                    for flag, suf in ((0, 'D1'), (1, 'D0')):
                        asm += ['%s_%s:' % (lab, suf), '\tpushl $%d' % flag, '\tpushl 8(%esp)',
                                '\tcall %s' % gas_name(target), '\taddl $8, %esp', '\tret']
                        entries.append('%s_%s' % (lab, suf))
                else:
                    entries.append(gas_name(host_func(a)))
            data_rw.extend(['\t.balign 4', '%s:' % lab, '\t.long 0, 0, ' + ', '.join(entries),
                            '\t.globl %s' % gas_name(sym), '\t.set %s, %s + 8' % (gas_name(sym), lab)])
            nvt += 1
            continue
        if kind == 'object' and addr and 0x80000000 <= int(addr, 16) < 0x81800000:
            # Asm-only data: an absolute symbol at its Wii address; runtime/dolload.cpp puts the retail bytes there.
            data += ['\t.globl %s' % gas_name(sym), '\t.set %s, 0x%s' % (gas_name(sym), addr)]
            a0, sz = int(addr, 16), int(size)
            # Pointer words in the retail bytes: code pointers -> host function, data pointers -> byte-swapped.
            ptr_words = set()
            if dol and sec not in ('.bss', '.sbss'):
                for k in range(0, sz - 3, 4):
                    w = dol(a0 + k)
                    if w is None:
                        continue
                    if 0x80004000 <= w < 0x802ED060 and w in text_names:
                        ptrfix.append((a0 + k, host_func(w)))
                        ptr_words.add(k)
                    elif 0x802ED060 <= w < 0x80565240 and w % 4 == 0:
                        ptrfix.append((a0 + k, None))
                        ptr_words.add(k)
            elem = elems.get(name)
            if sec not in ('.sdata2', '.sdata', '.data', '.rodata', '.bss', '.sbss'):
                pass
            elif elem in (2, 4, 8):  # typed in the source: swap every element, except the pointer words
                run = None
                for k in range(0, sz - sz % elem, elem):
                    if elem == 4 and k in ptr_words:
                        run = None
                    elif run is not None and run[0] + run[1] == a0 + k:
                        run[1] += elem
                    else:
                        run = [a0 + k, elem, elem]
                        fixes.append(run)
            elif elem is None and sz in (2, 4, 8) and not ptr_words:
                fixes.append([a0, sz, sz])
            nd += 1
        elif kind == 'object':
            n = max(int(size), 4)
            data += ['\t.balign 8', '\t.globl %s' % gas_name(sym), '%s:' % gas_name(sym), '\t.space %d' % n]
            nd += 1
        else:
            lab = 'Lname%d' % nf
            rodata += ['%s: .asciz "%s (%s, %s)"' % (lab, name.replace('"', ''), unit, addr or '?')]
            pop = thiscall_pop(sym)
            asm += ['\t.globl %s' % gas_name(sym), '%s:' % gas_name(sym),
                    '\tpushl $%s' % lab, '\tcall _port_unimpl', '\taddl $4, %esp',
                    '\tret $%d' % pop if pop else '\tret']
            nf += 1
    for a, lab in sorted(missing.items()):  # vtable entries with no host function: report by name when called
        rodata += ['%s_n: .asciz "vtable entry %s (%08X)"' % (lab, text_names.get(a, '?'), a)]
        asm += ['%s:' % lab, '\tpushl $%s_n' % lab, '\tcall _port_unimpl', '\taddl $4, %esp', '\tret']
    data += data_rw
    if not dol:
        print('WARNING: main.dol not found (set NICECRASH_DOL or use link.sh): retail vtables NOT translated')
    print('dol vtables: %d translated, %d entries without a host function' % (nvt, len(missing)))
    # Pointer words in DOL-backed labels (runtime/dolload.cpp applies them after loading the data sections).
    rodata += ['	.balign 4', '	.globl _port_dolptr_table', '_port_dolptr_table:']
    rodata += ['	.long 0x%08X, %s' % (a, gas_name(h) if h else '0') for a, h in ptrfix] + ['	.long 0, 0']
    print('dol data pointers: %d code, %d data' % (sum(1 for _, h in ptrfix if h), sum(1 for _, h in ptrfix if not h)))
    # Scalars among the DOL-backed labels: byte-swapped once at startup (runtime/dolload.cpp).
    rodata += ['\t.balign 4', '\t.globl _port_dolfix_table', '_port_dolfix_table:']
    rodata += ['\t.long 0x%08X, %d, %d' % (a, n, e) for a, n, e in fixes] + ['\t.long 0, 0, 0']
    print('dol scalars: %d swap runs (%d names typed by source declarations)' %
          (len(fixes), sum(1 for x in elems.values() if x)))
    s = os.path.join(B, 'stubs.s')
    open(s, 'w').write('\n'.join(asm + data + rodata) + '\n')
    so = os.path.join(B, 'stubs.o')
    r = zig('cc', '-target', 'x86-windows-gnu', '-c', s, '-o', so)
    if r.returncode:
        print(r.stderr[:3000])
        sys.exit(1)
    print('stubs: %d functions, %d data; %d MWCC-name bridges' % (nf, nd, nb))
    with open(os.path.join(B, 'bridges.tsv'), 'w') as f:  # docs/BRIDGES.md is generated from this
        for row in sorted(bridges):
            f.write('\t'.join(str(x) for x in row) + '\n')
    cmd = base + [so]
    # The program icon (assets/icon/nicecrash.rc -> a .res lld-link embeds): Explorer, window and taskbar.
    res = os.path.join(B, 'nicecrash.res')
    rr = subprocess.run([trial.find_zig(), 'rc', '/fo', res, os.path.join(PORT, 'assets', 'icon', 'nicecrash.rc')],
                        cwd=os.path.join(PORT, 'assets', 'icon'), capture_output=True, text=True,
                        creationflags=0x08000000)
    if rr.returncode == 0:
        cmd.append(res)
    else:
        print('WARNING: icon resource not built:', rr.stderr[:500])
    r = subprocess.run(rsp(cmd), cwd=PORT, capture_output=True, text=True, errors='replace', creationflags=0x08000000)
    out = r.stdout + r.stderr
    open(os.path.join(B, 'link.log'), 'w', encoding='utf-8').write(out)
    print('link', 'OK' if r.returncode == 0 else 'FAILED', '(%d game objects)' % len(good))
    if r.returncode:
        print(out[:4000])


if __name__ == '__main__':
    main()
