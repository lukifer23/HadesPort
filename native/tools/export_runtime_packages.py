#!/usr/bin/env python3.13
"""Export source-bound SGG package closures and native unit-package names.

Only original atlas pixels are decoded. Includes retain their authored names;
unit mappings retain exact runtime keys and actual package-basename values.
"""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re
import tempfile

from hades_assets import Package, xnb_image
from export_actor_catalog import literal as lua_literal
from export_room_creation import RangedSjson

ROOT = Path(__file__).resolve().parents[2]
TARGET = 'd98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'


def identity(path):
    path = Path(path).resolve()
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    return {'path': str(path), 'sha256': digest, 'bytes': path.stat().st_size}


def package_name(name):
    if not isinstance(name, str) or not re.fullmatch(r'[A-Za-z0-9_-]+', name):
        raise ValueError('Invalid original package name: ' + repr(name))
    return name


def package_inventory(content):
    """The basename set, rather than payload bytes, is this mapping input."""
    names = {}
    for path in sorted((content / 'Win/Packages').glob('*.pkg')):
        name = package_name(path.stem)
        folded = name.lower()
        if folded in names:
            raise ValueError('Ambiguous original package basename: ' + name)
        names[folded] = name
    if not names:
        raise ValueError('Original package inventory is empty')
    return names


def unit_package_names(content, inventory):
    """Rebuild ReadGameData b7518..b77f7 from original UnitData and filenames.

    Root-name and parent comparisons use stricmp. The own-package boundary is
    an exact-case hash-set lookup; GetUnitPackageName itself is exact-case.
    GameData PDB mName=+8 and mInheritFrom=+0x38, not a parent-list heuristic.
    """
    units, folded, sources = {}, {}, []
    for path in sorted((content / 'Game/Units').glob('*.sjson')):
        raw = path.read_bytes()
        source = identity(path)
        if source['sha256'] != hashlib.sha256(raw).hexdigest():
            raise ValueError('Original unit source changed while reading: ' + str(path))
        text = raw.decode('utf-8')
        parser = RangedSjson(text)
        if text.startswith('\ufeff'):
            parser.pos = 1
        parsed = parser.parse()
        rows = parsed.get('Units')
        if not isinstance(rows, list):
            raise ValueError('Original unit source has no Units array: ' + str(path))
        sources.append(source)
        for index, row in enumerate(rows):
            if not isinstance(row, dict):
                raise ValueError('Original UnitData must be an object')
            name, parent = row.get('Name'), row.get('InheritFrom')
            if not isinstance(name, str) or not name or not name.isascii() or '\0' in name:
                raise ValueError('Original UnitData name unsupported: ' + repr(name))
            if parent is not None and (not isinstance(parent, str) or not parent.isascii() or '\0' in parent):
                raise ValueError('Native UnitData requires single text InheritFrom: ' + name)
            if name.lower() in folded:
                raise ValueError('Ambiguous original UnitData name: ' + name)
            start, size = parser.ranges[id(row)]
            units[name] = {'parent': parent, 'source': source, 'record_index': index,
                           'range': [start, size], 'range_sha256': hashlib.sha256(raw[start:start + size]).hexdigest()}
            folded[name.lower()] = name
    if not units:
        raise ValueError('Original UnitData inventory is empty')
    children = defaultdict(list)
    for name, row in units.items():
        parent = row['parent']
        if parent:
            if parent.lower() not in folded:
                raise ValueError('Original UnitData parent absent: ' + name + ' -> ' + parent)
            children[folded[parent.lower()]].append(name)
    # Reject invalid source inheritance independently of package boundaries.
    complete, active = set(), set()
    def visit(name):
        if name in active:
            raise ValueError('Original UnitData inheritance cycle: ' + name)
        if name in complete:
            return
        active.add(name)
        if units[name]['parent']:
            visit(folded[units[name]['parent'].lower()])
        active.remove(name)
        complete.add(name)
    for name in units:
        visit(name)
    package_stems = set(inventory.values())
    mappings, provenance, roots = {}, {}, []
    for stem in sorted(package_stems):
        root = folded.get(stem.lower())
        if root is None:
            continue
        roots.append({'unit': root, 'package': stem})
        pending = [(root, [])]
        while pending:
            name, ancestors = pending.pop()
            if name in mappings:
                if mappings[name] != stem:
                    raise ValueError('Ambiguous native unit-package mapping: ' + name)
                continue
            mappings[name] = stem
            chain = ancestors + [name]
            provenance[name] = {'package': stem, 'root': root, 'chain': chain,
                                'unit_records': {key: units[key] for key in chain}}
            # b77b2..b77f7 deliberately tests exact child spelling here.
            pending.extend((child, chain) for child in children[name] if child not in package_stems)
    stems = sorted(package_stems)
    proof = {'target_sha256': TARGET, 'native_read_game_data_rva': '0xb6df0',
             'root_and_parent_match': 'ASCII stricmp', 'own_package_boundary': 'exact-case package basename membership',
             'runtime_lookup': 'exact-case unit key; actual package basename value',
             'package_directory': str((content / 'Win/Packages').resolve()),
             'package_stems': stems, 'package_stems_sha256': hashlib.sha256(json.dumps(stems, separators=(',', ':')).encode()).hexdigest(),
             'package_inventory_scope': 'Actual .pkg filenames only; unselected payload bytes do not affect this mapping',
             'unit_count': len(units), 'roots': roots, 'source_files': sources,
             'mappings': dict(sorted(mappings.items())), 'provenance': dict(sorted(provenance.items()))}
    return proof['mappings'], proof


