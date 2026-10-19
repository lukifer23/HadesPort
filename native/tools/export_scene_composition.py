#!/usr/bin/env python3
"""Bind original room group boundaries to unchanged HDS3 sprite ordering."""
import argparse
import collections
import json
import struct
from pathlib import Path
from build_room_scene import identity


def export(content, scene_dir, output):
    if output.exists():
        raise ValueError('Output must be new')
    source = scene_dir / 'scene-manifest.json'
    manifest = json.loads(source.read_text())
    scene = identity(scene_dir / 'scene-affine.bin')
    if scene['sha256'] != manifest['scene']['sha256']:
        raise ValueError('Scene identity mismatch')
    map_path = content / 'Maps' / (manifest['room'] + '.map_text')
    meta = json.loads(map_path.read_text())
    by_name = {g['Id']['Name']: g for g in meta['ThingGroups']}
    flattened = []
    def visit(group):
        if group in flattened:
            raise ValueError('Duplicate/cyclic group')
        for child in reversed(group['ChildGroups']):
            actual = by_name[child['Name']]
            if actual['Id'] != child or actual['ParentGroup'] != group['Id']:
                raise ValueError('Group reference mismatch')
            visit(actual)
        flattened.append(group)
    for group in meta['ThingGroups']:
        if group['ParentGroup']['Id'] == 0:
            visit(group)
    if len(flattened) != len(by_name):
        raise ValueError('Incomplete group graph')
    index = {g['Id']['Name']: i for i, g in enumerate(flattened)}
    records = []
    last = -1
    runs = []
    for i, sprite in enumerate(manifest['sprites']):
        group_id = min(index[g] for g in sprite['group_names'])
        group = flattened[group_id]
        if group_id != sprite['source_layer'] or group['BlendMode'] != sprite['layer_blend_mode']:
            raise ValueError('Source group/material mismatch')
        if group_id < last:
            raise ValueError('Noncontiguous source group')
        last = group_id
        records.append({'sprite_index': i, 'object_id': sprite['object_id'],
            'group_index': group_id, 'group_name': group['Id']['Name'],
            'blend_mode': group['BlendMode']})
        if not runs or runs[-1]['group_index'] != group_id:
            runs.append({'start': i, 'count': 0, 'group_index': group_id,
                'group_name': group['Id']['Name'], 'blend_mode': group['BlendMode']})
        runs[-1]['count'] += 1
    affected = [r for r in runs if r['blend_mode'] in (9, 11)]
    # Existing renderer inserts the original hero in Standing's native layer.
    standing = index['Standing']
    if any(r['group_index'] == standing for r in affected):
        raise ValueError('Hero would split a composed group')
    output.mkdir(parents=True)
    with (output / 'composition.bin').open('xb') as f:
        f.write(b'HGC1' + bytes.fromhex(scene['sha256']) + struct.pack('<I', len(records)))
        for record in records:
            f.write(struct.pack('<I', record['group_index']))
    result = {'schema': 'hades-source-group-composition-v1',
        'packet': identity(output / 'composition.bin'), 'scene': scene,
        'sources': [identity(source), identity(map_path)],
        'format': 'HGC1,32byte sceneSHA256,u32spriteCount; eachsprite u32 originalGroupIndex',
        'groups': [{'index': i, 'source': g} for i, g in enumerate(flattened)],
        'records': records, 'runs': runs, 'composed_runs': affected,
        'hero': {'existing_insertion_group': standing, 'name': 'Standing',
            'can_split_composed_group': False},
        'software_composition_profile': {
            '9': {'clear_rgba8': [128, 128, 128, 128], 'inside_group': 'premultiplied source-over', 'final': 'Overlay2D,opaque'},
            '11': {'clear_rgba8': [0, 0, 0, 255], 'inside_group': 'premultiplied source-over', 'final': 'LighterColor2D,opaque'}},
        'limits': ['Selects the recovered original software composition route; original UseOverlayHardware default is not asserted.',
            'Applies only to verified static source group ordering; future dynamic draw-group mutations need runtime integration.',
            'Original scene and animation bytes remain unchanged.']}
    (output / 'manifest.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'packet': result['packet'], 'composed_runs': affected, 'hero': result['hero']}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--content', type=Path, required=True)
    parser.add_argument('--scene', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    export(args.content, args.scene, args.output)
