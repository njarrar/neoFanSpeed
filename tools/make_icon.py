#!/usr/bin/env python3
"""Build res/app.ico from rendered PNGs of res/icon-large.svg and res/icon-small.svg.

Every image is stored as a plain BMP (no PNG inside the icon) in 16 colors
and 256 colors, the formats Windows 98 and ME can read.

usage: make_icon.py out.ico icon16.png icon32.png icon48.png
"""
import struct
import sys

from PIL import Image

VGA16 = [
    (0, 0, 0), (128, 0, 0), (0, 128, 0), (128, 128, 0), (0, 0, 128), (128, 0, 128), (0, 128, 128), (192, 192, 192),
    (128, 128, 128), (255, 0, 0), (0, 255, 0), (255, 255, 0), (0, 0, 255), (255, 0, 255), (0, 255, 255), (255, 255, 255),
]


def palette_image(colors):
    pal = Image.new("P", (1, 1))
    flat = []
    for c in colors:
        flat.extend(c)
    flat.extend([0] * (768 - len(flat)))
    pal.putpalette(flat)
    return pal


def entry(img, bpp):
    w, h = img.size
    rgba = img.convert("RGBA")
    flat = Image.new("RGB", (w, h), (0, 0, 0))
    flat.paste(rgba, mask=rgba.split()[3])
    if bpp == 4:
        colors = VGA16
        q = flat.quantize(palette=palette_image(VGA16), dither=Image.Dither.NONE)
    else:
        q = flat.quantize(colors=256, dither=Image.Dither.NONE)
        p = q.getpalette()[: 256 * 3]
        p += [0] * (768 - len(p))
        colors = [tuple(p[i * 3: i * 3 + 3]) for i in range(256)]
    ncol = 16 if bpp == 4 else 256
    px = q.load()
    alpha = rgba.split()[3].load()

    header = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, bpp, 0, 0, 0, 0, ncol, 0)
    pal = b"".join(struct.pack("<BBBB", c[2], c[1], c[0], 0) for c in colors[:ncol])

    xor_rows = []
    row_bytes = ((w * bpp + 31) // 32) * 4
    for y in range(h - 1, -1, -1):
        row = bytearray()
        if bpp == 8:
            for x in range(w):
                row.append(px[x, y] if alpha[x, y] >= 128 else 0)
        else:
            for x in range(0, w, 2):
                a = px[x, y] if alpha[x, y] >= 128 else 0
                b = (px[x + 1, y] if alpha[x + 1, y] >= 128 else 0) if x + 1 < w else 0
                row.append((a << 4) | b)
        row += b"\0" * (row_bytes - len(row))
        xor_rows.append(bytes(row))

    and_rows = []
    mask_bytes = ((w + 31) // 32) * 4
    for y in range(h - 1, -1, -1):
        row = bytearray(mask_bytes)
        for x in range(w):
            if alpha[x, y] < 128:
                row[x // 8] |= 0x80 >> (x % 8)
        and_rows.append(bytes(row))

    data = header + pal + b"".join(xor_rows) + b"".join(and_rows)
    return w, h, ncol, bpp, data


def main():
    out = sys.argv[1]
    imgs = [Image.open(p) for p in sys.argv[2:]]
    entries = []
    for bpp in (4, 8):
        for im in imgs:
            entries.append(entry(im, bpp))
    head = struct.pack("<HHH", 0, 1, len(entries))
    offset = 6 + 16 * len(entries)
    dirs, blobs = b"", b""
    for w, h, ncol, bpp, data in entries:
        dirs += struct.pack("<BBBBHHII", w % 256, h % 256, ncol % 256, 0, 1, bpp, len(data), offset)
        offset += len(data)
        blobs += data
    with open(out, "wb") as f:
        f.write(head + dirs + blobs)


if __name__ == "__main__":
    main()
