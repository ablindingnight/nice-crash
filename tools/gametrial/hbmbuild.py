"""Compile the decomp's HOME button library for the host.

Excite Truck's HOME menu library comes from two decomp trees (both matched, both read from the pinned snapshot,
decomp.py):
  - libs/revolution/src/homebuttonLib (TP-derived, configure.py lib HBM_tp, 16 units): the HOME menu itself
    (HBMBase, HBMGUIManager, HBMController, HBMAnmController, HBMRemoteSpk) and most of nw4hbm's layout engine;
  - libs/msc/src/RVL_SDK/hbm (Strikers-derived, lib HBM, the units configure.py lists): nw4hbm's sound system,
    layout animation, math, lists, HBMFrameController.
Sources are copied to build/hbmsrc (tp/ and msc/), port-only rewrites applied (mechanical ones below, and
patches/hbm-*.patch.txt: same format as the game patches, file paths prefixed tp: or msc:); objects go to
build/hbmobj.

Usage: python hbmbuild.py [--syntax]   (--syntax: only check, no objects)
"""
import concurrent.futures, glob, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import decomp  # noqa: E402
import trial  # noqa: E402

TP_UNITS = [
    'nw4hbm/lyt/lyt_layout.cpp', 'nw4hbm/ut/ut_CharWriter.cpp', 'nw4hbm/lyt/lyt_window.cpp',
    'nw4hbm/lyt/lyt_textBox.cpp', 'nw4hbm/lyt/lyt_arcResourceAccessor.cpp', 'nw4hbm/lyt/lyt_drawInfo.cpp',
    'nw4hbm/db/db_directPrint.cpp', 'HBMBase.cpp', 'HBMAnmController.cpp', 'HBMGUIManager.cpp',
    'HBMController.cpp', 'HBMRemoteSpk.cpp', 'nw4hbm/lyt/lyt_pane.cpp', 'nw4hbm/lyt/lyt_material.cpp',
    'nw4hbm/lyt/lyt_common.cpp', 'nw4hbm/lyt/lyt_group.cpp',
]
OUT = os.path.join(PORT, 'build', 'hbmsrc')
OBJ = os.path.join(PORT, 'build', 'hbmobj')
SHIM = os.path.join(PORT, 'shim', 'hbm_include')
COMMON = ['-DVERSION=3', '-D__REVOLUTION_SDK__', '-DGAME_REXE01', '-DNDEBUG=1',
          # Retail keeps nw4hbm's asserts; some compare member-function pointers MWCC-style (no &Class::), which
          # standard C++ rejects. The host build drops them.
          '-DNW4HBM_DEBUG=0']


def msc_units():
    """The msc HBM units Excite Truck links, from configure.py at the pinned commit."""
    # configure.py is part of the pinned export (decomp.PATHS), so no git is needed (tester packages have none).
    cfg = open(os.path.join(decomp.root(), 'configure.py'), encoding='utf-8').read()
    return re.findall(r'Object\(\w+, "RVL_SDK/hbm/([^"]+\.cpp)"', cfg)


def units():
    return [('tp', u) for u in TP_UNITS] + [('msc', u) for u in msc_units()]


def tree_root(tree):
    return os.path.join(OUT, tree, 'src')


