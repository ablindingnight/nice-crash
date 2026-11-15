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
import coffsyms  # noqa: E402


def zig(*args, **kw):
    return subprocess.run([trial.find_zig(), *args], cwd=PORT, capture_output=True, text=True, errors='replace',
                          env=dict(os.environ, ZIG_LOCAL_CACHE_DIR=os.path.join(B, 'zigcache')),
                          creationflags=0x08000000, **kw)


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
        _g.glob(os.path.join(PORT, 'sdk', '*.cpp')) + [os.path.join(PORT, 'lib', 'portconfig.cpp')] if os.path.basename(p) not in ('port_main.cpp', 'unimpl.cpp'))
    for src in srcs:
        o = os.path.join(B, 'rt_' + src.replace('/', '_') + '.o')
        r = zig('c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O1', '-g', '-c', '-I', 'lib', '-fno-sanitize=all', src, '-o', o)
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
    r1 = subprocess.run(base, cwd=PORT, capture_output=True, text=True, errors='replace', creationflags=0x08000000)
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
    for sym, name, kind, unit, sec, addr, size, cat in rows:
        if sym == '_port_dolfix_table':
            continue  # generated below
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
            asm += ['\t.globl %s' % gas_name(sym), '%s:' % gas_name(sym),
                    '\tpushl $%s' % lab, '\tcall _port_unimpl', '\taddl $4, %esp', '\tret']
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
    print('stubs: %d functions, %d data' % (nf, nd))
    cmd = base + [so]
    r = subprocess.run(cmd, cwd=PORT, capture_output=True, text=True, errors='replace', creationflags=0x08000000)
    out = r.stdout + r.stderr
    open(os.path.join(B, 'link.log'), 'w', encoding='utf-8').write(out)
    print('link', 'OK' if r.returncode == 0 else 'FAILED', '(%d game objects)' % len(good))
    if r.returncode:
        print(out[:4000])


if __name__ == '__main__':
    main()
