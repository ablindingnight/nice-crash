// Port replacement for excite-truck/include/types.h (found first on the include path).
// Same typedefs; skips decomp/compat.h, whose clang branch blanks __attribute__/asm/register for editor
// parsers and breaks the system headers, and lang.h (noexcept/override macros clash with C++11).
#ifndef TYPES_H
#define TYPES_H

#ifdef __cplusplus
#include <cstdarg>
#include <cstddef>
#include <new>
#else
#include <stdarg.h>
#include <stddef.h>
#endif

#include <decomp/decomp.h>
#include <macros.h>

// 32-bit types as int, not long: the same size as the Wii's `unsigned long`, but it makes u32 == size_t on
// x86 so the game's `operator new(u32, ...)`, qsort/memset prototypes etc. match the host's declarations.
typedef unsigned long long u64;
typedef signed long long s64;
typedef unsigned int u32;
typedef signed int s32;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned char u8;
typedef signed char s8;
typedef float f32;
typedef double f64;

typedef int UNKWORD;
typedef void UNKTYPE;

enum { FALSE, TRUE };
typedef int BOOL;

typedef void (*funcptr_t)(void);

#define ASM(...)
#define ASM_VOLATILE(...)
#define DECL_ADDRESS(...)
#define FLEXIBLE_ARRAY(NAME) NAME[]
#define DECLTYPE(x) decltype(x)

#endif
