"""Losslessly pack 32-bit DIB entries in an ICO as PNG; keep other entries intact.

Requires Python 3 and Pillow. Run after make_icon.ps1 / shrink_icon.py:
    python tools/pack_icon.py [assets/notepad_mint.ico]
    python tools/pack_icon.py --check --reference build/notepad_mint-before-1.0.6.ico

Every converted entry retains identical RGBA pixels and directory metadata. PNG
entries are copied verbatim. On Windows, LoadImageW and DrawIconEx also compare
native rendering at all embedded sizes and common intermediate DPI sizes before
the file is replaced. --check verifies that another packing pass changes no bytes.

PNG icon image resources are supported since Vista (the app targets Windows 7+):
https://devblogs.microsoft.com/oldnewthing/20101022-00/?p=12473
https://devblogs.microsoft.com/oldnewthing/20231025-00/?p=108925
No XP compatibility is implied; old tools may not understand PNG icon entries.
"""
import argparse
import ctypes
import io
import os
from pathlib import Path
import struct
import tempfile

from PIL import Image


PNG = b"\x89PNG\r\n\x1a\n"
DIRECTORY = struct.Struct("<BBBBHHII")


def entries(data):
    if len(data) < 6:
        raise ValueError("truncated icon header")
    reserved, kind, count = struct.unpack_from("<HHH", data)
    if reserved or kind != 1 or not count or 6 + count * DIRECTORY.size > len(data):
        raise ValueError("invalid icon directory")
    result = []
    for index in range(count):
        header = DIRECTORY.unpack_from(data, 6 + index * DIRECTORY.size)
        size, offset = header[6:]
        if not size or offset < 6 + count * DIRECTORY.size or offset + size > len(data):
            raise ValueError("icon image lies outside the file")
        result.append((header[:6], data[offset:offset + size]))
    return result


def encode(items):
    header = struct.pack("<HHH", 0, 1, len(items))
    directory, bodies = bytearray(), bytearray()
    offset = 6 + len(items) * DIRECTORY.size
    for fields, body in items:
        directory.extend(DIRECTORY.pack(*fields, len(body), offset))
        bodies.extend(body)
        offset += len(body)
    return header + directory + bodies


def rgba(item):
    # A one-entry ICO also handles duplicate sizes without selecting another image.
    with Image.open(io.BytesIO(encode([item]))) as image:
        return image.convert("RGBA")


def pack(data):
    result, changes = [], []
    for fields, body in entries(data):
        width, height = fields[0] or 256, fields[1] or 256
        eligible = False
        if not body.startswith(PNG) and len(body) >= 40:
            header, dib_width, dib_height, planes, bits, compression = struct.unpack_from("<IiiHHI", body)
            pixels_end = 40 + width * height * 4
            # Legacy all-zero alpha uses the AND mask for transparency. Preserve it.
            eligible = (header == 40 and dib_width == width and dib_height == height * 2
                        and planes == 1 and bits == 32 and compression == 0
                        and len(body) >= pixels_end
                        and any(body[43:pixels_end:4]))
        if eligible:
            image = rgba((fields, body))
            stream = io.BytesIO()
            image.save(stream, "PNG", optimize=True)
            compressed = stream.getvalue()
            with Image.open(io.BytesIO(compressed)) as decoded:
                if decoded.convert("RGBA").tobytes() != image.tobytes():
                    raise ValueError("PNG encoding changed pixels")
            if len(compressed) < len(body):
                changes.append((width, height, len(body), len(compressed)))
                body = compressed
        result.append((fields, body))
    return (encode(result) if changes else data), changes


def verify_pixels(before, after):
    original, packed = entries(before), entries(after)
    if len(original) != len(packed):
        raise ValueError("packing changed the number of images")
    for left, right in zip(original, packed):
        if left[0] != right[0]:
            raise ValueError("packing changed image directory metadata")
        if rgba(left).tobytes() != rgba(right).tobytes():
            raise ValueError("packing changed RGBA pixels")
        if left[1].startswith(PNG) and left[1] != right[1]:
            raise ValueError("packing changed an existing PNG image")


