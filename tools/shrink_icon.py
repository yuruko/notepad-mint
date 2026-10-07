"""shrink_icon.py - shrinks the 256 x 256 png inside assets\\notepad_mint.ico (65 kb -> 14 kb: a third of the exe was this one picture).

make_icon.ps1 builds the icon at full quality; run this after it (python 3 with pillow and numpy):
    python tools\\shrink_icon.py [assets\\notepad_mint.ico]
the big image becomes an 8-bit palette png (with per-colour alpha), after an ordered dither of +-3.5 levels so that the soft gradient of the
glass does not turn into visible bands. the small bmp sizes are left as they are. running it twice does nothing (it only touches a png with more than 256 colours)."""
import io
import struct
import sys

import numpy as np
from PIL import Image

path = sys.argv[1] if len(sys.argv) > 1 else 'assets/notepad_mint.ico'
data = open(path, 'rb').read()
count = struct.unpack('<H', data[4:6])[0]
entries = [list(struct.unpack('<BBBBHHII', data[6 + 16 * i:22 + 16 * i])) for i in range(count)]
bodies = [data[e[7]:e[7] + e[6]] for e in entries]

for i, e in enumerate(entries):
    if bodies[i][:4] != b'\x89PNG':
        continue
    img = Image.open(io.BytesIO(bodies[i]))
    if img.mode == 'P':
        continue
    px = np.asarray(img.convert('RGBA')).astype(float)
    bayer = np.array([[0, 32, 8, 40, 2, 34, 10, 42], [48, 16, 56, 24, 50, 18, 58, 26], [12, 44, 4, 36, 14, 46, 6, 38], [60, 28, 52, 20, 62, 30, 54, 22],
                      [3, 35, 11, 43, 1, 33, 9, 41], [51, 19, 59, 27, 49, 17, 57, 25], [15, 47, 7, 39, 13, 45, 5, 37], [63, 31, 55, 23, 61, 29, 53, 21]]) / 64.0 - 0.5
    tile = np.tile(bayer, (px.shape[0] // 8 + 1, px.shape[1] // 8 + 1))[:px.shape[0], :px.shape[1]]
    for c in range(3):
        px[..., c] += tile * 7
    noisy = Image.fromarray(np.clip(px, 0, 255).astype(np.uint8))
    q = noisy.quantize(256, method=Image.FASTOCTREE, dither=Image.NONE)
    out = io.BytesIO()
    q.save(out, 'PNG', optimize=True)
    print('%dx%d png: %d -> %d bytes' % (e[0] or 256, e[1] or 256, len(bodies[i]), len(out.getvalue())))
    bodies[i] = out.getvalue()

head = bytearray(data[:6])
off = 6 + 16 * count
table = bytearray()
for e, b in zip(entries, bodies):
    e[6] = len(b)
    e[7] = off
    off += len(b)
    table += struct.pack('<BBBBHHII', *e)
open(path, 'wb').write(bytes(head) + bytes(table) + b''.join(bodies))
print(path, off, 'bytes')
