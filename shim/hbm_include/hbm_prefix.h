/* Port prefix for the HOME button library (tools/gametrial/hbmbuild.py): host C++ headers the CodeWarrior
   library assumes (MSL puts va_list etc. in std). */
#pragma once
#ifdef __cplusplus
#include <cstdarg>
#include <cstddef>
#include <cstring>
namespace std { using ::va_list; }
#endif
#include <global.h>
/* MSL <cmath> */
#ifndef DEG_TO_RAD
#define DEG_TO_RAD(degrees) (degrees * (3.14159265358979323846 / 180.0f))
#endif
