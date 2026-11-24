"""Check a tester package for anything that came from the disc or main.dol (docs/TESTER_PACKAGE.md).

    python -I tools/package/scan_package.py <package dir> [--dol main.dol] [--disc extracted-DATA-dir]

Without --dol/--disc only the name rules run. With them (the developer's own dump, read only):
  1. names: no file named like a disc file (main.dol, *.rel, tocres.res, ...) and no disc-image/DOL/REL extension;
  2. whole files: no package file has the size and SHA-1 of any disc file;
  3. bytes: no 32-byte run of main.dol's sections (4-byte aligned, at least 12 distinct byte values, so padding and
     simple patterns don't count) appears anywhere in a package file we made. Printable-ASCII runs inside source
     files are the code's own string literals (format strings, names) and are counted, not failed. The third-party toolchains
     (tools/zig, tools/python, tools/realesrgan, tools/dtk) are copied verbatim from their publishers and only get
     checks 1 and 2;
  4. source tables: initializers whose literals equal 8+ consecutive big-endian words of a main.dol data section
     with 4+ distinct values >= 0x10000 (floats as bits; string characters don't count). 64+ values fail (a data
     table pasted into source); shorter ones (vtables, SDK constant tables, tuning-float structs: code constants
     every decompiled source has) are printed for review.
Exit status 1 if anything is found.
"""
import argparse, hashlib, os, re, struct, sys

DISC_EXT = {'.dol', '.rel', '.iso', '.rvz', '.wia', '.wbfs', '.ciso', '.gcz', '.nfs', '.wad', '.tik', '.tmd',
            '.thp', '.brsar', '.brstm', '.arc', '.szs', '.tpl', '.bnr'}
DISC_NAMES = {'main.dol', 'boot.bin', 'bi2.bin', 'apploader.img', 'fst.bin', 'tocres.res', 'ticket.bin', 'tmd.bin',
              'cert.bin', 'h3.bin'}
THIRD_PARTY = [os.path.join('build', 'tools', d) for d in ('zig', 'python', 'realesrgan', 'dtk')]
TEXT_EXT = {'.c', '.cpp', '.cp', '.h', '.hpp', '.inc', '.s', '.py', '.txt', '.md', '.yml', '.json', '.tsv'}
LIT = re.compile(r'-?(?:0[xX][0-9a-fA-F]+[uUlL]*|\d+\.\d*(?:[eE][-+]?\d+)?[fF]?|\d*\.\d+(?:[eE][-+]?\d+)?[fF]?|\d+[uUlL]*)')


def lit(x):
    """A C numeric literal as the 32-bit word it would be stored as (floats as their single-precision bits)."""
    try:
        if '.' in x and not x.lower().startswith(('0x', '-0x')):
            return struct.unpack('>I', struct.pack('>f', float(x.rstrip('fF'))))[0]
        return int(x.rstrip('uUlL'), 0) & 0xFFFFFFFF
    except (ValueError, OverflowError):
        return None


TABLE_FAIL = 64


def ascii_word(x):
    """A word that is four characters of a string (vtables carry their pure-virtual message this way)."""
    b = struct.pack('>I', x)
    return sum(1 for c in b if c) >= 3 and all(32 <= c < 127 or c == 0 for c in b)


