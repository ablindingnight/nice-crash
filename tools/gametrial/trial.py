"""Route A trial: compile-check every excite-truck/src/game unit for 32-bit x86 Windows with zig.

Read-only on the decomp. Each job gets its own zig local cache (parallel zig processes race on a shared one).
Usage: python trial.py [--jobs N] [--only a.cpp,b.cpp] [--obj]   (--obj: compile to .o instead of -fsyntax-only)
Writes out/<unit>.log and out/summary.tsv (unit, errors, first error).
"""
import concurrent.futures, glob, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.normpath(os.path.join(HERE, '..', '..'))
ET = os.path.normpath(os.path.join(PORT, '..', 'excite-truck'))
OUT = os.path.join(HERE, 'out')
SHIM = os.path.join(PORT, 'shim', 'include')


def find_zig():
    for p in os.environ.get('PATH', '').split(os.pathsep):
        z = os.path.join(p, 'zig.exe')
        if os.path.exists(z):
            return z
    g = glob.glob(os.path.expandvars(r'%LOCALAPPDATA%\Microsoft\WinGet\Packages\zig.zig*\*\zig.exe'))
    return g[0]


def flags_for(src):
    if src.endswith('.c'):
        return ['cc', '-std=gnu99']
    return ['c++', '-std=gnu++11']


def run(zig, src, obj):
    b = os.path.basename(src)
    mode, std = flags_for(src)
    cache = os.path.join(PORT, 'build', 'zigcache')
    env = dict(os.environ, ZIG_LOCAL_CACHE_DIR=cache)
    out = ['-c', '-o', os.path.join(OUT, 'obj', b + '.o')] if obj else ['-fsyntax-only']
    cmd = [zig, mode, '-target', 'x86-windows-gnu', std, *out, '-w', '-ferror-limit=100',
           '-I', SHIM, '-I', os.path.join(PORT, 'build', 'gamesrc', 'include'), '-I', os.path.join(ET, 'include'), '-I', os.path.join(PORT, 'build', 'gamesrc'), '-I', os.path.join(ET, 'src'),
           '-include', os.path.join(SHIM, 'port_prefix.h'), src]
    p = subprocess.run(cmd, env=env, cwd=PORT, capture_output=True, text=True, errors='replace',
                       creationflags=0x08000000)
    log = p.stdout + p.stderr
    open(os.path.join(OUT, b + '.log'), 'w', encoding='utf-8').write(log)
    errs = re.findall(r' error: (.*)', log)
    return b, len(errs), errs[0] if errs else ''


def main():
    jobs, only, obj = 6, None, False
    a = sys.argv[1:]
    while a:
        x = a.pop(0)
        if x == '--jobs': jobs = int(a.pop(0))
        elif x == '--only': only = set(a.pop(0).split(','))
        elif x == '--obj': obj = True
    os.makedirs(os.path.join(OUT, 'obj'), exist_ok=True)
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