def verify_native(before, after, sizes):
    """Load complete ICO files through Windows, including its size selection rules."""
    user = ctypes.WinDLL("user32", use_last_error=True)
    gdi = ctypes.WinDLL("gdi32", use_last_error=True)
    ptr, uint, integer = ctypes.c_void_p, ctypes.c_uint, ctypes.c_int
    user.LoadImageW.argtypes = [ptr, ctypes.c_wchar_p, uint, integer, integer, uint]
    user.LoadImageW.restype = ptr
    user.DestroyIcon.argtypes = [ptr]
    user.DrawIconEx.argtypes = [ptr, integer, integer, ptr, integer, integer, uint, ptr, uint]
    gdi.CreateCompatibleDC.argtypes = [ptr]
    gdi.CreateCompatibleDC.restype = ptr
    gdi.CreateDIBSection.argtypes = [ptr, ptr, uint, ctypes.POINTER(ptr), ptr, uint]
    gdi.CreateDIBSection.restype = ptr
    gdi.SelectObject.argtypes = [ptr, ptr]
    gdi.SelectObject.restype = ptr
    gdi.DeleteObject.argtypes = [ptr]
    gdi.DeleteDC.argtypes = [ptr]

    def render(path, size, background):
        icon = user.LoadImageW(None, str(path.resolve()), 1, size, size, 0x10)
        if not icon:
            raise ctypes.WinError(ctypes.get_last_error())
        dc, bitmap, previous = None, None, None
        try:
            dc = gdi.CreateCompatibleDC(None)
            if not dc:
                raise ctypes.WinError(ctypes.get_last_error())
            bits = ptr()
            info = ctypes.create_string_buffer(struct.pack("<IiiHHIIiiII", 40, size, -size, 1, 32, 0, size * size * 4, 0, 0, 0, 0))
            bitmap = gdi.CreateDIBSection(dc, info, 0, ctypes.byref(bits), None, 0)
            if not bitmap or not bits.value:
                raise ctypes.WinError(ctypes.get_last_error())
            previous = gdi.SelectObject(dc, bitmap)
            pixels = (uint * (size * size)).from_address(bits.value)
            for index in range(size * size):
                pixels[index] = background
            if not user.DrawIconEx(dc, 0, 0, icon, size, size, 0, None, 3):
                raise ctypes.WinError(ctypes.get_last_error())
            return ctypes.string_at(bits.value, size * size * 4)
        finally:
            if previous:
                gdi.SelectObject(dc, previous)
            if bitmap:
                gdi.DeleteObject(bitmap)
            if dc:
                gdi.DeleteDC(dc)
            user.DestroyIcon(icon)

    checks = 0
    for size in sizes:
        for background in (0x000000, 0x161418, 0xDFDEE1, 0xFFFFFF):
            if render(before, size, background) != render(after, size, background):
                raise ValueError(f"native pixels differ at {size}px on #{background:06x}")
            checks += 1
    print(f"native LoadImageW/DrawIconEx: {checks} identical renderings")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", nargs="?", type=Path, default=Path("assets/notepad_mint.ico"))
    parser.add_argument("--check", action="store_true", help="verify packing is already complete without writing")
    parser.add_argument("--reference", type=Path, help="compare pixels against an original ICO")
    args = parser.parse_args()
    original = args.path.read_bytes()
    packed, changes = pack(original)
    verify_pixels(original, packed)
    if pack(packed)[0] != packed:
        raise ValueError("packing is not byte-for-byte idempotent")
    if args.check and changes:
        raise ValueError("icon has entries that can still be packed")
    reference = args.reference or args.path
    verify_pixels(reference.read_bytes(), packed)
    sizes = sorted({16, 20, 24, 32, 40, 48, 64, 96, 128, 256}
                   | {fields[0] or 256 for fields, _ in entries(original)})
    staged = None
    try:
        if changes:
            with tempfile.NamedTemporaryFile(dir=args.path.parent, suffix=".ico", delete=False) as file:
                staged = Path(file.name)
                file.write(packed)
            candidate = staged
        else:
            candidate = args.path
        if os.name == "nt":
            verify_native(reference, candidate, sizes)
        if changes:
            os.replace(staged, args.path)
            staged = None
    finally:
        if staged is not None:
            staged.unlink(missing_ok=True)
    for width, height, before, after in changes:
        print(f"{width}x{height}: {before} -> {after} bytes, lossless RGBA")
    print(f"{args.path}: {len(original)} -> {len(packed)} bytes; idempotence verified")


if __name__ == "__main__":
    main()
