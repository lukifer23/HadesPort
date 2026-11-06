#!/usr/bin/env python3
"""Check staging against genuine exported catalogs and damaged local copies.

No original game/export bytes are modified. Payload links and filename-inventory
copies live in a temporary directory and are removed after all checks.
"""
import argparse
import copy
import json
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from package_catalog import TARGET, validate_package_catalog
from export_runtime_packages import identity


def check(manifest_path, legacy_manifest=None):
    original = json.loads(manifest_path.read_text(encoding='utf-8'))
    proof = json.loads(Path(original['unit_package_names']['path']).read_text(encoding='utf-8'))
    packet = Path(original['packet']['path'])
    payloads = packet.parent / 'package-textures'
    summary = validate_package_catalog(manifest_path, packet)
    assert summary['unit_mapping_provenance_verified']
    passed = ['real exported package catalog and original mapping inventory']
    if legacy_manifest is not None:
        legacy = validate_package_catalog(legacy_manifest)
        assert not legacy['unit_mapping_provenance_verified']
        passed.append('legacy Equip catalog accepted without optional mapping proof')
    with tempfile.TemporaryDirectory(prefix='hades-package-catalog-') as temporary:
        scratch = Path(temporary)
        serial = 0

        def case():
            nonlocal serial
            serial += 1
            root = scratch / str(serial)
            root.mkdir()
            (root / 'packages.lua').symlink_to(packet.resolve())
            textures = root / 'package-textures'
            textures.mkdir()
            for source in payloads.iterdir():
                (textures / source.name).hardlink_to(source)
            return root, copy.deepcopy(original)

        def run(root, data):
            path = root / 'manifest.json'
            path.write_text(json.dumps(data), encoding='utf-8')
            return validate_package_catalog(path, root / 'packages.lua')

        def rejects(label, mutate):
            root, data = case()
            mutate(root, data)
            try:
                run(root, data)
            except (ValueError, OSError):
                passed.append(label)
            else:
                raise AssertionError('Accepted ' + label)

        def write_proof(root, data, value):
            path = root / 'mapping.json'
            path.write_text(json.dumps(value), encoding='utf-8')
            data['unit_package_names'] = identity(path)

        root, data = case()
        assert run(root, data)['texture_bytes'] == summary['texture_bytes']
        passed.append('caller-selected symlinked catalog validates its sibling payloads')
        rejects('damaged packet identity', lambda r, d: d['packet'].update(sha256='0' * 64))
        rejects('damaged original source identity', lambda r, d: d['sources'][0].update(sha256='0' * 64))
        rejects('changed aggregate payload extent', lambda r, d: d.update(texture_bytes=d['texture_bytes'] + 1))
        rejects('boolean aggregate extent', lambda r, d: d.update(texture_bytes=True))
        rejects('missing sibling payload despite valid symlink target', lambda r, d: next((r / 'package-textures').iterdir()).unlink())
        rejects('unexpected payload artifact', lambda r, d: (r / 'package-textures' / 'scratch.log').write_text('unexpected'))

        def damaged_pixels(root, data):
            path = next((root / 'package-textures').iterdir())
            pixels = path.read_bytes()
            path.unlink()  # Break the link before damaging only our scratch copy.
            path.write_bytes(bytes([pixels[0] ^ 1]) + pixels[1:])
        rejects('same-size damaged genuine RGBA payload', damaged_pixels)
        rejects('null mapping proof is not treated as an absent legacy field', lambda r, d: d.update(unit_package_names=None))
        rejects('mapping proof declared byte extent changed', lambda r, d: d['unit_package_names'].update(bytes=d['unit_package_names']['bytes'] + 1))

        def damaged_proof_bytes(root, data):
            path = root / 'mapping.json'
            path.write_bytes(Path(data['unit_package_names']['path']).read_bytes() + b' ')
            data['unit_package_names']['path'] = str(path)
        rejects('mapping proof actual bytes changed', damaged_proof_bytes)

        def changed_proof(root, data):
            value = copy.deepcopy(proof)
            value['native_read_game_data_rva'] = '0x0'
            write_proof(root, data, value)
        rejects('rehash cannot bless altered mapping provenance', changed_proof)
        if proof['mappings']:
            def changed_mapping(root, data):
                value = copy.deepcopy(proof)
                value['mappings'][next(iter(value['mappings']))] = '__ChangedPackage__'
                write_proof(root, data, value)
            rejects('rehash cannot bless a mapping inconsistent with original UnitData', changed_mapping)
        for field in ['source_files', 'package_stems', 'provenance']:
            def removed_field(root, data, key=field):
                value = copy.deepcopy(proof)
                value.pop(key)
                write_proof(root, data, value)
            rejects('mapping proof missing ' + field, removed_field)
        rejects('engine proof input must be bound by manifest sources', lambda r, d: d.update(sources=[row for row in d['sources'] if row['sha256'] != TARGET]))
        source_path = str(Path(proof['source_files'][0]['path']).resolve())
        rejects('unit proof input must be bound by manifest sources', lambda r, d: d.update(sources=[row for row in d['sources'] if str(Path(row['path']).resolve()) != source_path]))

        # The complete filename set drives native mapping, including unselected
        # packages. Retain genuine source files through links; mutate only this
        # temporary inventory after proving the unchanged copy is accepted.
        game = scratch / 'inventory-game'
        content = game / 'Content'
        directory = content / 'Win/Packages'
        directory.mkdir(parents=True)
        source_directory = Path(proof['package_directory'])
        for path in source_directory.glob('*.pkg'):
            (directory / path.name).symlink_to(path.resolve())
        (content / 'Game').mkdir()
        (content / 'Game/Units').symlink_to(source_directory.parent.parent / 'Game/Units', target_is_directory=True)
        (game / 'x64').symlink_to(source_directory.parent.parent.parent / 'x64', target_is_directory=True)
        root, data = case()
        value = copy.deepcopy(proof)
        value['package_directory'] = str(directory.resolve())
        write_proof(root, data, value)
        assert run(root, data)['unit_mappings'] == summary['unit_mappings']
        passed.append('identical genuine filename-inventory copy accepted')
        extra = directory / '__NewPackageInventoryEntry__.pkg'
        extra.symlink_to(next(source_directory.glob('*.pkg')).resolve())
        try:
            run(root, data)
        except ValueError:
            passed.append('added unselected package filename invalidates stale mapping proof')
        else:
            raise AssertionError('Added original inventory name was ignored')
        extra.unlink()
        selected_names = {name.lower() for name in original['packages']}
        removed = next(path for path in directory.iterdir() if path.stem.lower() not in selected_names)
        removed.unlink()
        try:
            run(root, data)
        except ValueError:
            passed.append('removed unselected package filename invalidates stale mapping proof')
        else:
            raise AssertionError('Removed original inventory name was ignored')
    return {'checks_passed': len(passed), 'passed': passed, 'real_catalog': summary,
            'no_mocked_providers': True, 'all_temporary_copies_removed': True,
            'lua_reference_closure_verified': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--legacy-manifest', type=Path)
    args = parser.parse_args()
    print(json.dumps(check(args.manifest, args.legacy_manifest), indent=2))


if __name__ == '__main__':
    main()
