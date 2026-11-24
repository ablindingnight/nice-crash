"""Compile the decomp's matched SDK MP3 decoder (src/sdk_mp3, the TMCCMP3* API) for the host.

The SD-card custom soundtrack (game/sdmp3.cpp) decodes with it; on the PC the "SD card" is a music folder
(sdk/vfmusic.cpp). The decoder's constant tables are not in the decomp: its tools/mp3/gentables.py generates them
from main.dol (NICECRASH_DOL or dump.ini) into build/mp3src/include/sdk_mp3; we then keep only each table's name,
type and shape there (dumptables.rewrite_generated): the tables are read from the player's main.dol at run time
(runtime/dolload.cpp), so the compiled objects carry no DOL data. Nothing from the DOL is stored in the port repo.

Usage: python mp3build.py  -> build/mp3obj/*.o, logs next to them
"""
import glob, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.normpath(os.path.join(HERE, '..', '..'))
SRC = os.path.join(PORT, 'build', 'mp3src')
OBJ = os.path.join(PORT, 'build', 'mp3obj')
sys.path.insert(0, HERE)
import trial  # noqa: E402
import decomp  # noqa: E402
ET = decomp.root()


def player_dol():
    dol = os.environ.get('NICECRASH_DOL')
    if not dol:
        ini = os.path.join(os.environ.get('LOCALAPPDATA', ''), 'NiceCrash', 'dump.ini')
        if os.path.exists(ini):
            for line in open(ini, encoding='utf-8'):
                if line.startswith('dol='):
                    dol = line[4:].strip()
    if not dol or not os.path.exists(dol):
        sys.exit("mp3build: the decoder tables need the player's main.dol (NICECRASH_DOL or dump.ini)")
    return dol


def gen_tables():
    """gentables.py reads orig/REXE01/sys/main.dol relative to its working directory: give it a private one."""
    tool = os.path.join(ET, 'tools', 'mp3', 'gentables.py')
    work = os.path.join(PORT, 'build', 'mp3gen')
    dol_dst = os.path.join(work, 'orig', 'REXE01', 'sys', 'main.dol')
    os.makedirs(os.path.dirname(dol_dst), exist_ok=True)
    src_dol = player_dol()
    if not os.path.exists(dol_dst) or os.path.getsize(dol_dst) != os.path.getsize(src_dol):
        shutil.copyfile(src_dol, dol_dst)
    sym_dst = os.path.join(work, 'config', 'REXE01', 'symbols.txt')  # it also reads the symbol list there
    os.makedirs(os.path.dirname(sym_dst), exist_ok=True)
    shutil.copyfile(os.path.join(ET, 'config', 'REXE01', 'symbols.txt'), sym_dst)
    out = os.path.join(SRC, 'include', 'sdk_mp3')
    os.makedirs(out, exist_ok=True)
    r = subprocess.run([sys.executable, '-I', tool, out], cwd=work, capture_output=True, text=True,
                       creationflags=0x08000000)
    if r.returncode:
        sys.exit('mp3build: gentables failed:\n' + r.stdout[-2000:] + r.stderr[-2000:])
    # Keep the names and shapes, drop the bytes: each table becomes a view at its Wii address, filled from the
    # player's main.dol at run time (dumptables.py, runtime/dolload.cpp). The exe carries no DOL data.
    import dumptables
    tables = []
    for f in sorted(glob.glob(os.path.join(out, '*.inc'))):
        t = open(f, encoding='utf-8').read()
        t2, found = dumptables.rewrite_generated(t, os.path.basename(f))
        open(f, 'w', encoding='utf-8', newline='\n').write(t2)
        tables += found
    dumptables.write_runs('mp3', tables)
    return out


def main():
    src_dir = os.path.join(ET, 'src', 'sdk_mp3')
    srcs = sorted(glob.glob(os.path.join(src_dir, '*.c')))
    if not srcs:
        sys.exit('mp3build: no src/sdk_mp3 in the pinned decomp')
    inc = gen_tables()
    os.makedirs(OBJ, exist_ok=True)
    zig = trial.find_zig()
    bad = 0
    for s in srcs:
        b = os.path.basename(s)
        o = os.path.join(OBJ, 'mp3_' + b + '.o')
        cmd = [zig, 'cc', '-target', 'x86-windows-gnu', *trial.HOST_TRIPLE, '-std=gnu99', '-O2', '-g', '-w',
               '-fno-sanitize=all', '-fno-strict-aliasing', '-fwrapv', '-c', '-I', src_dir, '-I', inc, s, '-o', o]
        p = subprocess.run(cmd, cwd=PORT, capture_output=True, text=True, errors='replace', creationflags=0x08000000,
                           env=dict(os.environ, ZIG_LOCAL_CACHE_DIR=os.path.join(PORT, 'build', 'zigcache')))
        log = p.stdout + p.stderr
        open(os.path.join(OBJ, b + '.log'), 'w', encoding='utf-8').write(log)
        if p.returncode:
            bad += 1
            print('mp3build: %s failed: %s' % (b, (re.findall(r' error: (.*)', log) or [log[-300:]])[0]))
    print('mp3 decoder              %d of %d units' % (len(srcs) - bad, len(srcs)))
    return 1 if bad else 0


def objects():
    return sorted(glob.glob(os.path.join(OBJ, '*.o')))


if __name__ == '__main__':
    sys.exit(main())
