/* Force-included before every decomp SDK file compiled for the host (tools/gametrial/sdkbuild.py). */
#pragma once
/* PowerPC intrinsics MWCC provides. Bit numbering as on PPC: bit 0 is the most significant. */
static inline unsigned int __port_ppcmask(int mb, int me) {
    unsigned int m1 = 0xFFFFFFFFu >> mb, m2 = 0xFFFFFFFFu << (31 - me);
    return mb <= me ? (m1 & m2) : (m1 | m2);
}
static inline unsigned int __port_rotl(unsigned int v, int sh) { sh &= 31; return sh ? (v << sh) | (v >> (32 - sh)) : v; }
static inline unsigned int __rlwimi(unsigned int a, unsigned int b, int sh, int mb, int me) {
    unsigned int m = __port_ppcmask(mb, me);
    return (__port_rotl(b, sh) & m) | (a & ~m);
}
static inline unsigned int __rlwinm(unsigned int a, int sh, int mb, int me) { return __port_rotl(a, sh) & __port_ppcmask(mb, me); }
static inline unsigned int __cntlzw(unsigned int x) { return x ? (unsigned int)__builtin_clz(x) : 32u; }
