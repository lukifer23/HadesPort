#!/usr/bin/env python3
"""Fetch the exact native source versions recovered from the original engine.

Lua 5.2.2 and Luabins 0.3 are named by strings in EngineWin64s.dll. LZ4 is
linked into the engine (LZ4_compress_fast/LZ4_decompress_safe in rmem_hook.obj)
with the 1.9.x 16416-byte stream state; 1.9.4 is pinned as the block-format
implementation. Every archive is identified by SHA-256 before extraction.
"""
import hashlib
import io
import os
from pathlib import Path
import stat
import tarfile
import tempfile
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


def source_members(package, dependency):
    """Validate the complete archive before creating any extracted source files."""
    members = []
    names = set()
    for entry in package.getmembers():
        path = Path(entry.name)
        if path.is_absolute() or '..' in path.parts or not path.parts or path.parts[0] != dependency['directory']:
            raise RuntimeError(f"Unsafe {dependency['name']} archive member")
        if entry.issym() or entry.islnk():
            if entry.name not in dependency['skip_links']:
                raise RuntimeError(f"Unexpected link in {dependency['name']} archive")
            continue
        if not (entry.isfile() or entry.isdir()) or path in names:
            raise RuntimeError(f"Unsafe or duplicate {dependency['name']} archive member")
        names.add(path)
        members.append(entry)
    if not any(entry.isfile() for entry in members):
        raise RuntimeError(f"Empty {dependency['name']} source archive")
    return members


def verify_sources(directory, package, members, dependency):
    """Existence is not completion: compare every source with the pinned archive."""
    if directory.is_symlink() or not directory.is_dir():
        raise RuntimeError(f"Pinned {dependency['name']} source is not an actual directory: {directory}")
    expected = set()
    for entry in members:
        relative = Path(entry.name).relative_to(dependency['directory'])
        expected.add(relative)
        expected.update(relative.parents)
        path = directory / relative
        try:
            mode = path.lstat().st_mode
        except FileNotFoundError:
            raise RuntimeError(f"Incomplete {dependency['name']} source: missing {relative}") from None
        # Check parent directories before opening a member, so verification never
        # follows an injected symlink outside the selected source tree.
        if any((directory / parent).is_symlink() for parent in relative.parents):
            raise RuntimeError(f"Unexpected link in {dependency['name']} source: {relative}")
        if entry.isdir():
            valid = stat.S_ISDIR(mode)
        else:
            valid = stat.S_ISREG(mode) and path.stat().st_size == entry.size
            if valid:
                with path.open('rb') as actual, package.extractfile(entry) as original:
                    valid = hashlib.file_digest(actual, 'sha256').digest() == hashlib.file_digest(original, 'sha256').digest()
        if not valid:
            raise RuntimeError(f"Modified {dependency['name']} source: {relative}; existing files preserved")
    for path in directory.rglob('*'):
        if path.relative_to(directory) not in expected:
            raise RuntimeError(f"Unexpected {dependency['name']} source entry: {path.relative_to(directory)}; existing files preserved")


def prepare(root, dependency):
    archive = root / dependency['archive']
    if not archive.exists():
        with urllib.request.urlopen(dependency['url'], timeout=30) as response:
            data = response.read(dependency['max_bytes'] + 1)
        if len(data) > dependency['max_bytes'] or hashlib.sha256(data).hexdigest() != dependency['sha256']:
            raise RuntimeError(f"{dependency['name']} source archive identity mismatch")
        # A cancelled download must not publish a partial cache entry.
        with tempfile.TemporaryDirectory(prefix='.archive-', dir=root) as temporary:
            candidate = Path(temporary) / dependency['archive']
            candidate.write_bytes(data)
            os.replace(candidate, archive)
    if archive.stat().st_size > dependency['max_bytes']:
        raise RuntimeError(f"Cached {dependency['name']} archive exceeds size bound")
    data = archive.read_bytes()
    if len(data) > dependency['max_bytes'] or hashlib.sha256(data).hexdigest() != dependency['sha256']:
        raise RuntimeError(f"Cached {dependency['name']} archive identity mismatch")
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as package:
        members = source_members(package, dependency)
        directory = root / dependency['directory']
        if directory.exists() or directory.is_symlink():
            verify_sources(directory, package, members, dependency)
            print(f"Verified pinned {dependency['name']} source")
            return
        # Extract and verify beside the destination, then publish with one rename.
        # Failures/cancellation leave no directory that could masquerade as ready.
        with tempfile.TemporaryDirectory(prefix='.source-', dir=root) as temporary:
            package.extractall(temporary, members=members, filter='data')
            candidate = Path(temporary) / dependency['directory']
            verify_sources(candidate, package, members, dependency)
            if directory.exists() or directory.is_symlink():
                raise RuntimeError(f"{dependency['name']} source appeared during preparation; existing files preserved")
            candidate.rename(directory)
    print(f"Prepared {dependency['name']} {dependency['sha256']} from {dependency['url']}")


def main():
    root = PROJECT / '.local/native-deps'
    root.mkdir(parents=True, exist_ok=True)
    for dependency in DEPENDENCIES:
        prepare(root, dependency)


if __name__ == '__main__':
    main()
