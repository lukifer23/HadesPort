#!/usr/bin/env python3.13
"""Extract the original Windows application icon from Hades.exe for Android.

Reads the PE resource tree (RT_GROUP_ICON 14 / RT_ICON 3) without executing
anything, takes the largest icon image, and writes Android launcher PNGs
(mipmap densities). The icon is an original asset: personal use, never
distributed. Requires a fresh output directory and Pillow.
"""
import argparse, hashlib, io, json, struct
from pathlib import Path
from PIL import Image

DENSITIES = {'mdpi': 48, 'hdpi': 72, 'xhdpi': 96, 'xxhdpi': 144, 'xxxhdpi': 192}


def sections(data):
    pe = struct.unpack_from('<I', data, 60)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    optional = struct.unpack_from('<H', data, pe + 20)[0]
    magic = struct.unpack_from('<H', data, pe + 24)[0]
    directories = pe + 24 + (112 if magic == 0x20b else 96)
    resource_rva = struct.unpack_from('<I', data, directories + 8 * 2)[0]
    table = []
    for i in range(count):
        o = pe + 24 + optional + 40 * i
        vsize, rva, rsize, raw = struct.unpack_from('<IIII', data, o + 8)
        table.append((rva, max(vsize, rsize), raw))
    return resource_rva, table


def offset(table, rva):
    for start, size, raw in table:
        if start <= rva < start + size:
            return raw + rva - start
    raise ValueError('RVA outside sections')


def entries(data, base, directory):
    named, ids = struct.unpack_from('<HH', data, directory + 12)
    for i in range(named + ids):
        name, target = struct.unpack_from('<II', data, directory + 16 + 8 * i)
        yield name, target


def resources(data):
    rva, table = sections(data)
    root = offset(table, rva)
    found = {}
    for type_id, type_target in entries(data, root, root):
        if not type_target & 0x80000000:
            continue
        for name_id, name_target in entries(data, root, root + (type_target & 0x7fffffff)):
            for _, leaf in entries(data, root, root + (name_target & 0x7fffffff)):
                data_rva, size = struct.unpack_from('<II', data, root + leaf)
                found[(type_id, name_id)] = data[offset(table, data_rva):offset(table, data_rva) + size]
    return found


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if a.output.exists():
        raise SystemExit('Fresh output required')
    data = a.exe.read_bytes()
    res = resources(data)
    best = None
    for (type_id, name_id), group in res.items():
        if type_id != 14:
            continue
        _, kind, n = struct.unpack_from('<HHH', group, 0)
        for i in range(n):
            w, h, _, _, _, bits, size, icon_id = struct.unpack_from('<BBBBHHIH', group, 6 + 14 * i)
            w, h = w or 256, h or 256
            image = res.get((3, icon_id))
            if image and (best is None or (w * h, bits) > best[0]):
                best = ((w * h, bits), icon_id, image, w, h)
    if not best:
        raise SystemExit('No RT_GROUP_ICON in executable')
    _, icon_id, blob, w, h = best
    if blob[:8] == b'\x89PNG\r\n\x1a\n':
        image = Image.open(io.BytesIO(blob)).convert('RGBA')
    else:  # DIB icon: wrap as a single-image .ico for Pillow
        header = struct.pack('<HHH', 0, 1, 1) + struct.pack('<BBBBHHII', w % 256, h % 256, 0, 0, 1, 32, len(blob), 22)
        image = Image.open(io.BytesIO(header + blob)).convert('RGBA')
    a.output.mkdir(parents=True)
    outputs = {}
    for density, size in DENSITIES.items():
        folder = a.output / f'mipmap-{density}'
        folder.mkdir()
        path = folder / 'ic_launcher.png'
        image.resize((size, size), Image.LANCZOS).save(path)
        outputs[density] = {'path': str(path.relative_to(a.output)), 'size': size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    report = {'schema': 'hades-app-icon-v1', 'source': {'path': str(a.exe), 'sha256': hashlib.sha256(data).hexdigest()},
              'icon_resource_id': icon_id, 'source_size': [w, h], 'mipmaps': outputs}
    (a.output / 'manifest.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
