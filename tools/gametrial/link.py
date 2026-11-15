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


def thiscall_pop(sym):
    """Bytes a __thiscall member function pops (i386 MinGW: non-static member functions are thiscall, callee
    cleans the stack). None for anything else (free functions, C names, varargs)."""
    if not sym.startswith('__ZN'):
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
    for f in sorted(os.listdir(objdir)):
        log = os.path.join(HERE, 'out', f[:-2] + '.log')
        if f.endswith('.o') and os.path.getsize(os.path.join(objdir, f)) and \
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
                '-lgdi32', '-luser32', '-lbcrypt', '-ldbghelp', rt[1], '--name', 'probe', '--verbose-link')
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
    nb = 0
    for sym, name, kind, unit, sec, addr, size, cat in rows:
        if sym == '_port_dolfix_table':
            continue  # generated below
        mbase = sym.lstrip('_').split('__F')[0] if not sym.startswith('__Z') else None
        if kind != 'object' and mbase and len(by_base.get(mbase, ())) == 1:
            target = next(iter(by_base[mbase]))
            asm += ['\t.globl %s' % gas_name(sym), '%s:' % gas_name(sym), '\tjmp %s' % gas_name(target)]
            nb += 1
            continue
        if re.match(r'(sdk_|gxr_|gxi_|port_)', name):
            sys.exit('link: the port\'s own symbol %s is undefined (declaration/linkage mismatch?)' % name)
        if kind == 'object' and addr and 0x80000000 <= int(addr, 16) < 0x81800000:
            # Asm-only data: an absolute symbol at its Wii address; runtime/dolload.cpp puts the retail bytes there.
            data += ['\t.globl %s' % gas_name(sym), '\t.set %s, 0x%s' % (gas_name(sym), addr)]
            if int(size) in (2, 4, 8) and sec in ('.sdata2', '.sdata', '.data', '.rodata', '.bss', '.sbss'):
                fixes.append((addr, int(size)))
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
    # Scalars among the DOL-backed labels: byte-swapped once at startup (runtime/dolload.cpp).
    rodata += ['\t.balign 4', '\t.globl _port_dolfix_table', '_port_dolfix_table:']
    rodata += ['\t.long 0x%s, %d' % (a, n) for a, n in fixes] + ['\t.long 0, 0']
    s = os.path.join(B, 'stubs.s')
    open(s, 'w').write('\n'.join(asm + data + rodata) + '\n')
    so = os.path.join(B, 'stubs.o')
    r = zig('cc', '-target', 'x86-windows-gnu', '-c', s, '-o', so)
    if r.returncode:
        print(r.stderr[:3000])
        sys.exit(1)
    print('stubs: %d functions, %d data; %d MWCC-name bridges' % (nf, nd, nb))
    cmd = base + [so]
    r = subprocess.run(rsp(cmd), cwd=PORT, capture_output=True, text=True, errors='replace', creationflags=0x08000000)
    out = r.stdout + r.stderr
    open(os.path.join(B, 'link.log'), 'w', encoding='utf-8').write(out)
    print('link', 'OK' if r.returncode == 0 else 'FAILED', '(%d game objects)' % len(good))
    if r.returncode:
        print(out[:4000])


if __name__ == '__main__':
    main()
