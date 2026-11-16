// The GX write-gather pipe (0xCC008000) as a host object. Game code writes `WGPIPE.f = x;` etc.; prep.py
// rewrites the fixed-address macro to `port_wgpipe`, and each member's assignment appends the value to the host
// GX FIFO in the GPU's big-endian byte order (sdk/gxfifo.cpp interprets the stream).
#pragma once
#ifdef __cplusplus
extern "C" void port_fifo_u8(unsigned char v);
extern "C" void port_fifo_u16(unsigned short v);
extern "C" void port_fifo_u32(unsigned int v);
extern "C" void port_fifo_f32(float v);

struct PortWGPipe {
    struct U8 { void operator=(unsigned char v) const { port_fifo_u8(v); } } uc, c;
    struct U16 { void operator=(unsigned short v) const { port_fifo_u16(v); } } us, s;
    struct U32 { void operator=(unsigned int v) const { port_fifo_u32(v); } } u, i, ui;
    struct F32 { void operator=(float v) const { port_fifo_f32(v); } } f;
    // GXFastCallDisplayList: the list address (Wii memory is mapped at the same host address, 32-bit build)
    struct P { void operator=(void* v) const { port_fifo_u32((unsigned int)(unsigned long)v); } } p;
};
static const PortWGPipe port_wgpipe = {};
#endif
