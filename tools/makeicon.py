"""Build the Nice Crash icon from the source artwork (standard library only).

assets/icon/nicecrash_source.png (the X on a white background) ->
  assets/icon/nicecrash.png  (1024x1024, transparent background)
  assets/icon/nicecrash.ico  (16..256 px, PNG-compressed entries; for the exe, the window/taskbar and an installer)

The background is the white area connected to the image border (flood fill), so white highlights inside the X stay.
Edge pixels between the yellow outline and the background get partial alpha from how white they are.
usage: python makeicon.py
"""
import os, struct, zlib
from collections import deque

HERE = os.path.dirname(os.path.abspath(__file__))
ICON = os.path.normpath(os.path.join(HERE, '..', 'assets', 'icon'))


def read_png(path):
    b = open(path, 'rb').read()
    assert b[:8] == b'\x89PNG\r\n\x1a\n'
    pos, idat, w = 8, b'', 0
    while pos < len(b):
        n, typ = struct.unpack('>I4s', b[pos:pos + 8])
        data = b[pos + 8:pos + 8 + n]
        if typ == b'IHDR':
            w, h, depth, ctype, _, _, interlace = struct.unpack('>IIBBBBB', data)
            assert depth == 8 and ctype in (2, 6) and interlace == 0, 'unsupported PNG'
        elif typ == b'IDAT':
            idat += data
        pos += 12 + n
    bpp = 4 if ctype == 6 else 3
    raw = zlib.decompress(idat)
    stride = w * bpp
    out = bytearray(h * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            c = prev[i - bpp] if i >= bpp else 0
            up = prev[i]
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + up) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + up) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(up - c), abs(a - c), abs(a + up - 2 * c)
                pr = a if pa <= pb and pa <= pc else (up if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    rgba = bytearray(w * h * 4)
    for i in range(w * h):
        rgba[i * 4:i * 4 + 3] = out[i * bpp:i * bpp + 3]
        rgba[i * 4 + 3] = out[i * bpp + 3] if bpp == 4 else 255
    return w, h, rgba


def write_png(w, h, rgba):
    raw = b''.join(b'\0' + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def remove_background(w, h, px):
    # Whiteness: how close to white (0..255). Background = near-white pixels connected to the border.
    def white(i):
        r, g, b = px[i * 4], px[i * 4 + 1], px[i * 4 + 2]
        return min(r, g, b)
    bg = bytearray(w * h)
    q = deque()
    for x in range(w):
        for y in (0, h - 1):
            q.append(y * w + x)
    for y in range(h):
        for x in (0, w - 1):
            q.append(y * w + x)
    while q:
        i = q.popleft()
        if bg[i] or white(i) < 225:
            continue
        bg[i] = 1
        x, y = i % w, i // w
        if x > 0: q.append(i - 1)
        if x < w - 1: q.append(i + 1)
        if y > 0: q.append(i - w)
        if y < h - 1: q.append(i + w)
    for i in range(w * h):
        if bg[i]:
            px[i * 4 + 3] = 0
        else:
            # pixels touching the background fade with their whiteness (anti-aliased edge)
            x, y = i % w, i // w
            edge = (x > 0 and bg[i - 1]) or (x < w - 1 and bg[i + 1]) or (y > 0 and bg[i - w]) or (y < h - 1 and bg[i + w])
            if edge:
                wv = white(i)
                px[i * 4 + 3] = max(0, min(255, (255 - wv) * 255 // 60)) if wv > 195 else 255
    return px


def crop_square(w, h, px, margin=0.02):
    xs = [i % w for i in range(w * h) if px[i * 4 + 3]]
    ys = [i // w for i in range(w * h) if px[i * 4 + 3]]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    side = int(max(x1 - x0 + 1, y1 - y0 + 1) * (1 + 2 * margin))
    cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
    out = bytearray(side * side * 4)
    for y in range(side):
        sy = cy - side // 2 + y
        if 0 <= sy < h:
            for x in range(side):
                sx = cx - side // 2 + x
                if 0 <= sx < w:
                    out[(y * side + x) * 4:(y * side + x) * 4 + 4] = px[(sy * w + sx) * 4:(sy * w + sx) * 4 + 4]
    return side, out


def resize(side, px, n):
    """Box filter with premultiplied alpha (no dark fringes)."""
    out = bytearray(n * n * 4)
    for y in range(n):
        y0, y1 = y * side // n, max(y * side // n + 1, (y + 1) * side // n)
        for x in range(n):
            x0, x1 = x * side // n, max(x * side // n + 1, (x + 1) * side // n)
            r = g = b = a = cnt = 0
            for sy in range(y0, y1):
                base = sy * side
                for sx in range(x0, x1):
                    i = (base + sx) * 4
                    al = px[i + 3]
                    r += px[i] * al
                    g += px[i + 1] * al
                    b += px[i + 2] * al
                    a += al
                    cnt += 1
            o = (y * n + x) * 4
            if a:
                out[o:o + 4] = bytes((r // a, g // a, b // a, a // cnt))
    return out


def write_ico(path, images):
    head = struct.pack('<HHH', 0, 1, len(images))
    dirs, blobs, off = b'', b'', 6 + 16 * len(images)
    for n, png in images:
        dirs += struct.pack('<BBBBHHII', n % 256, n % 256, 0, 0, 1, 32, len(png), off)
        blobs += png
        off += len(png)
    open(path, 'wb').write(head + dirs + blobs)


def main():
    w, h, px = read_png(os.path.join(ICON, 'nicecrash_source.png'))
    px = remove_background(w, h, px)
    side, sq = crop_square(w, h, px)
    big = resize(side, sq, 1024)
    open(os.path.join(ICON, 'nicecrash.png'), 'wb').write(write_png(1024, 1024, big))
    images = []
    for n in (256, 128, 64, 48, 32, 24, 16):
        images.append((n, write_png(n, n, resize(1024, big, n))))
    write_ico(os.path.join(ICON, 'nicecrash.ico'), images)
    print('wrote nicecrash.png (1024) and nicecrash.ico (%s)' % ', '.join(str(n) for n, _ in images))


if __name__ == '__main__':
    main()
