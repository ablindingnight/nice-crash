// NC_ENV("NAME"): getenv read once per call site (the renderer's debug switches are checked per draw and per
// copy; getenv_s was 12% of the race's game thread).
#pragma once
#include <cstdlib>
#define NC_ENV(name) ([]() -> const char* { static const char* v = std::getenv(name); return v; }())
