#!/usr/bin/env python3
"""Export native ambient precedence as a companion to the unchanged v5 scene."""
import argparse
import collections
import hashlib
import json
import math
import struct
from pathlib import Path
from build_room_scene import Sjson, merge, identity

PREDEFINED = {'Default': 1.0, 'Unlit': 0.0, 'Emissive': 0.0, 'Shadowed': 0.699999988079071}
DLL_SHA = 'd98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'


def material_ambient(definition, default):
    value = default
    provenance = 'compiled_default'
    if 'Material' in definition:
        material = definition['Material']
        if isinstance(material, str):
            if material not in PREDEFINED:
                raise ValueError('Unverified named material: ' + material)
            # FromName compares key pointers and returns zero on a miss. The
            # actual room uses only zero-valued names, so its result is exact
            # independent of unresolved string interning/lookup behavior.
            if material not in ('Unlit', 'Emissive'):
                raise ValueError('Nonzero named-material lookup requires separately verified key identity')
            value = PREDEFINED[material]
            provenance = 'Material/' + material
        elif isinstance(material, dict):
            if 'Ambient' in material:
                value = float(material['Ambient'])
                provenance = 'Material.Ambient'
        else:
            raise ValueError('Unsupported Material node')
    if 'Ambient' in definition:
        explicit = float(definition['Ambient'])
        if 'Material' in definition and value != explicit:
            raise ValueError('Source has differing Material and Ambient; loading order unverified')
        value, provenance = explicit, 'Ambient'
    if not math.isfinite(value):
        raise ValueError('Non-finite ambient')
    return value, provenance


def export(content, scene_dir, creation_path, output):
    if output.exists():
        raise ValueError('Output must be new')
    if identity(content.parent / 'x64/EngineWin64s.dll')['sha256'] != DLL_SHA:
        raise ValueError('Native defaults require matching DLL')
    manifest = json.loads((scene_dir / 'scene-manifest.json').read_text())
    scene = scene_dir / 'scene-affine.bin'
    scene_id = identity(scene)
    if scene_id['sha256'] != manifest['scene']['sha256']:
        raise ValueError('Scene/manifest hash mismatch')
    creation = json.loads(creation_path.read_text())
    things = {record['id']: record for record in creation['records']}
    sources = [identity(scene_dir / 'scene-manifest.json'), identity(creation_path)]
    definitions = {}
    for path in sorted((content / 'Game/Animations').glob('*.sjson')):
        sources.append(identity(path))
        for ordinal, definition in enumerate(Sjson(path.read_text(encoding='utf-8-sig')).parse().get('Animations', [])):
            name = definition['Name']
            if name in definitions:
                raise ValueError('Duplicate animation: ' + name)
            definitions[name] = (definition, str(path), ordinal)
    resolved = {}
    def resolve(name, stack=()):
        if name in resolved:
            return resolved[name]
        if name in stack:
            raise ValueError('Animation cycle')
        own, path, ordinal = definitions[name]
        parent = own.get('InheritFrom')
        base, chain = resolve(parent, stack + (name,)) if parent else ({}, [])
        result = merge(base, own), [{'name': name, 'path': path, 'ordinal': ordinal}] + chain
        resolved[name] = result
        return result
    map_path = content / 'Maps' / (manifest['room'] + '.map_text')
    meta = json.loads(map_path.read_text())
    global_ambient = [meta['AmbientLightColor'][axis] for axis in ('X', 'Y', 'Z')]
    if any(not math.isfinite(v) or v < 0 or v > 1 for v in global_ambient):
        raise ValueError('Map ambient outside validated room profile')
    sources.append(identity(map_path))
    records = []
    for index, sprite in enumerate(manifest['sprites']):
        thing = things[sprite['object_id']]
        body, body_source = material_ambient(thing['resolved_thing'], 1.0)
        if thing['raw_map']['Ambient'] >= 0:
            body, body_source = thing['raw_map']['Ambient'], 'MapThing.Ambient'
        name = sprite.get('animation')
        animation, chain = resolve(name) if name else ({}, [])
        anim_ambient, anim_source = material_ambient(animation, -1.0)
        ambient, source = (anim_ambient, 'Animation/' + anim_source) if anim_ambient >= 0 else (body, 'Body/' + body_source)
        if ambient < 0:
            ambient, source = 1.0, 'Drawable fallback1'
        if not math.isfinite(ambient) or not 0 <= ambient <= 1:
            raise ValueError('Ambient outside verified source profile')
        # These original room entries are plain atlas sprites or recovered
        # Constant/Book clips. Mask/custom color effects are not guessed.
        unknown = [key for key in ('Mask', 'MaskName', 'Shader', 'ShaderName', 'ReplaceHue', 'ColorLerp') if key in animation]
        shader_class = 0 if unknown else 1
        records.append({'sprite_index': index, 'object_id': sprite['object_id'],
            'name': sprite['name'], 'animation': name, 'animation_inheritance': chain,
            'definition_key': thing['definition_key'], 'map_source_range': thing['source_range'],
            'body_ambient': body, 'body_provenance': body_source,
            'animation_ambient': anim_ambient, 'animation_provenance': anim_source,
            'resolved_ambient': ambient, 'provenance': source,
            'shader_class': shader_class,
            'world_shader': ('AddColor/Lighting3DFast.vert' if sprite['material']['add_color'] else 'Lighting3DFast') if shader_class else 'unsupported',
            'unknown_effect_fields': unknown,
            'rgb_multiplier': [1 + (v - 1) * ambient for v in global_ambient] if shader_class else [1, 1, 1]})
    output.mkdir(parents=True)
    with (output / 'ambient.bin').open('xb') as f:
        f.write(b'HLA1' + bytes.fromhex(scene_id['sha256']) + struct.pack('<I3f', len(records), *global_ambient))
        for record in records:
            f.write(struct.pack('<fI', record['resolved_ambient'], record['shader_class']))
    result = {'schema': 'hades-source-ambient-v1', 'packet': identity(output / 'ambient.bin'),
        'format': 'HLA1,32byte sceneSHA256,u32spriteCount,3float globalAmbientRGB; eachrecord floatambient,u32shaderClass(0unchanged,1worldfast)',
        'scene': scene_id, 'global_ambient': global_ambient,
        'counts': {'sprites': len(records), 'shader_classes': dict(collections.Counter(r['shader_class'] for r in records)),
            'ambient_values': dict(collections.Counter(str(r['resolved_ambient']) for r in records)),
            'unchanged_unit_multiplier': sum(r['rgb_multiplier'] == [1, 1, 1] for r in records)},
        'records': records, 'sources': sources,
        'native_defaults': {'ThingData.Ambient': {'value': 1.0, 'va': '0x1800eb7e3'},
            'AnimationData.Ambient': {'value': -1.0, 'va': '0x18009425f'},
            'MaterialManager.Predefined': PREDEFINED},
        'limitations': ['Ambient vertex-color multiplier only; original light sprites, shadow maps, bloom and exact group composition remain separate work.',
            'Original tiny position dither omitted because HDS3 stores projected XY rather than native shader XYZ.',
            'Original v5 texture/quad/animation files remain unchanged; per-sprite metadata is hash-bound to the exact scene.',
            'Material object source and scalar Ambient precedence with differing simultaneous values is rejected rather than guessed.']}
    (output / 'manifest.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'packet': result['packet'], 'counts': result['counts']}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--content', type=Path, required=True)
    parser.add_argument('--scene', type=Path, required=True)
    parser.add_argument('--creation', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    export(args.content, args.scene, args.creation, args.output)