def apply_patches():
    for pf in sorted(glob.glob(os.path.join(PORT, 'patches', 'hbm-*.patch.txt'))):
        blocks = re.findall(r'@@@ (\w+):(\S+) \| [^\n]*\n<<<\n(.*?)\n===\n(.*?)\n>>>',
                            open(pf, encoding='utf-8').read(), re.S)
        for tree, rel, old, new in blocks:
            # tp: relative to homebuttonLib; msc: relative to build/hbmsrc/msc (src/... or include/...).
            path = os.path.join(tree_root(tree) if tree == 'tp' else os.path.join(OUT, tree), rel)
            t = open(path, encoding='utf-8', errors='surrogateescape').read()
            n = t.count(old)
            if n != 1:
                raise SystemExit('hbmbuild: %s: patch for %s:%s matches %d times' % (os.path.basename(pf), tree, rel, n))
            open(path, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(t.replace(old, new))


def rewrite_headers(root):
    for path in glob.glob(os.path.join(root, '**', '*.h'), recursive=True):
        t = open(path, encoding='utf-8', errors='surrogateescape').read()
        # `virtual ~X() = 0 {}` (MWCC accepts a pure destructor with an inline body) -> plain virtual destructor
        # (the class stays abstract through its other pure virtuals).
        t2 = re.sub(r'#ifdef MAKE_DTOR_ZERO\s*= 0\s*#endif', '', t)
        t2 = re.sub(r'(virtual ~\w+\(\))\s*=\s*0\s*\{\}', r'\1 {}', t2)
        # Dependent type names inside templates need `typename` (MWCC infers it).
        t2 = re.sub(r'(?<!typename )\bTPrioList::Iterator\b', 'typename TPrioList::Iterator', t2)
        # Member-function pointers compared by bare name (MWCC): standard C++ takes &Class::member.
        t2 = re.sub(r'mReadFunc == (ReadNextChar\w+)', r'mReadFunc == &CharStrmReader::\1', t2)
        # In-class `static const int X = (int)(10 * float_const)` needs a constexpr float.
        t2 = re.sub(r'static const f32 (VOLUME_(?:MIN|MAX|RANGE)_DB)\b', r'static constexpr f32 \1', t2)
        # Hardware registers defined at fixed addresses (MWCC `X[] : 0xCD000000`): declarations only on the host.
        t2 = re.sub(r'(?m)^(\s*)volatile (\w+) (\w+)\[\] AT_ADDRESS\([^)]*\);', r'\1extern volatile \2 \3[];', t2)
        if t2 != t:
            open(path, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(t2)


def prep():
    d = decomp.root()
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    shutil.copytree(os.path.join(d, 'libs', 'revolution'), os.path.join(OUT, 'tp'))
    shutil.copytree(os.path.join(d, 'libs', 'msc', 'src', 'RVL_SDK', 'hbm'), os.path.join(OUT, 'msc', 'src'))
    shutil.copytree(os.path.join(d, 'libs', 'msc', 'libs', 'RVL_SDK', 'include'), os.path.join(OUT, 'msc', 'include'))
    rewrite_headers(os.path.join(OUT, 'tp', 'src', 'homebuttonLib'))
    rewrite_headers(os.path.join(OUT, 'msc'))
    # As in sdkbuild.py: the host C library owns these names; drop the SDK's own definitions.
    tp = os.path.join(OUT, 'msc', 'include', 'revolution', 'types.h')
    t = open(tp, encoding='utf-8', errors='surrogateescape').read()
    for name in ('uintptr_t', 'wchar_t', 'char16_t', 'register_t'):
        t = re.sub(r'typedef [^;]*\b%s;\n' % name, '', t)
    t = t.replace('#include <decomp.h>', '#include <decomp.h>\n#include <stddef.h>\n#include <stdint.h>', 1)
    open(tp, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(t)
    # The tp tree's patches name files relative to homebuttonLib.
    os.rename(os.path.join(OUT, 'tp', 'src'), os.path.join(OUT, 'tp', 'src0'))
    os.rename(os.path.join(OUT, 'tp', 'src0', 'homebuttonLib'), os.path.join(OUT, 'tp', 'src'))
    apply_patches()
    # The write-gather pipe (0xCC008000) -> the port FIFO (shim/hbm_include/hbm_wgpipe.h), in both header sets.
    vert = os.path.join(OUT, 'tp', 'include', 'revolution', 'gx', 'GXVert.h')
    t = open(vert, encoding='utf-8').read()
    old = '#define GXWGFifo (*(volatile PPCWGPipe *)GXFIFO_ADDR)'
    if t.count(old) != 1:
        raise SystemExit('hbmbuild: GXVert.h pipe define not found')
    open(vert, 'w', encoding='utf-8', newline='').write(t.replace(old, '#include <hbm_wgpipe.h>\n#define GXWGFifo port_hbm_wgpipe'))
    hw = os.path.join(OUT, 'msc', 'include', 'revolution', 'gx', 'GXHardware.h')
    t = open(hw, encoding='utf-8').read()
    t2 = re.sub(r'extern volatile union \{.*?\} WGPIPE AT_ADDRESS\(0xCC008000\);',
                '#include <hbm_wgpipe.h>\n#define WGPIPE port_hbm_wgpipe', t, flags=re.S)
    if t2 == t:
        raise SystemExit('hbmbuild: GXHardware.h WGPIPE union not found')
    open(hw, 'w', encoding='utf-8', newline='').write(t2)
    # ut::Color <-> u32 reinterprets the four bytes: a big-endian word on the Wii (r = bits 24-31). Make the
    # conversions explicit so literals (Color::RED = 0xFF0000FF) and the resource files' colour words (swapped to
    # native by sdk/hbmres.cpp) mean the same colours on the host.
    for path in (os.path.join(OUT, 'tp', 'src', 'nw4hbm', 'ut', 'Color.h'),
                 os.path.join(OUT, 'msc', 'include', 'revolution', 'hbm', 'nw4hbm', 'ut', 'Color.h')):
        t = open(path, encoding='utf-8').read()
        for old, new in (
                ('ToU32ref() = color;', 'r = (u8)(color >> 24); g = (u8)(color >> 16); b = (u8)(color >> 8); a = (u8)color;'),
                ('return *this = *reinterpret_cast<const u32*>(&color);', 'r = color.r; g = color.g; b = color.b; a = color.a; return *this;'),
                ('u32 ToU32() const { return ToU32ref(); }', 'u32 ToU32() const { return (u32)r << 24 | (u32)g << 16 | (u32)b << 8 | a; }'),
                ('operator u32() const { return ToU32ref(); }', 'operator u32() const { return ToU32(); }')):
            if t.count(old) != 1:
                raise SystemExit('hbmbuild: Color.h rewrite %r matches %d times in %s' % (old, t.count(old), path))
            t = t.replace(old, new)
        open(path, 'w', encoding='utf-8', newline='').write(t)
    # tp_include/global.h without its non-MWCC intrinsic declarations (they clash with the port prefix's).
    g = open(os.path.join(d, 'libs', 'tp_include', 'global.h'), encoding='utf-8').read()
    g = re.sub(r'#ifndef __MWERKS__\n// Silence clangd.*?#endif\n', '', g, flags=re.S)
    os.makedirs(os.path.join(OUT, 'tpinc'), exist_ok=True)
    open(os.path.join(OUT, 'tpinc', 'global.h'), 'w', encoding='utf-8', newline='').write(g)


def obj_name(tree, unit):
    return ('hbm_' if tree == 'tp' else 'hbmm_') + os.path.basename(unit)


def flags(tree):
    d = decomp.root()
    if tree == 'tp':
        inc = os.path.join(OUT, 'tp', 'include')
        return ['-DSDK_SEP2006', '-I', SHIM, '-I', os.path.join(OUT, 'tpinc'),
                '-I', os.path.join(d, 'libs', 'tp_include'), '-I', inc, '-I', os.path.join(inc, 'revolution'),
                '-I', os.path.join(OUT, 'tp', 'src0'), '-I', os.path.join(OUT, 'tp', 'src'),
                '-I', os.path.join(d, 'include'),
                '-include', os.path.join(SHIM, 'hbm_prefix.h')]
    # -fdelayed-template-parsing: MWCC resolves template bodies at instantiation (they use classes completed later).
    # -DHBM_ASSERT as in the decomp's cflags_msc_hbm: retail evaluates these asserts (some have side effects).
    return ['-fdelayed-template-parsing', '-DHBM_ASSERT', '-DVERSION_R4QJ01', '-DBUILD_VERSION=3', '-I', SHIM, '-I', os.path.join(PORT, 'shim', 'sdk_include'),
            '-I', os.path.join(d, 'libs', 'msc', 'include'),
            '-I', os.path.join(OUT, 'msc', 'include'), '-I', os.path.join(OUT, 'msc', 'src'),
            '-include', os.path.join(SHIM, 'hbm_prefix_msc.h')]


def compile_one(zig, tree, unit, syntax):
    src = os.path.join(tree_root(tree), unit)
    b = obj_name(tree, unit)
    out = ['-fsyntax-only'] if syntax else ['-c', '-o', os.path.join(OBJ, b + '.o')]
    cmd = [zig, 'c++', '-target', 'x86-windows-gnu', *trial.HOST_TRIPLE, '-std=gnu++14', *out, '-w',
           '-ferror-limit=100', '-O1', '-g', '-fno-sanitize=all', '-fno-strict-aliasing', '-fwrapv',
           '-fno-omit-frame-pointer', '-fno-strict-return', '-fno-exceptions', *COMMON,
           # Data tables the decomp generates from the player's DOL (prep.py runs datagen into build/dumpinc).
           '-I', os.path.join(PORT, 'build', 'dumpinc'),
           '-include', os.path.join(PORT, 'shim', 'include', 'port_prefix.h'), *flags(tree), src]
    p = subprocess.run(cmd, cwd=PORT, capture_output=True, text=True, errors='replace', creationflags=0x08000000,
                       env=dict(os.environ, ZIG_LOCAL_CACHE_DIR=os.path.join(PORT, 'build', 'zigcache', 'hbm', b)))
    log = p.stdout + p.stderr
    open(os.path.join(OBJ, b + '.log'), 'w', encoding='utf-8').write(log)
    errs = re.findall(r' error: (.*)', log)
    return b, len(errs), errs[0] if errs else ''


def objects():
    return [os.path.join(OBJ, obj_name(t, u) + '.o') for t, u in units()]


def main():
    syntax = '--syntax' in sys.argv
    os.makedirs(OBJ, exist_ok=True)
    prep()
    zig = trial.find_zig()
    us = units()
    with concurrent.futures.ThreadPoolExecutor(8) as ex:
        res = list(ex.map(lambda tu: compile_one(zig, tu[0], tu[1], syntax), us))
    for b, n, e in res:
        if n or '-v' in sys.argv:
            print('%-36s %3d %s' % (b, n, e[:110]))
    bad = sum(1 for r in res if r[1])
    print('hbm units %d, clean %d' % (len(res), len(res) - bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