def counting(c):
    """32 bytes that are an arithmetic progression of 16- or 32-bit words (index lists, glyph ids): not data."""
    for size, fmt in ((2, 'H'), (4, 'I')):
        for end in '<>':
            w = struct.unpack(end + fmt * (32 // size), c)
            d = w[1] - w[0]
            if all(w[k + 1] - w[k] == d for k in range(len(w) - 1)):
                return True
    return False


def sha1(p):
    h = hashlib.sha1()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def dol_sections(b):
    out = []
    for i in range(18):
        off, addr, size = (struct.unpack_from('>I', b, x)[0] for x in (i * 4, 0x48 + i * 4, 0x90 + i * 4))
        if size:
            out.append(b[off:off + size])
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('pkg')
    ap.add_argument('--dol')
    ap.add_argument('--disc')
    a = ap.parse_args()
    pkg = os.path.abspath(a.pkg)
    files = []
    for r, _, fs in os.walk(pkg):
        for f in fs:
            files.append(os.path.join(r, f))
    problems, review = [], []

    def rel(p):
        return os.path.relpath(p, pkg)

    def third(p):
        return any(rel(p).startswith(t + os.sep) for t in THIRD_PARTY)

    # 1. names
    disc_names = set(DISC_NAMES)
    disc_files = []
    if a.disc:
        for r, _, fs in os.walk(a.disc):
            for f in fs:
                disc_files.append(os.path.join(r, f))
                disc_names.add(f.lower())
    generic = {'readme.txt', 'license.txt', 'readme.md'}
    for p in files:
        n = os.path.basename(p).lower()
        if os.path.splitext(n)[1] in DISC_EXT or (n in disc_names and n not in generic):
            problems.append('name: %s looks like a disc file' % rel(p))
    print('scan 1 names        %d files checked' % len(files), flush=True)

    # 2. whole-file hashes against the disc
    if disc_files:
        by_size = {}
        for d in disc_files:
            by_size.setdefault(os.path.getsize(d), []).append(d)
        cache = {}
        hits = 0
        for p in files:
            s = os.path.getsize(p)
            if s < 16 or s not in by_size:
                continue
            h = sha1(p)
            for d in by_size[s]:
                if d not in cache:
                    cache[d] = sha1(d)
                if cache[d] == h:
                    problems.append('file: %s is the disc file %s' % (rel(p), os.path.relpath(d, a.disc)))
                    hits += 1
        print('scan 2 hashes       %d disc files, %d same-size candidates hashed' % (len(disc_files), len(cache)),
              flush=True)
    if a.dol and os.path.exists(a.dol):
        dol = open(a.dol, 'rb').read()
        dh = hashlib.sha1(dol).hexdigest()
        for p in files:
            if os.path.getsize(p) == len(dol) and sha1(p) == dh:
                problems.append('file: %s is main.dol' % rel(p))

        # 3. 32-byte runs of the DOL in our own files. Data sections also in host byte order (each 4-byte and each
        # 2-byte element swapped): a table compiled into an x86 exe is stored little-endian (the exes before
        # 2026-11-23 held the DUMP_DATA tables that way and passed the big-endian check).
        # In our compiled files (.exe/.dll, which packages ship since the prebuilt exe) a run fails when it lies in a
        # table the decomp generates from main.dol (DUMP_DATA, the MP3 decoder's; ranges in build/dumptables/*.tsv,
        # written by tools/gametrial/dumptables.py): those must be read at run time. Other runs there are tables the
        # decomp writes as source (nw4hbm's debug font and ASCII table, envelope tables) or runtime-library
        # constants (compiler_rt, mingw powers of ten): listed for review. In any other file every run fails.
        # Not the last two data sections, .sdata and .sdata2: MWCC puts only objects of 8 bytes or less there, so a
        # run of them is several separate scalars, the compiler's constants from the source (betarace.cpp's named
        # floats, pooled in the same order by our compiler), never a table.
        gen = []
        mdir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'build', 'dumptables')
        if os.path.isdir(mdir):
            for f in os.listdir(mdir):
                if f.endswith('.tsv'):
                    for line in open(os.path.join(mdir, f), encoding='utf-8'):
                        q = line.split('\t')
                        if len(q) > 2:
                            gen.append((int(q[1], 16), int(q[2], 16)))
        else:
            print('scan 3: no build/dumptables (run the game build first): exe runs are only listed')
        chunks = {}  # run -> (Wii address, host byte order?)
        data_secs = [k for k in range(7, 18) if struct.unpack_from('>I', dol, 0x90 + k * 4)[0]]
        for k in range(18):
            off, addr, size = (struct.unpack_from('>I', dol, x)[0] for x in (k * 4, 0x48 + k * 4, 0x90 + k * 4))
            sec = dol[off:off + size]
            forms = [(sec, False)]
            if k in data_secs[:-2]:
                n4 = len(sec) // 4 * 4
                forms += [(b''.join(sec[j:j + 4][::-1] for j in range(0, n4, 4)), True),
                          (b''.join(sec[j:j + 2][::-1] for j in range(0, n4, 2)), True)]
            for f, swapped in forms:
                for i in range(0, len(f) - 31, 4):
                    c = f[i:i + 32]
                    if len(set(c)) >= 12 and not counting(c):
                        if swapped:
                            chunks.setdefault(c, (addr + i, True))
                        else:
                            chunks[c] = (addr + i, False)
        # Tables computed from a published formula, not game data: the CRC-32 tables (MSB-first 0x04C11DB7, which the
        # game's lbl_802F8F08 is and lib/crc.cpp generates; the reflected 0xEDB88320), in either byte order.
        std_runs = set()
        for poly, msb in ((0x04C11DB7, True), (0xEDB88320, False)):
            tab = []
            for v in range(256):
                x = v << 24 if msb else v
                for _ in range(8):
                    if msb:
                        x = ((x << 1) ^ poly if x & 0x80000000 else x << 1) & 0xFFFFFFFF
                    else:
                        x = (x >> 1) ^ poly if x & 1 else x >> 1
                tab.append(x)
            for fmt in ('>256I', '<256I'):
                tb = struct.pack(fmt, *tab)
                std_runs.update(tb[k:k + 32] for k in range(0, len(tb) - 31, 4))
        n = strings = standard = 0
        exe_review = []
        for p in files:
            if third(p):
                continue
            b = open(p, 'rb').read()
            n += 1
            i = 0
            while i < len(b) - 31:
                c = b[i:i + 32]
                if c in chunks:
                    if all(32 <= x < 127 or x in (0, 9, 10, 13) for x in c) and sum(32 <= x < 127 for x in c) >= 20:
                        # A string literal of the code (format strings, names, version strings), in source or in
                        # our exes (lib/res.cpp shares the game's resource error messages): code text. Counted.
                        strings += 1
                        i += 32
                        continue
                    if c in std_runs:
                        standard += 1
                        i += 32
                        continue
                    wa, swapped = chunks[c]
                    order = 'host byte order' if swapped else 'Wii byte order'
                    generated = any(ga - 32 < wa < ga + gn for ga, gn in gen)
                    if p.lower().endswith(('.exe', '.dll')) and not generated:
                        exe_review.append('%s offset 0x%x = main.dol 0x%08X (%s)' % (rel(p), i, wa, order))
                        i += 32
                        continue
                    where = ' (a generated table: must be read at run time)' if generated else ''
                    problems.append('bytes: %s offset 0x%x = main.dol 0x%08X, %s%s' % (rel(p), i, wa, order, where))
                    break
                i += 1
        print('scan 3 bytes        %d of our files against %d DOL chunks; %d string-literal runs '
              '(code text, allowed); %d standard CRC-table runs (allowed); %d runs in our exes outside generated '
              'tables (review)' % (n, len(chunks), strings, standard, len(exe_review)), flush=True)
        for s in exe_review[:8]:
            print('  review: ' + s)
        if len(exe_review) > 8:
            print('  review: ... %d more' % (len(exe_review) - 8))

        # 4. numeric tables in source: initializers (`= { ... }`) whose values equal consecutive words of a DOL
        # data section. Float literals count as their bits. Code constants and small-number tables (month lengths,
        # identity matrices) are ignored: a hit needs 8+ consecutive words with 4+ distinct values >= 0x10000.
        dwords = {}
        for k, sec in enumerate(dol_sections(dol)):
            if k < 7:
                continue  # text sections
            for i in range(0, len(sec) - 7, 4):
                dwords.setdefault(struct.unpack_from('>I', sec, i)[0], set()).add(struct.unpack_from('>I', sec, i + 4)[0])
        t = 0
        for p in files:
            if third(p) or os.path.splitext(p)[1].lower() not in TEXT_EXT:
                continue
            t += 1
            text = open(p, encoding='utf-8', errors='replace').read()
            for m in re.finditer(r'=\s*\{([^{};]{40,})\}', text):
                vals = [lit(x) for x in LIT.findall(m.group(1))]
                best, start = [], 0
                for i in range(len(vals)):
                    ok = (i > start and vals[i] is not None and vals[i - 1] is not None and
                          vals[i - 1] in dwords and vals[i] in dwords[vals[i - 1]])
                    if not ok:
                        start = i
                    elif i + 1 - start > len(best):
                        best = vals[start:i + 1]
                big = {x for x in best if x >= 0x10000 and not ascii_word(x)}
                if len(best) < 8 or len(big) < 4:
                    continue
                line = text.count('\n', 0, m.start()) + 1
                msg = 'table: %s:%d: %d values equal consecutive main.dol data words' % (rel(p), line, len(best))
                # A long table is data; a short initializer (a vtable, an SDK constant table, a struct of tuning
                # floats) is a code constant of the decompiled source: listed for review, not failed.
                (problems if len(best) >= TABLE_FAIL else review).append(msg)
        print('scan 4 tables       %d text files' % t, flush=True)
        if review:
            print('\nreview (short initializers in the decompiled source, %d; fail at %d+ values):' %
                  (len(review), TABLE_FAIL))
            for x in review:
                print('  ' + x)
    else:
        print('scan 3/4 skipped    (no --dol)', flush=True)

    if problems:
        print('\nFOUND %d problem(s):' % len(problems))
        for x in problems[:200]:
            print('  ' + x)
        return 1
    print('\nclean: no file or byte run from the disc or main.dol')
    return 0


if __name__ == '__main__':
    sys.exit(main())
