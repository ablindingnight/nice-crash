// MWCC runtime entry points that some decomp units call by their MWCC-mangled names (written that way so the
// matching build links them like retail): tagged new/new[]/delete, array construction/destruction, and the
// float conversion helpers. Forwarded to the game's own operators (mem.cpp) and to plain C.
#include <cstddef>
#include <cstdint>

void* operator new(size_t size, const char* tag);    // game mem.cpp
void* operator new[](size_t size, const char* tag);  // game mem.cpp

typedef void (*CtorFn)(void* obj);
typedef void (*DtorFn)(void* obj, int destroy);

extern "C" {

void* __nw__FUlPCc(unsigned int size, const char* tag) { return operator new(size, tag); }
void* __nwa__FUlPCc(unsigned int size, const char* tag) { return operator new[](size, tag); }
void __dl__FPv(void* p) { operator delete(p); }
void __dla__FPv(void* p) { operator delete[](p); }

// MWCC: construct `n` objects of `size` bytes at `block` with `ctor` (dtor is for unwinding, unused here).
void __construct_array(void* block, CtorFn ctor, DtorFn dtor, unsigned int size, unsigned int n) {
    (void)dtor;
    if (!ctor) return;
    for (unsigned int i = 0; i < n; i++) ctor((char*)block + i * size);
}

// MWCC: destroy `n` objects of `size` bytes at `block`, last to first, with `dtor(obj, -1)` (no deallocation).
void __destroy_arr(void* block, DtorFn dtor, unsigned int size, unsigned int n) {
    if (!dtor) return;
    for (unsigned int i = n; i-- > 0;) dtor((char*)block + i * size, -1);
}

// MWCC conversion helpers.
unsigned int __cvt_fp2unsigned(double v) {
    if (v <= 0.0) return 0;
    if (v >= 4294967295.0) return 0xFFFFFFFFu;
    return (unsigned int)v;
}
float __cvt_sll_flt(long long v) { return (float)v; }

}  // extern "C"