def export(content, output, names):
    content = Path(content).resolve()
    selected = list(dict.fromkeys(package_name(name) for name in names))
    if not selected:
        raise ValueError('Select original package names')
    output = Path(output)
    if output.exists() or output.is_symlink():
        raise ValueError('Choose a fresh output directory')
    output = output.resolve()
    original_root = content.parent if content.name.lower() == 'content' else content
    if output.is_relative_to(original_root) or output.is_relative_to((ROOT / '.local/game').resolve()):
        raise ValueError('Package output cannot overwrite original game files')
    engine = identity(content.parent / 'x64/EngineWin64s.dll')
    if engine['sha256'] != TARGET:
        raise ValueError('Unit-package mapping requires the matching original engine')
    inventory = package_inventory(content)
    mappings, proof = unit_package_names(content, inventory)
    # All validation/publication happens under one fresh sibling directory.
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='packages-stage-', dir=output.parent) as temporary:
        work = Path(temporary) / output.name
        work.mkdir()
        textures = work / 'package-textures'
        textures.mkdir()
        catalog = {'schema': 'hades-native-asset-packages-v1', 'packages': {}, 'unit_package_names': mappings}
        sources = {row['path']: row for row in [engine, *proof['source_files']]}
        texture_names, visiting, active_files = {}, set(), set()

        def load(name):
            name = package_name(name)
            if name in visiting:
                raise ValueError('Original package include cycle: ' + name)
            if name in catalog['packages']:
                return
            source_name = inventory.get(name.lower())
            if source_name is None:
                raise ValueError('Original package absent: ' + name)
            if source_name in active_files:
                raise ValueError('Original package include cycle through filename alias: ' + name)
            visiting.add(name)
            active_files.add(source_name)
            stem = content / 'Win/Packages' / source_name
            manifest, payload = stem.with_suffix('.pkg_manifest'), stem.with_suffix('.pkg')
            before = [identity(manifest), identity(payload)]
            atlas, includes, entries, local_names = {}, [], [], set()

            def include(dependency):
                dependency = package_name(dependency)
                if dependency not in includes:
                    includes.append(dependency)
                    load(dependency)

            for entry in Package(manifest).entries():
                if entry['type'] == 'include':
                    include(entry['name'])
                elif entry['type'] == 'atlas':
                    if entry['name'] in atlas:
                        raise ValueError('Duplicate original atlas manifest: ' + entry['name'])
                    atlas[entry['name']] = entry
                else:
                    raise ValueError('Unsupported original manifest entry: ' + entry['type'])
            for entry in Package(payload).entries():
                if entry['type'] == 'include':
                    include(entry['name'])
                    continue
                if entry['type'] != 'texture':
                    raise ValueError('Unsupported portable package entry: ' + entry['type'])
                if entry['name'] in local_names:
                    raise ValueError('Duplicate original package texture: ' + entry['name'])
                local_names.add(entry['name'])
                meta = atlas.pop(entry['name'], None)
                if meta is None:
                    raise ValueError('Texture has no source atlas manifest: ' + entry['name'])
                image, _ = xnb_image(entry['data'])
                rgba = image.tobytes()
                digest = hashlib.sha256(rgba).hexdigest()
                target = textures / (digest + '.rgba')
                if not target.exists():
                    target.write_bytes(rgba)
                elif target.stat().st_size != len(rgba):
                    raise ValueError('Conflicting content-hash texture extent')
                row = {'name': entry['name'], 'width': image.width, 'height': image.height,
                       'path': 'package-textures/' + target.name, 'sha256': digest,
                       'sprites': meta['sprites'], 'xnb_sha256': hashlib.sha256(entry['data']).hexdigest()}
                previous = texture_names.get(row['name'])
                if previous is not None and previous != row:
                    raise ValueError('Conflicting package texture name identity: ' + row['name'])
                texture_names[row['name']] = row
                entries.append(row)
                del rgba, image
            if atlas:
                raise ValueError('Unresolved atlas payloads: ' + ', '.join(atlas))
            for row in before:
                if identity(row['path']) != row:
                    raise ValueError('Original package changed while exporting: ' + row['path'])
                sources[row['path']] = row
            catalog['packages'][name] = {'manifest_sha256': before[0]['sha256'],
                                         'package_sha256': before[1]['sha256'],
                                         'includes': includes, 'textures': entries}
            visiting.remove(name)
            active_files.remove(source_name)

        for name in selected:
            load(name)
        # Includes precede their first parent in traversal order. Requested
        # root spelling/order remains recorded independently in the receipt.
        packet = work / 'packages.lua'
        packet.write_text('return ' + lua_literal(catalog) + '\n')
        mapping_proof = work / 'unit-package-names.json'
        mapping_proof.write_text(json.dumps(proof, indent=2) + '\n')
        packet_identity = identity(packet)
        packet_identity['path'] = str(output / packet.name)
        proof_identity = identity(mapping_proof)
        proof_identity['path'] = str(output / mapping_proof.name)
        manifest = {'schema': catalog['schema'], 'target_sha256': TARGET,
                    'sources': list(sources.values()), 'packet': packet_identity,
                    'unit_package_names': proof_identity, 'requested_names': selected,
                    'packages': list(catalog['packages']),
                    'texture_bytes': sum(path.stat().st_size for path in textures.iterdir()),
                    'limits': ['Original source pixels/atlas geometry, decoded to portable RGBA. CPU residency and GPU texture uploads are distinct states; no audio or movie coverage implied.',
                               'Unit mapping binds actual package filenames and original single-parent UnitData; unsupported or ambiguous source graphs fail explicitly.']}
        (work / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        # Mapping inputs include the complete filename set, not only packages
        # selected for decoding. Reject a changing source inventory as well as
        # changed file bytes before making the complete export visible.
        if package_inventory(content) != inventory:
            raise ValueError('Original package filename inventory changed while exporting')
        for row in sources.values():
            if identity(row['path']) != row:
                raise ValueError('Original package/unit source changed before publication: ' + row['path'])
        if output.exists() or output.is_symlink():
            raise ValueError('Choose a fresh output directory')
        work.rename(output)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--content', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--names', nargs='+', required=True)
    args = parser.parse_args()
    result = export(args.content, args.output, args.names)
    print(json.dumps({'packet': result['packet'], 'packages': result['packages']}))


if __name__ == '__main__':
    main()
