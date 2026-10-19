#!/usr/bin/env python3
"""Fetch the exact native source versions recovered from the original engine.

Lua 5.2.2 and Luabins 0.3 are named by strings in EngineWin64s.dll. LZ4 is
linked into the engine (LZ4_compress_fast/LZ4_decompress_safe in rmem_hook.obj)
with the 1.9.x 16416-byte stream state; 1.9.4 is pinned as the block-format
implementation. Every archive is identified by SHA-256 before extraction.
"""
import hashlib
import io
from pathlib import Path
import tarfile
import urllib.request

PROJECT = Path(__file__).resolve().parents[1]
DEPENDENCIES = [
    {
        'name': 'Lua 5.2.2',
        'url': 'https://www.lua.org/ftp/lua-5.2.2.tar.gz',
        'sha256': '3fd67de3f5ed133bf312906082fa524545c6b9e1b952e8215ffbd27113f49f00',
        'archive': 'lua-5.2.2.tar.gz',
        'directory': 'lua-5.2.2',
        'max_bytes': 2_000_000,
        'skip_links': set(),
    },
    {
        # Upstream tag v0.3 (commit 1ab67eeb0fbb7ae3ebd0011ceecf04f85a4449d1).
        'name': 'Luabins 0.3',
        'url': 'https://github.com/agladysh/luabins/archive/refs/tags/v0.3.tar.gz',
        'sha256': '701f68b988fcd1b5cb874dfefc6df84830f6f2b801339c273395ab3ad9de79cb',
        'archive': 'luabins-0.3.tar.gz',
        'directory': 'luabins-0.3',
        'max_bytes': 1_000_000,
        # README -> README.md is the only link in the release; it is not extracted.
        'skip_links': {'luabins-0.3/README'},
    },
    {
        'name': 'LZ4 1.9.4',
        'url': 'https://github.com/lz4/lz4/archive/refs/tags/v1.9.4.tar.gz',
        'sha256': '0b0e3aa07c8c063ddf40b082bdf7e37a1562bda40a0ff5272957f3e987e0e54b',
        'archive': 'lz4-1.9.4.tar.gz',
        'directory': 'lz4-1.9.4',
        'max_bytes': 2_000_000,
        'skip_links': set(),
    },
]


def prepare(root, dependency):
    archive = root / dependency['archive']
    if not archive.exists():
        with urllib.request.urlopen(dependency['url'], timeout=30) as response:
            data = response.read(dependency['max_bytes'] + 1)
        if len(data) > dependency['max_bytes'] or hashlib.sha256(data).hexdigest() != dependency['sha256']:
            raise RuntimeError(f"{dependency['name']} source archive identity mismatch")
        archive.write_bytes(data)
    data = archive.read_bytes()
    if hashlib.sha256(data).hexdigest() != dependency['sha256']:
        raise RuntimeError(f"Cached {dependency['name']} archive identity mismatch")
    if (root / dependency['directory']).exists():
        print(f"Pinned {dependency['name']} source already present")
        return
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as package:
        members = []
        for entry in package.getmembers():
            path = Path(entry.name)
            if path.is_absolute() or '..' in path.parts or (path.parts and path.parts[0] != dependency['directory']):
                raise RuntimeError(f"Unsafe {dependency['name']} archive member")
            if entry.issym() or entry.islnk():
                if entry.name not in dependency['skip_links']:
                    raise RuntimeError(f"Unexpected link in {dependency['name']} archive")
                continue
            if not (entry.isfile() or entry.isdir()):
                raise RuntimeError(f"Unsafe {dependency['name']} archive member")
            members.append(entry)
        package.extractall(root, members=members, filter='data')
    print(f"Prepared {dependency['name']} {dependency['sha256']} from {dependency['url']}")


def main():
    root = PROJECT / '.local/native-deps'
    root.mkdir(exist_ok=True)
    for dependency in DEPENDENCIES:
        prepare(root, dependency)


if __name__ == '__main__':
    main()
