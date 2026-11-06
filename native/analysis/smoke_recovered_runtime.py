#!/usr/bin/env python3
"""Build a host-only debugging smoke driver against the exact Lua5.2.2 sources."""
from pathlib import Path
import concurrent.futures
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
LUA = ROOT / '.local/native-deps/lua-5.2.2/src'
sources = sorted(p for p in LUA.glob('*.c') if p.name not in ('lua.c', 'luac.c'))
if not sources:
    raise RuntimeError('Prepare the pinned Lua5.2.2 sources before this check')

with tempfile.TemporaryDirectory(prefix='recovered-runtime-', dir=ROOT / '.local') as temporary:
    output = Path(temporary)

    def compile_source(source):
        obj = output / (source.stem + '.o')
        subprocess.run(['nice', '-n', '15', 'clang', '-O1', '-DLUA_USE_POSIX', '-I', str(LUA), '-c', str(source), '-o', str(obj)], check=True)
        return obj

    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        objects = list(pool.map(compile_source, sources))
    binary = output / 'recovered_runtime_smoke'
    subprocess.run(['nice', '-n', '15', 'clang++', '-std=c++20', '-Wall', '-Wextra', '-I', str(LUA),
        str(ROOT/'native/app/recovered_runtime.cpp'), str(ROOT/'native/analysis/recovered_runtime_smoke.cpp'),
        *map(str, objects), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
