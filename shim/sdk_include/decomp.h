/* Port version of libs/msc/include/decomp.h (found first on the SDK include path): same helpers, with the
   MWCC-only placement attributes neutralised for the host. */
#ifndef DECOMP_H
#define DECOMP_H

#define ROUNDUP(x, align) (((x) + ((align) - 1)) & ~((align) - 1))
#define ROUNDDOWN(x, align) ((x) & ~((align) - 1))
#define PTR_ROUNDUP(x, align) ((void*)ROUNDUP((unsigned long)(x), (align)))
#define FOURCC(a, b, c, d) ((u32)(a) << 24 | (u32)(b) << 16 | (u32)(c) << 8 | (u32)(d))
#define ASM(...)
#define ASM_VOLATILE(...)
#define __CONCAT(x, y) x##y
#define CONCAT(x, y) __CONCAT(x, y)
#define ROUND_UP(x, align) ROUNDUP((x), (align))
#define ROUND_DOWN_PTR(x, align) ((void*)ROUNDDOWN((unsigned long)(x), (align)))
#define ROUND_UP_PTR(x, align) PTR_ROUNDUP((x), (align))
#define ARRAY_COUNT(arr) (s32)(sizeof(arr) / sizeof(arr[0]))
#define ARRAY_LENGTH(x) (sizeof(x) / sizeof(x[0]))
#define CLAMP(low, high, x) ((x) > (high) ? (high) : ((x) < (low) ? (low) : (x)))
#define DECL_SECTION(x)
#define DECL_WEAK __attribute__((weak))
#define DECOMP_INLINE inline
#define DECOMP_DONT_INLINE __attribute__((noinline))
#define DECOMP_FORCEACTIVE(module, ...)
#define AT_ADDRESS(address)
#endif
