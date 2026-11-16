"""Route A trial: compile-check every excite-truck/src/game unit for 32-bit x86 Windows with zig.

Read-only on the decomp. Each job gets its own zig local cache (parallel zig processes race on a shared one).
Usage: python trial.py [--jobs N] [--only a.cpp,b.cpp] [--obj]   (--obj: compile to .o instead of -fsyntax-only)
Writes out/<unit>.log and out/summary.tsv (unit, errors, first error).
"""
import concurrent.futures, glob, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import decomp  # noqa: E402
ET = decomp.root()  # the pinned decomp snapshot (tools/gametrial/decomp.py)
OUT = os.path.join(HERE, 'out')
SHIM = os.path.join(PORT, 'shim', 'include')



_BTE = None


def bte_dirs():
    """The decomp builds with `-ir include/revolution/BTE` (MWCC searches every subfolder); the Broadcom headers the
    game's WPAD/KPAD headers now pull in (bt_target.h -> data_types.h) need the same, searched last."""
    global _BTE
    if _BTE is None:
        root = os.path.join(PORT, 'build', 'gamesrc', 'include', 'revolution', 'BTE')
        _BTE = []
        for d, _, fs in sorted(os.walk(root)):
            if any(f.endswith('.h') for f in fs):
                _BTE += ['-idirafter', d]
    return _BTE


_UNITDEF = None


def unit_defines(base):
    """Per-unit -D flags from the decomp's configure.py extra_cflags (rjoy/sysconfig: REVOLUTION and the SDK
    variant, which the BTE/SC headers they include test)."""
    global _UNITDEF
    if _UNITDEF is None:
        import re
        _UNITDEF = {}
        cfg = os.path.join(ET, 'configure.py')
        if os.path.exists(cfg):
            for m in re.finditer(r'"game/([\w.]+)",[^\n]*?extra_cflags=\[([^\]]*)\]', open(cfg, encoding='utf-8').read()):
                _UNITDEF[m.group(1)] = re.findall(r'"(-D\w+(?:=\w+)?)"', m.group(2))
    return _UNITDEF.get(base, [])


def find_zig():
    for p in os.environ.get('PATH', '').split(os.pathsep):
        z = os.path.join(p, 'zig.exe')
        if os.path.exists(z):
            return z
    g = glob.glob(os.path.expandvars(r'%LOCALAPPDATA%\Microsoft\WinGet\Packages\zig.zig*\*\zig.exe'))
    return g[0]


# Compile game (and SDK) code with clang's Cygwin x86 triple while using zig's MinGW headers and libraries:
#  - C++ methods are cdecl (MinGW's i386 default is thiscall). `this` becomes the first stack argument, as it is
#    the first register argument on PowerPC, so the decomp's hand-written vtables of free functions and its
#    per-unit virtual declarations with differing parameter lists keep working.
#  - Bitfields are packed GCC/SysV style (as MWCC does), not MS style; doubles stay 8-aligned.
# The macros put the MinGW view back (_WIN32, __MINGW32__) for the system headers.
HOST_TRIPLE = ['-Xclang', '-triple', '-Xclang', 'i686-pc-windows-cygnus', '-U__CYGWIN__', '-U__CYGWIN32__',
               '-U__unix__', '-U__unix', '-Uunix', '-D_WIN32', '-DWIN32', '-D__MINGW32__',
               '-D_LIBCPP_HAS_THREAD_API_WIN32', '-Wno-macro-redefined']

# C files the decomp compiles with `#pragma cplusplus on` sections: build them as C++.
CXX_C_FILES = {'unk_8020F3B4.c'}


def flags_for(src):
    if os.path.basename(src) in CXX_C_FILES:
        return ['c++', ['-std=gnu++11', '-x', 'c++']]
    if src.endswith('.c'):
        return ['cc', ['-std=gnu99', '-Wno-implicit-function-declaration']]  # C89-style calls (radmemset16)
    return ['c++', ['-std=gnu++11']]


def run(zig, src, obj):
    b = os.path.basename(src)
    mode, std = flags_for(src)
    cache = os.path.join(PORT, 'build', 'zigcache')
    env = dict(os.environ, ZIG_LOCAL_CACHE_DIR=cache)
    out = ['-c', '-o', os.path.join(OUT, 'obj', b + '.o')] if obj else ['-fsyntax-only']
    cmd = [zig, mode, '-target', 'x86-windows-gnu', *HOST_TRIPLE, *std, *out, '-w', '-ferror-limit=100', '-O1', '-g',
           '-fno-sanitize=all', '-fno-strict-aliasing', '-fwrapv', '-fno-omit-frame-pointer',
           # Non-void functions that fall off the end (UICustom::Slot68): PPC returns whatever r3/f1 holds; clang
           # would treat the path as unreachable and run into the next function.
           '-fno-strict-return'] + (['-fno-exceptions'] if mode == 'c++' else []) + [
           '-I', SHIM, '-I', os.path.join(PORT, 'build', 'dumpinc'), '-I', os.path.join(PORT, 'build', 'gamesrc', 'include'), '-I', os.path.join(ET, 'include'), '-I', os.path.join(PORT, 'build', 'gamesrc'), '-I', os.path.join(ET, 'src'),
           *bte_dirs(), *unit_defines(b), '-include', os.path.join(SHIM, 'port_prefix.h'), src]
    p = subprocess.run(cmd, env=env, cwd=PORT, capture_output=True, text=True, errors='replace',
                       creationflags=0x08000000)
    log = p.stdout + p.stderr
    open(os.path.join(OUT, b + '.log'), 'w', encoding='utf-8').write(log)
    errs = re.findall(r' error: (.*)', log)
    return b, len(errs), errs[0] if errs else ''


def main():
    jobs, only, obj, prep = 6, None, False, True
    a = sys.argv[1:]
    while a:
        x = a.pop(0)
        if x == '--jobs': jobs = int(a.pop(0))
        elif x == '--only': only = set(a.pop(0).split(','))
        elif x == '--obj': obj = True
        elif x == '--noprep': prep = False  # debugging: keep hand edits in build/gamesrc
    os.makedirs(os.path.join(OUT, 'obj'), exist_ok=True)
    if prep:
        subprocess.run([sys.executable, '-I', os.path.join(HERE, 'prep.py')], check=True)
    srcs = sorted(glob.glob(os.path.join(PORT, 'build', 'gamesrc', 'game', '*.c*')))
    if only:
        srcs = [s for s in srcs if os.path.basename(s) in only]
    zig = find_zig()
    res = []
    with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
        for r in ex.map(lambda s: run(zig, s, obj), srcs):
            res.append(r)
    res.sort(key=lambda r: (r[1] == 0, -r[1], r[0]))
    if not only:
        with open(os.path.join(OUT, 'summary.tsv'), 'w', encoding='utf-8') as f:
            for r in res:
                f.write('%s\t%d\t%s\n' % r)
    clean = sum(1 for r in res if r[1] == 0)
    print('units %d, clean %d, with errors %d' % (len(res), clean, len(res) - clean))


if __name__ == '__main__':
    main()
