"""Group the trial's remaining compile errors into docs/SOURCE_NOTES.md (write-ups for the decomp sessions)."""
import collections, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.normpath(os.path.join(HERE, '..', '..'))
OUT = os.path.join(HERE, 'out')

# (key, regex on the error text, advice)
CATS = [
    ('templates-in-extern-C', r'templates must have C\+\+ linkage',
     'Templates (or template helpers) sit inside an `extern "C" {` block. MWCC allows it; standard C++ does not. '
     'Close the extern "C" block before the template (matching is unaffected: templates are C++ anyway).'),
    ('linkage-mismatch', r'has a different language linkage',
     'A symbol is declared both inside and outside `extern "C"` in the same unit (usually an `extern` re-declared '
     'locally). Declare it once, with one linkage.'),
    ('no-matching-call', r'no matching function for call to',
     'Usually a temporary/rvalue passed to a non-const reference or pointer parameter, or a call that relies on '
     'MWCC\'s looser conversions. See the candidate notes in the log; often `const T&` on the helper fixes it.'),
    ('placement-new-const', r'placement new expression with a const-qualified argument',
     '`new ("tag") T` where the unit does not see the game\'s `operator new(size_t, const char*)`, so clang picks the '
     'standard placement new. Declare the tagged operator new (with size_t) before use.'),
    ('switch-jump-over-init', r'cannot jump from switch statement to this case label',
     'A `case` label jumps past an initialised local declared in an earlier case. Wrap that case body in `{ }`.'),
    ('conflicting-types', r'conflicting types for',
     'Two different prototypes for the same function in one unit (often int vs s32/u32 or a header vs a local '
     'extern). Make them agree.'),
    ('out-of-line-mismatch', r'out-of-line definition of .* does not match|return type of out-of-line definition',
     'A member definition does not match its declaration (types differ, e.g. long vs s32).'),
    ('implicit-function', r'implicit function declaration|call to undeclared',
     'C file calls a function with no prototype in scope. Add the prototype.'),
    ('address-of-temporary', r'address of a temporary|address of an rvalue',
     'Taking the address of a temporary (MWCC allows it). Use a named local.'),
    ('return-type-overload', r'differ only in their return type',
     'Two declarations of the same function with different return types.'),
    ('asm-block', r"expected '\(' after 'asm'|expected.*asm|unknown type name 'asm'",
     'MWCC inline `asm` function. Needs a C equivalent under `#ifndef __MWERKS__` (or a port-side replacement).'),
]


def main():
    by = collections.OrderedDict((c[0], collections.defaultdict(list)) for c in CATS)
    other = collections.defaultdict(list)
    for f in sorted(os.listdir(OUT)):
        if not f.endswith('.log'):
            continue
        for line in open(os.path.join(OUT, f), encoding='utf-8', errors='replace'):
            m = re.search(r'[\\/]([\w.]+\.(?:cpp|c|h)):(\d+):\d+: error: (.*)', line)
            if not m or 'too many errors' in line:
                continue
            fn, ln, msg = m.groups()
            for key, rx, _ in CATS:
                if re.search(rx, msg):
                    by[key][fn].append(ln)
                    break
            else:
                other[fn].append('%s: %s' % (ln, msg[:120]))
    w = ['# Source notes for the decomp sessions (port trial, generated)', '',
         'From `port/tools/gametrial` (every src/game unit compiled for 32-bit x86 Windows with zig/clang). These are',
         'places where the decomp source relies on MWCC-only behaviour. Each fix is meant to be matching-neutral;',
         'please verify with the usual build before committing. Line numbers are as of the trial run.', '',
         'Already handled port-side without source changes (no action needed): `__declspec(section ...)`, MWCC',
         'intrinsics (`__fabs`, `__fabsf`, `__abs`, `__cntlzw`), `operator new(unsigned long, ...)` (rewritten to',
         'size_t in the port copy), decomp/compat.h clang branch (the port has its own types.h), the GX FIFO macro',
         '(0xCC008000 -> host FIFO object), and the little-endian byte-swap helpers (no-ops on an x86 host).', '',
         '## Behaviour differences found at run time (port patches in port/patches; please fix in the decomp)', '',
         '- `res.cpp` fn_8012A648: `lbl_8055E008->Free(AsyncResource::operator new(...))`: the argument creates the',
         '  pool that lbl_8055E008 points to. MWCC evaluates the argument first; C++17 compilers load the (null)',
         '  pointer first. Matching-neutral fix: `void* p = AsyncResource::operator new(...); lbl_8055E008->Free(p);`',
         '  (check the match).',
         '- `mrender.cpp` declares `Pool::Pool(const char*, long, long)`; pool.cpp defines it with s32. Same type on',
         '  MWCC, different mangled names elsewhere: use s32 in mrender.cpp.', '']
    for key, rx, advice in CATS:
        units = by[key]
        if not units:
            continue
        n = sum(len(v) for v in units.values())
        w += ['## %s: %d sites in %d files' % (key, n, len(units)), '', advice, '']
        for fn in sorted(units):
            lines = sorted(set(units[fn]), key=int)
            w.append('- `%s`: lines %s' % (fn, ', '.join(lines[:30]) + (' ...' if len(lines) > 30 else '')))
        w.append('')
    if other:
        w += ['## other', '']
        for fn in sorted(other):
            for e in other[fn][:8]:
                w.append('- `%s` %s' % (fn, e))
        w.append('')
    open(os.path.join(PORT, 'docs', 'SOURCE_NOTES.md'), 'w', encoding='utf-8').write('\n'.join(w))
    print('\n'.join(l for l in w if l.startswith('## ')))


if __name__ == '__main__':
    main()
