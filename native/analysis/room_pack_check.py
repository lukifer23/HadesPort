#!/usr/bin/env python3
"""Check room staging/revision against a genuine locally exported room bundle.

Usage: python3 native/analysis/room_pack_check.py --manifest MANIFEST --actors ACTOR_MANIFEST
Only temporary copies of original metadata are damaged; active packs stay intact.
"""
import argparse
import copy
import json
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from room_pack_contract import validate_room_manifest, validate_room_name
from stage_room_packs import ROOT, build_room_pack, identity, revise_room_actor_catalog, room_output_path, stage_room_pack


def check(manifest, actors, source=None):
    original = json.loads(manifest.read_text())
    room = validate_room_manifest(original)
    passed = []
    with tempfile.TemporaryDirectory(prefix='hades-room-pack-') as temporary:
        root = Path(temporary)
        assets = root / 'assets'
        assets.mkdir()
        result = stage_room_pack(manifest, assets)
        assert result == {'room': room, 'files': len(original['files'])}
        for name, row in original['files'].items():
            assert identity(assets / 'rooms' / room / name)['sha256'] == row['sha256']
        passed.append('real room stage and payload identities')
        try:
            stage_room_pack(manifest, assets)
        except ValueError as error:
            assert 'Duplicate room' in str(error)
        else:
            raise AssertionError('Duplicate room selection accepted')
        passed.append('duplicate selection preserves staged room')

        def rejects(label, mutate):
            changed = copy.deepcopy(original)
            mutate(changed)
            bad = root / 'invalid.json'
            bad.write_text(json.dumps(changed))
            rejected = root / 'rejected'
            rejected.mkdir(exist_ok=True)
            try:
                stage_room_pack(bad, rejected)
            except (ValueError, FileNotFoundError):
                pass
            else:
                raise AssertionError('Accepted ' + label)
            assert not list(rejected.iterdir()), 'Wrote assets for ' + label
            passed.append(label)

        for name in original['files']:
            rejects('missing ' + name, lambda d, n=name: d['files'].pop(n))
        for value in ['', 'x' * 65, '../Room', 'room/name', 'Róom', 'RoomOpening', None, 1]:
            rejects('invalid room ' + repr(value), lambda d, v=value: d.update(room=v))
        for name in ['Scripts', '../scene.bin']:
            rejects('unexpected file ' + name, lambda d, n=name: d['files'].update({n: next(iter(d['files'].values()))}))
        rejects('wrong target', lambda d: d.update(target_sha256='0' * 64))
        for field, value in [('sha256', '0' * 64), ('bytes', 1), ('sha256', 'bad'), ('bytes', True), ('path', str(root / 'absent'))]:
            rejects('invalid source identity ' + repr((field, value)), lambda d, k=field, v=value: d['files']['scene.bin'].update({k: v}))

        revision = root / 'revision'
        revised = revise_room_actor_catalog(manifest, actors, revision)
        assert all(Path(row['path']).parent == revision.resolve() for row in revised['files'].values())
        validate_room_manifest(json.loads((revision / 'manifest.json').read_text()))
        for row in revised['files'].values():
            assert identity(row['path'])['sha256'] == row['sha256']
        passed.append('real actor revision publishes final-path identities')

        bad = copy.deepcopy(original)
        bad['files'].pop('scene.bin')
        path = root / 'bad-base.json'
        path.write_text(json.dumps(bad))
        before = set(root.iterdir())
        try:
            revise_room_actor_catalog(path, actors, root / 'failed-revision')
        except ValueError:
            pass
        else:
            raise AssertionError('Incomplete revision accepted')
        assert set(root.iterdir()) == before
        passed.append('failed revision leaves no output or scratch')
        # The real checkout may use .local -> .local.nosync. Compare physical
        # paths on both sides; do not create anything in the original game.
        for protected in [ROOT / '.local/game/fresh-check', (ROOT / '.local/game').resolve() / 'fresh-check']:
            try:
                room_output_path(protected)
            except ValueError as error:
                assert 'originals' in str(error)
            else:
                raise AssertionError('Original game output accepted')
        passed.append('original game protected through logical and physical paths')
        for name in ['A' * 64, '_', room]:
            assert validate_room_name(name) == name
        passed.append('native map-name boundaries')
        if source is not None:
            built = root / 'built'
            result = build_room_pack(source, built, actors=actors)
            assert result['room'] == room
            validate_room_manifest(json.loads((built / 'manifest.json').read_text()))
            for name, row in result['files'].items():
                assert Path(row['path']).parent == built.resolve()
                assert identity(row['path'])['sha256'] == row['sha256']
            passed.append('real room producer publishes complete final-path identities')
    return passed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--actors', type=Path, required=True)
    parser.add_argument('--source', type=Path, help='Also exercise the original room producer with preserved export sources')
    args = parser.parse_args()
    passed = check(args.manifest, args.actors, args.source)
    print(json.dumps({'checks_passed': len(passed), 'passed': passed}, indent=2))


if __name__ == '__main__':
    main()
