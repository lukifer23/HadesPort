#!/usr/bin/env python3
"""Run original Main.lua's coroutine scheduler through the portable Mac bridge."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lua-library', type=Path,
                        default=ROOT / '.local/native-host-build/liblua52.a')
    parser.add_argument('--main', type=Path,
                        default=ROOT / '.local/game/base/Content/Scripts/Main.lua')
    args = parser.parse_args()
    compiler = shutil.which('clang++')
    if not compiler or not args.lua_library.is_file() or not args.main.is_file():
        parser.error('Existing Mac clang++, host Lua 5.2.2 library and original Main.lua are required')
    parent = ROOT / '.local/native-analysis'
    parent.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix='script-scheduler-', dir=parent))
    executable = run / 'scheduler-smoke'
    sources = [ROOT / 'native/analysis/script_scheduler_smoke.cpp',
               ROOT / 'native/app/script_scheduler.cpp',
               ROOT / 'native/app/script_scheduler.hpp']
    command = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Wpedantic',
               '-fsanitize=address,undefined', '-I',
               str(ROOT / '.local/native-deps/lua-5.2.2/src'),
               str(sources[0]), str(sources[1]), str(args.lua_library), '-o', str(executable)]
    built = subprocess.run(command, text=True, capture_output=True)
    (run / 'build.log').write_text(built.stdout + built.stderr)
    built.check_returncode()
    result = subprocess.run([str(executable), str(args.main)], text=True, capture_output=True)
    (run / 'smoke.log').write_text(result.stdout + result.stderr)
    report = {
        'schema': 'hades-original-main-scheduler-debug-v1',
        'original_main': {'path': str(args.main.resolve()), 'sha256': digest(args.main)},
        'host_lua_library': {'path': str(args.lua_library.resolve()), 'sha256': digest(args.lua_library)},
        'compile_command': command,
        'compiler_identity': subprocess.check_output([compiler, '--version'], text=True),
        'sanitizers': ['address', 'undefined'],
        'source_sha256': {str(path.relative_to(ROOT)): digest(path) for path in sources},
        'executable_sha256': digest(executable),
        'exit_code': result.returncode,
        'result': 'pass' if result.returncode == 0 else 'fail',
        'log': str(run / 'smoke.log'),
        'limits': [
            'Runs the unmodified original Main.lua, not the original Windows executable.',
            'Synthetic callback bodies test scheduling semantics; no room, actor, input, camera or combat integration is implied.',
            'Deferred presentation functions are instrumented observation hooks in this isolated harness.',
            'The expected final coroutine error tests explicit fault retention; it is not a gameplay result.',
        ],
    }
    (run / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'result': report['result'], 'report': str(run / 'report.json')}))
    return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
