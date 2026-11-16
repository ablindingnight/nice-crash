/* Port prefix for the msc (Strikers-derived) half of the HOME button library (tools/gametrial/hbmbuild.py). */
#pragma once
#ifdef __cplusplus
#include <cstdarg>
#include <cstddef>
#include <cstring>
namespace std { using ::va_list; }
#endif
#ifndef DEG_TO_RAD
#define DEG_TO_RAD(degrees) (degrees * (3.14159265358979323846 / 180.0f))
#endif
/* nw4hbm::ut::FileStream names its seek origins SEEK_BEG/SEEK_CUR/SEEK_END: the host's <stdio.h> macros must go. */
#include <stdio.h>
#undef SEEK_CUR
#undef SEEK_END
#undef SEEK_SET
#include <revolution/types.h>
#ifndef PORT_REGISTER_T
#define PORT_REGISTER_T
typedef unsigned int register_t;  /* dropped from the SDK's types.h (host libc name), still used by db_assert */
#endif
#define __alloca __builtin_alloca
/* MWCC lets friend-less classes reach some private members (nw4hbm::snd sound handles); the host build opens
   them. Host C++ headers are already included above, so only the library's own classes see this. */
#define private public
