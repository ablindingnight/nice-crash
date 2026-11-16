// GX texture-object getters/setters the port's SDK build does not provide (the RVL_SDK copy it compiles lacks them).
// Field layout = the SDK's __GXTexObjInt / __GXTlutObjInt (GXInternal.h); register fields per public GX docs.
#include "sdk.h"

namespace {
struct TexObjInt {
    u32 mode0, mode1, image0, image3;
    void* userData;
    u32 fmt, tlutName;
    u16 loadCnt;
    u8 loadFmt, flags;
};
struct TlutObjInt {
    u32 tlut, loadTlut0;
    u16 numEntries;
};
const u8 kFilt[6] = {0x00, 0x04, 0x01, 0x05, 0x02, 0x06};  // GX min filter -> hardware encoding
}  // namespace

extern "C" {

// Physical address of the image (Excite Truck's SDK reads a 24-bit field, so MEM2 addresses survive).
void* GXGetTexObjData(const void* obj) {
    const TexObjInt* t = (const TexObjInt*)obj;
    return (void*)(uintptr_t)((t->image3 & 0xFFFFFF) << 5);
}

u16 GXGetTlutObjNumEntries(const void* obj) { return ((const TlutObjInt*)obj)->numEntries; }

void GXInitTexObjFilter(void* obj, int minFilt, int magFilt) {
    TexObjInt* t = (TexObjInt*)obj;
    t->mode0 = (t->mode0 & ~(1u << 4)) | (u32)(magFilt == 1) << 4;
    t->mode0 = (t->mode0 & ~(7u << 5)) | (u32)kFilt[minFilt >= 0 && minFilt < 6 ? minFilt : 0] << 5;
}

}  // extern "C"
