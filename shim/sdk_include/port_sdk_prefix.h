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
/* `*(u32*)&color` reads/writes of 4-byte colours: big-endian on the Wii (r in the top byte). sdkbuild.py rule
   colour-pun routes them through these so the register fields come out the same on the little-endian host. */
static inline unsigned int port_be_ld32(const void* p) {
    const unsigned char* b = (const unsigned char*)p;
    return (unsigned int)b[0] << 24 | (unsigned int)b[1] << 16 | (unsigned int)b[2] << 8 | b[3];
}
static inline void port_be_st32(void* p, unsigned int v) {
    unsigned char* b = (unsigned char*)p;
    b[0] = (unsigned char)(v >> 24); b[1] = (unsigned char)(v >> 16); b[2] = (unsigned char)(v >> 8); b[3] = (unsigned char)v;
}
