"""Compile parts of the decomp's matched Revolution SDK source for the host (read-only on the decomp).

Today: the whole GX library from libs/msc (RVL_SDK tree, one header set). Every write to the write-gather pipe
(`WGPIPE.<member> = value`, `GX_WRITE_*`) is rewritten to the port FIFO (sdk/gxfifo.cpp), so GX calls produce
the same command stream they produce on the Wii; sdk/gxinterp.cpp turns that stream into OpenGL.

Usage: python sdkbuild.py  -> build/sdkobj/*.o, logs in build/sdkobj/*.log
"""
import glob, os, re, subprocess, sys
import concurrent.futures

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.normpath(os.path.join(HERE, '..', '..'))
ET = os.path.normpath(os.path.join(PORT, '..', 'excite-truck'))
MSC = os.path.join(ET, 'libs', 'msc')
OUT = os.path.join(PORT, 'build', 'sdksrc')
OBJ = os.path.join(PORT, 'build', 'sdkobj')
sys.path.insert(0, HERE)
import trial  # noqa: E402

# GX, plus MEM (the heaps: the HOME button library allocates from expanded/frame/unit heaps in memory it is given).
LIBS = ['gx', 'mem']
SOURCES = sorted(p for lib in LIBS for p in glob.glob(os.path.join(MSC, 'src', 'RVL_SDK', lib, '*.c')))

MEMBER = {'uc': 'u8', 'c': 'u8', 'us': 'u16', 's': 'u16', 'ui': 'u32', 'i': 'u32', 'u': 'u32', 'p': 'u32',
          'f': 'f32'}
STMT = re.compile(r'WGPIPE\.(\w+)\s*=\s*([^;]+);')


def rewrite(t):
    def rep(m):
        kind = MEMBER.get(m.group(1))
        if not kind:
            raise SystemExit('unknown WGPIPE member %s' % m.group(1))
        cast = '(u32)(uintptr_t)' if m.group(1) == 'p' else ''
        return 'port_fifo_%s(%s(%s));' % (kind, cast, m.group(2))
    return STMT.sub(rep, t)


HW_FIFO = '''
/* port: the write-gather pipe is the host FIFO (port/sdk/gxfifo.cpp) */
#include <stdint.h>
void port_fifo_u8(unsigned char v);
void port_fifo_u16(unsigned short v);
void port_fifo_u32(unsigned int v);
void port_fifo_f32(float v);
#define GX_WRITE_U8(value) port_fifo_u8((u8)(value))
#define GX_WRITE_U16(value) port_fifo_u16((u16)(value))
#define GX_WRITE_U32(value) port_fifo_u32((u32)(value))
#define GX_WRITE_F32(value) port_fifo_f32((f32)(value))
/* Some code still takes &WGPIPE to pass "the pipe" to a helper; the helpers below ignore it. */
static volatile unsigned int WGPIPE;
'''

# GXTransform.c's paired-single helpers (psq_l/psq_st to the pipe or between buffers) as plain C. Same values,
# same order: the pipe receives floats exactly as the quantised stores would send them.
ASM_HELPERS = {
    'LoadProjPS': 'inline void LoadProjPS(f32* dst) { int i; for (i = 0; i < 6; i++) dst[i] = gxdt->proj[i]; }',
    'WriteProjPS': 'inline void WriteProjPS(volatile void* dst, const f32* src) '
                   '{ int i; for (i = 0; i < 6; i++) port_fifo_f32(src[i]); }',
    'Copy6Floats': 'inline void Copy6Floats(f32* dst, const f32* src) { int i; for (i = 0; i < 6; i++) dst[i] = src[i]; }',
    'WriteMTXPS4x3': 'inline void WriteMTXPS4x3(volatile void* dst, const Mtx src) '
                     '{ int r, c; for (r = 0; r < 3; r++) for (c = 0; c < 4; c++) port_fifo_f32(src[r][c]); }',
    'WriteMTXPS3x3': 'inline void WriteMTXPS3x3(volatile void* dst, const Mtx src) '
                     '{ int r, c; for (r = 0; r < 3; r++) for (c = 0; c < 3; c++) port_fifo_f32(src[r][c]); }',
    'WriteMTXPS4x2': 'inline void WriteMTXPS4x2(volatile void* dst, const Mtx src) '
                     '{ int r, c; for (r = 0; r < 2; r++) for (c = 0; c < 4; c++) port_fifo_f32(src[r][c]); }',
    # GXLight.c: three zero words, the colour, then aa..dirZ (12 floats, contiguous from aa).
    'WriteLightObj': 'inline void WriteLightObj(volatile void* dst, const GXLightObjImpl* src) '
                     '{ int i; const f32* f = &src->aa; port_fifo_u32(0); port_fifo_u32(0); port_fifo_u32(0); '
                     'port_fifo_u8(src->color.r); port_fifo_u8(src->color.g); port_fifo_u8(src->color.b); port_fifo_u8(src->color.a); for (i = 0; i < 12; i++) port_fifo_f32(f[i]); }',
}


# Implemented by the host layer instead (GPU synchronisation that depends on interrupts).
HOST_OVERRIDES = ['GXDrawDone', 'GXWaitDrawDone', 'GXFlush', 'GXSetDrawSyncCallback', 'GXSetDrawDoneCallback',
                  'GXEnableBreakPt', 'GXDisableBreakPt', 'GXGetFifoPtrs']


def replace_asm_helpers(t):
    for name, body in ASM_HELPERS.items():
        m = re.search(r'inline void %s\([^)]*\)\s*\{' % name, t)
        if not m:
            continue
        depth, i = 1, m.end()
        while depth:
            depth += {'{': 1, '}': -1}.get(t[i], 0)
            i += 1
        t = t[:m.start()] + body + t[i:]
    return t


