#!/usr/bin/env python3
"""Exercise dependency preparation with the real locally cached pinned archives.

No downloads or active source edits. Temporary extractions are removed on exit.
Run after scripts/prepare_native_dependencies.py has populated the archive cache.
"""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import tempfile

PROJECT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('prepare_native_dependencies', PROJECT / 'scripts/prepare_native_dependencies.py')
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


def check_dependency(root, dependency):
    archive = root / dependency['archive']
    shutil.copyfile(PROJECT / '.local/native-deps' / dependency['archive'], archive)
    directory = root / dependency['directory']
    passed = []

    def run():
        with contextlib.redirect_stdout(io.StringIO()):
            prepare.prepare(root, dependency)

    def rejects(label):
        try:
            run()
        except RuntimeError:
            passed.append(label)
        else:
            raise AssertionError(f"{dependency['name']}: accepted {label}")

    run()
    files = [path for path in directory.rglob('*') if path.is_file()]
    before = {path.relative_to(directory): (path.stat().st_ino, path.stat().st_mtime_ns) for path in files}
    run()
    assert before == {path.relative_to(directory): (path.stat().st_ino, path.stat().st_mtime_ns) for path in files}
    passed.append('fresh extraction and idempotent verification')

    source = next(path for path in files if path.stat().st_size)
    original = source.read_bytes()
    source.write_bytes(bytes([original[0] ^ 1]) + original[1:])
    rejects('same-size modified source')
    assert source.read_bytes() != original
    source.write_bytes(original)

    source.unlink()
    rejects('missing source')
    source.write_bytes(original)

    extra = directory / 'unreviewed.c'
    extra.write_text('unreviewed')
    rejects('unexpected source')
    assert extra.is_file()
    extra.unlink()

    source.unlink()
    source.symlink_to(archive.resolve())
    rejects('source symlink')
    assert source.is_symlink()

    shutil.rmtree(directory)
    directory.mkdir()
    rejects('interrupted empty extraction')
    assert directory.is_dir() and not list(directory.iterdir())
    directory.rmdir()

    original_archive = archive.read_bytes()
    archive.write_bytes(original_archive[:-1])
    rejects('corrupt cached archive')
    archive.write_bytes(original_archive + bytes(dependency['max_bytes'] + 1 - len(original_archive)))
    rejects('oversized cached archive')
    assert not directory.exists()
    assert not list(root.glob('.source-*')) and not list(root.glob('.archive-*'))
    return {'dependency': dependency['name'], 'passed': passed}


def main():
    # System temporary storage keeps probe extractions outside the active cache.
    with tempfile.TemporaryDirectory(prefix='hades-dependency-integrity-') as temporary:
        results = [check_dependency(Path(temporary), dependency) for dependency in prepare.DEPENDENCIES]
    print(json.dumps({'checks_passed': sum(len(result['passed']) for result in results), 'results': results}, indent=2))


if __name__ == '__main__':
    main()
