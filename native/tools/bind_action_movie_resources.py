#!/usr/bin/env python3.13
"""Bind original movie Books to the shared action Animation consumer.

Writes a fresh resource version. Atlas pixels remain the validated original
packet; movie pixels remain the independently validated HAA packet. No pixels,
timing, colors, children, or missing native defaults are synthesized here.
"""
import argparse
import copy
import json
import os
from pathlib import Path

from build_room_scene import identity
from export_effect_initialization import lua
from animation_postread import normalize_animation_postread


def bind(base, movies, output):
    if output.exists():
        raise ValueError('Fresh movie/action output required')
    report = json.loads((base / 'manifest.json').read_text())
    catalog = json.loads((base / 'catalog.json').read_text())
    actor = json.loads((movies / 'manifest.json').read_text())
    packet = base / 'action-effect-frames.bin'
    movie_packet = movies / 'actor-animations.bin'
    if identity(packet)['sha256'] != report['packet']['sha256']:
        raise ValueError('Atlas source packet identity changed')
    if identity(movie_packet)['sha256'] != actor['packet']['sha256']:
        raise ValueError('Movie source packet identity changed')
    definitions = {}
    for clip in actor['clips']:
        definition = clip['definition']
        if definition.get('Type') != 'Book' or not definition.get('VideoTexture'):
            continue
        if not clip['flags'] & 512:
            raise ValueError('Movie Book lacks native Book type: ' + clip['name'])
        if clip['name'] in catalog['Definitions']:
            raise ValueError('Animation bound to two image backends: ' + clip['name'])
        definitions[clip['name']] = normalize_animation_postread(copy.deepcopy(definition))
    if not definitions:
        raise ValueError('No actual original movie Books to bind')
    catalog['MovieDefinitions'] = definitions
    catalog['MovieBankSha256'] = actor['packet']['sha256']
    for name in definitions:
        catalog['Rejected'].pop(name, None)
    output.mkdir(parents=True)
    os.link(packet, output / packet.name)
    (output / 'catalog.json').write_text(json.dumps(catalog, indent=1) + '\n')
    (output / 'action-effects.lua').write_text('return ' + lua(catalog) + '\n')
    report['packet'] = identity(output / packet.name)
    report['catalog'] = identity(output / 'catalog.json')
    report['lua'] = identity(output / 'action-effects.lua')
    report['movie_bank'] = identity(movie_packet)
    report['movie_source_manifest'] = identity(movies / 'manifest.json')
    report['base_action_manifest'] = identity(base / 'manifest.json')
    report['movie_definitions'] = sorted(definitions)
    report['statistics']['movie_definitions'] = len(definitions)
    report['statistics']['rejected'] = len(catalog['Rejected'])
    report['movie_runtime_contract'] = {
        'factory': 'AnimationManager.CreateCustom29f870/AddChild293ae0 use the same native Book allocation and child lifetime as atlas Books.',
        'timing': 'Original Book.Init2af310/SetupFrameUpdateMode2b1040/UpdateInternal2af530; inherited SlideData does not execute.',
        'images': 'Original source VideoTexture frames and atlas geometry from the hash-bound HAA packet; no replacement pixels.',
        'coverage': 'Resource binding only; all actual source fields and dependency consumers remain checked by the native runtime.'}
    (output / 'manifest.json').write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps({'movie_definitions': len(definitions), 'movie_bank_sha256': actor['packet']['sha256'], 'output': str(output)}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('base', 'movies', 'output'):
        parser.add_argument('--' + key, type=Path, required=True)
    args = parser.parse_args()
    bind(args.base, args.movies, args.output)
