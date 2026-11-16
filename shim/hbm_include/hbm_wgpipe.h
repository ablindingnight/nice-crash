// The GX write-gather pipe for the HOME button library (tools/gametrial/hbmbuild.py): both of its header sets write
// vertices and commands to a union at 0xCC008000 (GXWGFifo.f32 = x, WGPIPE.uc = v, ...). On the host that address
// is nothing; this object takes the same member names and appends each value to the port's GX FIFO
// (sdk/gxfifo.cpp), in the GPU's byte order, as the game's own writes do (shim/include/port_fifo.h).
#pragma once
#ifdef __cplusplus
extern "C" void port_fifo_u8(unsigned char v);
extern "C" void port_fifo_u16(unsigned short v);
extern "C" void port_fifo_u32(unsigned int v);
extern "C" void port_fifo_f32(float v);

struct PortHbmWGPipe {
    struct B8 { void operator=(unsigned char v) const { port_fifo_u8(v); } };
    struct B16 { void operator=(unsigned short v) const { port_fifo_u16(v); } };
    struct B32 { void operator=(unsigned int v) const { port_fifo_u32(v); } };
    struct BP { void operator=(const void* v) const { port_fifo_u32((unsigned int)(unsigned long)v); } };
    struct BF { void operator=(float v) const { port_fifo_f32(v); } };
    B8 c, uc, u8, s8;
    B16 s, us, u16, s16;
    B32 i, ui, u, u32, s32;
    BP p;
    BF f, f32;
};
static const PortHbmWGPipe port_hbm_wgpipe = {};
#endif