# `*(u32*)&color` type-puns of GXColor (4 bytes) read the colour as a big-endian word on the Wii (r = bits 24-31).
# On the host they become explicit big-endian loads/stores. GXColorS10 (four s16) packs (r<<16 | g), (b<<16 | a).
# The float bit-casts in GXSetFogRangeAdj... (a_bits/c_bits) are left alone: they are not colours.
PUN_ST = re.compile(r'\*\(u32\s*\*\)\s*&([\w\->\.\[\]]+)\s*=\s*([^;]+);')
PUN_LD = re.compile(r'\*\(u32\s*\*\)\s*&([\w\->\.\[\]]+)')


def colour_puns(t):
    t = t.replace('sRG = *(u32*)&color;', 'sRG = (u32)(u16)color.r << 16 | (u16)color.g;')
    t = t.replace('sBA = *((u32*)&color + 1);', 'sBA = (u32)(u16)color.b << 16 | (u16)color.a;')
    t = PUN_ST.sub(lambda m: 'port_be_st32(&%s, %s);' % (m.group(1), m.group(2)), t)
    return PUN_LD.sub(lambda m: m.group(0) if m.group(1) in ('a', 'c') else 'port_be_ld32(&%s)' % m.group(1), t)


def prep():
    inc_src = os.path.join(MSC, 'libs', 'RVL_SDK', 'include')
    n = 0
    for src in glob.glob(os.path.join(inc_src, '**', '*.h'), recursive=True) + SOURCES:
        t = open(src, encoding='utf-8', errors='surrogateescape').read()
        if src.endswith('GXHardware.h'):
            # Drop the extern WGPIPE union declaration and the GX_WRITE_* macros; use the host FIFO instead.
            t = re.sub(r'extern volatile union \{.*?\} WGPIPE[^;]*;', HW_FIFO, t, flags=re.S)
            t = re.sub(r'#define GX_WRITE_(U8|U16|U32|F32)\(value\) \(WGPIPE[^\n]*\n', '', t)
        if src.replace(os.sep, '/').endswith('revolution/types.h'):
            # Host C library owns these names: drop the SDK's own definitions, use the host's.
            for name in ('uintptr_t', 'wchar_t', 'char16_t', 'register_t'):
                t = re.sub(r'typedef [^;]*\b%s;\n' % name, '', t)
            t = t.replace('#include <decomp.h>', '#include <decomp.h>\n#include <stddef.h>\n#include <stdint.h>', 1)
        if src.endswith(('GXTransform.c', 'GXLight.c')):
            t = replace_asm_helpers(t)
        if src.endswith('.c'):
            # SDK functions the host layer replaces (sdk/*.cpp): rename the SDK's definitions out of the way.
            for name in HOST_OVERRIDES:
                t = re.sub(r'(\n\w[\w \*]*\b)%s(\s*\([^)]*\)\s*\{)' % name, r'\1__sdk_unused_%s\2' % name, t)
        if src.replace(os.sep, '/').find('/gx/') >= 0 and src.endswith('.c'):
            t = colour_puns(t)
        t2 = rewrite(t)
        n += t2 != t
        rel = os.path.relpath(src, inc_src if src.startswith(inc_src) else os.path.join(MSC, 'src'))
        dst = os.path.join(OUT, 'include' if src.startswith(inc_src) else 'src', rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        open(dst, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(t2)
    return n


def compile_one(zig, src):
    b = os.path.basename(src)
    o = os.path.join(OBJ, 'sdk_' + b + '.o')
    cmd = [zig, 'cc', '-target', 'x86-windows-gnu', *trial.HOST_TRIPLE, '-std=gnu99', '-fgnu89-inline', '-O1', '-g', '-w', '-fno-sanitize=all',
           '-fno-strict-aliasing', '-fwrapv', '-c', '-ferror-limit=50', '-Wno-error=implicit-function-declaration', '-Wno-error=int-conversion', '-Wno-error=incompatible-pointer-types',
           '-I', os.path.join(PORT, 'shim', 'sdk_include'),
           '-I', os.path.join(OUT, 'include'), '-I', os.path.join(MSC, 'include'),
           '-include', os.path.join(PORT, 'shim', 'sdk_include', 'port_sdk_prefix.h'),
           '-DBUILD_VERSION=3', '-DVERSION_R4QJ01', src, '-o', o]
    p = subprocess.run(cmd, cwd=PORT, capture_output=True, text=True, errors='replace', creationflags=0x08000000,
                       env=dict(os.environ, ZIG_LOCAL_CACHE_DIR=os.path.join(PORT, 'build', 'zigcache')))
    log = p.stdout + p.stderr
    open(os.path.join(OBJ, b + '.log'), 'w', encoding='utf-8').write(log)
    errs = re.findall(r' error: (.*)', log)
    return b, len(errs), errs[0] if errs else ''


def main():
    os.makedirs(OBJ, exist_ok=True)
    print('sdk prep: %d files rewritten' % prep())
    zig = trial.find_zig()
    srcs = sorted(p for lib in LIBS for p in glob.glob(os.path.join(OUT, 'src', 'RVL_SDK', lib, '*.c')))
    with concurrent.futures.ThreadPoolExecutor(8) as ex:
        res = list(ex.map(lambda s: compile_one(zig, s), srcs))
    for b, n, e in res:
        print('%-18s %3d %s' % (b, n, e[:100]))
    print('sdk units %d, clean %d' % (len(res), sum(1 for r in res if r[1] == 0)))


if __name__ == '__main__':
    main()
