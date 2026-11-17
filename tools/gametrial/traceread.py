"""Prints a NICECRASH_TRACE ring (runtime/trace.cpp) in order: the renderer's last steps before the process ended."""
import struct, sys
d = open(sys.argv[1], 'rb').read()
n = struct.unpack_from('<i', d, 0)[0]
slots = [d[64 + i * 64:64 + (i + 1) * 64].split(b'\0')[0].decode(errors='replace') for i in range(512)]
first = max(0, n - 512)
count = int(sys.argv[2]) if len(sys.argv) > 2 else 60
for i in range(max(first, n - count), n):
    print(slots[i % 512])
