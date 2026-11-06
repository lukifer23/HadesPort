"""Validate exported package inputs before copying them into an asset stage.

Legacy Equip catalogs have no unit-name proof. New catalogs bind that proof to
the actual filename inventory and UnitData using the exporter's owning parser.
This checks available RGBA payloads and their aggregate extent; it does not parse
Lua to establish that every referenced texture exists or that every file is used.
"""
import json
from pathlib import Path
import re

from export_runtime_packages import TARGET, identity, package_inventory, unit_package_names


def verified_identity(row, label, selected=None):
    if not isinstance(row, dict) or not isinstance(row.get('path'), str) or not row['path']:
        raise ValueError(label + ' source path missing')
    if type(row.get('bytes')) is not int or row['bytes'] < 0:
        raise ValueError(label + ' source byte extent invalid')
    if not isinstance(row.get('sha256'), str) or not re.fullmatch(r'[0-9a-f]{64}', row['sha256']):
        raise ValueError(label + ' source digest invalid')
    path = Path(row['path'] if selected is None else selected)
    if not path.is_file():
        raise ValueError(label + ' source file unavailable: ' + str(path))
    actual = identity(path)
    if actual['bytes'] != row['bytes'] or actual['sha256'] != row['sha256']:
        raise ValueError(label + ' source identity changed: ' + str(path))
    return actual


def validate_package_catalog(manifest_path, packet_path=None):
    manifest_path = Path(manifest_path)
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    if not isinstance(manifest, dict) or manifest.get('schema') != 'hades-native-asset-packages-v1':
        raise ValueError('Unrecognized package manifest')
    if 'target_sha256' in manifest and manifest['target_sha256'] != TARGET:
        raise ValueError('Package manifest engine target mismatch')
    packet = verified_identity(manifest.get('packet'), 'Package catalog', packet_path)
    # Copying stages use the caller-selected packet's sibling payloads. A
    # symlinked Lua file must not silently select its target's payload folder.
    selected_packet = Path(manifest['packet']['path'] if packet_path is None else packet_path)
    packet_root = selected_packet.absolute().parent
    source_rows = manifest.get('sources')
    if not isinstance(source_rows, list):
        raise ValueError('Package manifest source list missing')
    sources = {}
    for row in source_rows:
        actual = verified_identity(row, 'Original package/unit')
        previous = sources.get(actual['path'])
        if previous is not None and previous != actual:
            raise ValueError('Conflicting original package source identity')
        sources[actual['path']] = actual

    texture_root = packet_root / 'package-textures'
    if not texture_root.is_dir():
        raise ValueError('Decoded package texture directory unavailable')
    texture_bytes, texture_files = 0, 0
    for path in texture_root.iterdir():
        if not path.is_file() or not re.fullmatch(r'[0-9a-f]{64}\.rgba', path.name):
            raise ValueError('Unexpected package texture artifact: ' + path.name)
        actual = identity(path)
        if actual['sha256'] != path.stem:
            raise ValueError('Decoded package texture changed: ' + path.name)
        texture_bytes += actual['bytes']
        texture_files += 1
    declared_bytes = manifest.get('texture_bytes')
    if type(declared_bytes) is not int or declared_bytes < 0 or declared_bytes != texture_bytes:
        raise ValueError('Decoded package texture aggregate extent changed')

    mapping_count, inventory_count = 0, 0
    has_proof = 'unit_package_names' in manifest
    proof_row = manifest.get('unit_package_names')
    if has_proof:
        proof_identity = verified_identity(proof_row, 'Unit-package mapping proof')
        proof = json.loads(Path(proof_identity['path']).read_text(encoding='utf-8'))
        if not isinstance(proof, dict) or not isinstance(proof.get('package_directory'), str):
            raise ValueError('Unit-package mapping source directory missing')
        package_directory = Path(proof['package_directory']).resolve()
        if package_directory.name != 'Packages' or package_directory.parent.name != 'Win':
            raise ValueError('Unit-package mapping source directory invalid')
        content = package_directory.parent.parent
        inventory = package_inventory(content)
        mappings, expected = unit_package_names(content, inventory)
        if proof != expected:
            raise ValueError('Unit-package mapping proof differs from current original unit/package inventory')
        engine = identity(content.parent / 'x64/EngineWin64s.dll')
        if engine['sha256'] != TARGET:
            raise ValueError('Unit-package mapping engine identity mismatch')
        for row in [engine, *expected['source_files']]:
            actual = verified_identity(row, 'Unit-package mapping input')
            if sources.get(actual['path']) != actual:
                raise ValueError('Unit-package mapping input is not bound by package manifest sources')
        mapping_count, inventory_count = len(mappings), len(inventory)

    return {'packet_sha256': packet['sha256'], 'source_files': len(sources),
            'texture_files': texture_files, 'texture_bytes': texture_bytes,
            'unit_mapping_provenance_verified': has_proof,
            'unit_mappings': mapping_count, 'package_inventory_names': inventory_count,
            'lua_texture_reference_closure_verified': False,
            'scope': 'Packet/source identities, available RGBA bytes and aggregate extent; optional mapping provenance rederived from current original inputs. Lua texture-reference membership remains a runtime consumer check.'}
