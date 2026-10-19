#!/usr/bin/env python3
"""Build the standalone native ARM64 reconstruction with bundled original assets.

The current engine displays a decoded original room. It is not a complete game.
Only package with actual asset-reconstruction output; never invent fallback art.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import struct
import sys
import zipfile

PROJECT = Path(__file__).resolve().parents[1]
SDK = Path(os.environ.get('ANDROID_HOME') or os.environ.get('ANDROID_SDK_ROOT') or Path.home()/'Library/Android/sdk')
NDK = SDK / 'ndk/29.0.14206865'
JAVA = Path(os.environ.get('JAVA_HOME') or '/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home')
BUILD = PROJECT / '.local/native-build'
TOOLS = SDK / 'build-tools/37.0.0'


def run(command):
    subprocess.run([str(arg) for arg in command], check=True,
                   env=dict(os.environ, JAVA_HOME=str(JAVA)))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--scene', type=Path)
    parser.add_argument('--geometry', type=Path, help='Decoded original room geometry, if stored separately from the scene')
    parser.add_argument('--animations', type=Path, help='HDA1 original-frame animation companion bound to this exact HDS3 scene')
    parser.add_argument('--creation', type=Path, default=PROJECT / '.local/native-assets/opening-creation-v2/creation.json',
                        help='Source-verified room creation metadata and resolved definition defaults')
    parser.add_argument('--hero', type=Path, default=PROJECT / '.local/native-assets/hero-eight-directions')
    parser.add_argument('--ui', type=Path, default=PROJECT / '.local/native-assets/native-ui-v1/ui',
                        help='Decoded original menu textures and original SpriteFont data')
    parser.add_argument('--ambient', type=Path, default=PROJECT / '.local/native-assets/opening-ambient-v1/ambient.bin',
                        help='Optional scene-bound original material ambient companion')
    parser.add_argument('--composition', type=Path, default=PROJECT / '.local/native-assets/opening-composition-v1/composition.bin')
    parser.add_argument('--collision', type=Path, default=PROJECT / '.local/native-assets/opening-collision-v1/room-geometry.json',
                        help='Source-verified deterministic collision transforms')
    parser.add_argument('--weapons', type=Path, default=PROJECT / '.local/native-assets/player-weapons-v19/weapons.bin',
                        help='Source-bound staged weapon initialization catalog; no firing support')
    parser.add_argument('--actors',type=Path,default=PROJECT / '.local/native-assets/opening-actors-v5/actors.lua')
    parser.add_argument('--parallax',type=Path,default=PROJECT / '.local/native-assets/opening-parallax-v1/parallax.bin')
    parser.add_argument('--hulls',type=Path,default=PROJECT / '.local/native-assets/opening-hulls-v1/hulls.bin')
    parser.add_argument('--packages',type=Path,default=PROJECT / '.local/native-assets/player-packages-v1/packages.lua')
    parser.add_argument('--actor-animations',type=Path,default=PROJECT / '.local/native-assets/hero-actions-v4/actor-animations.bin')
    parser.add_argument('--effects',type=Path,default=PROJECT / '.local/native-assets/opening-spawners-v1/effects.lua')
    parser.add_argument('--actor-markers',type=Path,default=PROJECT / '.local/native-assets/hero-markers-v4/actor-markers.bin')
    parser.add_argument('--actor-properties',type=Path,default=PROJECT / '.local/native-assets/hero-properties-v6/actor-properties.bin')
    parser.add_argument('--actor-lights',type=Path,default=PROJECT / '.local/native-assets/actor-lights-opening-v1/manifest.json')
    parser.add_argument('--text-resources',type=Path,default=PROJECT / '.local/native-assets/original-text-v6/manifest.json')
    parser.add_argument('--action-effects',type=Path,default=PROJECT / '.local/native-assets/action-effects-v8/manifest.json')
    parser.add_argument('--screen-animations',type=Path,default=PROJECT / '.local/native-assets/screen-opening-v8/manifest.json')
    parser.add_argument('--particle-frames',type=Path,default=PROJECT / '.local/native-assets/cinder-frames-v1/cinder-frames.bin')
    parser.add_argument('--primaries',type=Path,default=PROJECT / '.local/native-assets/opening-primaries-v4/manifest.json')
    parser.add_argument('--bloom-settings',type=Path,default=PROJECT / '.local/native-assets/bloom-settings-v1/manifest.json')
    parser.add_argument('--color-grading',type=Path,default=PROJECT / '.local/native-assets/color-grading-v1/manifest.json')
    parser.add_argument('--audio-events',type=Path,default=PROJECT / '.local/native-assets/audio-events-v19/receipt.json')
    parser.add_argument('--room-leaf-frames',type=Path,default=PROJECT / '.local/native-assets/opening-leaf-frames-v1/room-leaf-frames.bin')
    parser.add_argument('--app-icon',type=Path,default=PROJECT / '.local/native-assets/app-icon-v1/manifest.json',
                        help='Original Hades.exe icon mipmaps (native/tools/export_app_icon.py)')
    parser.add_argument('--movie-preloads',type=Path,default=PROJECT / '.local/native-assets/movie-preloads-v1/movie-preloads.lua')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--compile-only', action='store_true')
    mode.add_argument('--assets-only', action='store_true', help='Stage original data for Mac debugging without building an APK')
    args = parser.parse_args()
    if not args.compile_only and (args.scene is None or not args.scene.is_file()):
        parser.error('--scene must point to a decoded original HDS1/HDS2/HDS3 room')
    BUILD.mkdir(parents=True, exist_ok=True)
    if not args.assets_only:
        run(['cmake', '-S', PROJECT / 'native/app', '-B', BUILD / 'cmake', '-G', 'Ninja',
             f'-DCMAKE_TOOLCHAIN_FILE={NDK}/build/cmake/android.toolchain.cmake',
             '-DANDROID_ABI=arm64-v8a', '-DANDROID_PLATFORM=android-26',
             '-DANDROID_STL=c++_static', '-DCMAKE_BUILD_TYPE=RelWithDebInfo'])
        run(['cmake', '--build', BUILD / 'cmake', '--parallel', '4'])
    library = BUILD / 'cmake/libhades_native.so'
    if args.compile_only:
        print(f'Native ARM64 engine compiled: {library}')
        return
    assets = BUILD / 'assets'
    assets.mkdir(exist_ok=True)
    if not args.ui.is_dir():
        raise RuntimeError('Original native UI assets are required; run native/tools/export_native_ui.py')
    shutil.copytree(args.ui, assets / 'ui', dirs_exist_ok=True)
    weapon_manifest=json.loads((args.weapons.parent/'manifest.json').read_text())
    weapon_bytes=args.weapons.read_bytes()
    if weapon_bytes[:4]==b'HWA1':
        if weapon_manifest['schema']!='hades-native-weapon-initialization-v1' or len(weapon_bytes)<104 or \
           hashlib.sha256(weapon_bytes).hexdigest()!=weapon_manifest['packet']['sha256'] or \
           weapon_bytes[4:36].hex()!=weapon_manifest['source']['sha256'] or \
           weapon_bytes[36:68].hex()!=weapon_manifest['normalization']['sha256'] or \
           weapon_bytes[68:100].hex()!=weapon_manifest['target_sha256']:
            raise RuntimeError('Weapon initialization packet/manifest identity mismatch')
        for entry in ('source','normalization'):
            item=weapon_manifest[entry]
            if hashlib.file_digest(Path(item['path']).open('rb'),'sha256').hexdigest()!=item['sha256']:
                raise RuntimeError('Weapon initialization source/provenance changed: '+entry)
    elif weapon_bytes[:4] in (b'HWA2',b'HWA3'):
        if weapon_manifest['schema'] not in ('hades-player-weapons-runtime-v2','hades-player-weapons-runtime-v3') or \
           hashlib.sha256(weapon_bytes).hexdigest()!=weapon_manifest['packet_sha256'] or \
           hashlib.file_digest((args.weapons.parent/'catalog.json').open('rb'),'sha256').hexdigest()!=weapon_manifest['catalog_sha256'] or \
           weapon_bytes[68:100].hex()!='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254':
            raise RuntimeError('Expanded weapon packet/catalog identity mismatch')
    else:
        raise RuntimeError('Unrecognized source weapon packet')
    shutil.copyfile(args.weapons,assets/'weapons.bin')
    if 'projectile_thing_packet' in weapon_manifest:
        projectile_meta=weapon_manifest['projectile_thing_packet'];projectile_packet=args.weapons.parent/projectile_meta['path']
        if hashlib.file_digest(projectile_packet.open('rb'),'sha256').hexdigest()!=projectile_meta['sha256']:
            raise RuntimeError('Original projectile Thing catalog changed')
        shutil.copyfile(projectile_packet,assets/'projectiles.lua')
    else:
        (assets/'projectiles.lua').unlink(missing_ok=True)
    weapon_report={'path':str(args.weapons.resolve()),'sha256':hashlib.sha256(weapon_bytes).hexdigest(),
                   'scope':'Native weapon initialization; firing/combat integration in progress'}
    actor_manifest=json.loads((args.actors.parent/'manifest.json').read_text())
    if actor_manifest['schema']!='hades-source-actor-catalog-v1' or \
       hashlib.file_digest(args.actors.open('rb'),'sha256').hexdigest()!=actor_manifest['packet']['sha256']:
        raise RuntimeError('Actor packet/manifest identity mismatch')
    for item in [actor_manifest['creation'],actor_manifest['defaults'],*actor_manifest['source_files']]:
        if hashlib.file_digest(Path(item['path']).open('rb'),'sha256').hexdigest()!=item['sha256']:
            raise RuntimeError('Actor source/provenance changed: '+item['path'])
    shutil.copyfile(args.actors,assets/'actors.lua')
    package_manifest=json.loads((args.packages.parent/'manifest.json').read_text())
    if package_manifest['schema']!='hades-native-asset-packages-v1' or hashlib.file_digest(args.packages.open('rb'),'sha256').hexdigest()!=package_manifest['packet']['sha256']:
        raise RuntimeError('Package catalog identity changed')
    for item in package_manifest['sources']:
        if hashlib.file_digest(Path(item['path']).open('rb'),'sha256').hexdigest()!=item['sha256']:
            raise RuntimeError('Original package source changed')
    for texture in (args.packages.parent/'package-textures').glob('*.rgba'):
        if hashlib.file_digest(texture.open('rb'),'sha256').hexdigest()!=texture.stem:
            raise RuntimeError('Decoded package texture changed: '+texture.name)
    shutil.copyfile(args.packages,assets/'packages.lua')
    shutil.copytree(args.packages.parent/'package-textures',assets/'package-textures',dirs_exist_ok=True)
    action_manifest=json.loads((args.actor_animations.parent/'manifest.json').read_text())
    if action_manifest['schema']!='hades-original-actor-animations-v1' or hashlib.file_digest(args.actor_animations.open('rb'),'sha256').hexdigest()!=action_manifest['packet']['sha256']:
        raise RuntimeError('Original actor action packet identity changed')
    for item in action_manifest['sources']:
        if hashlib.file_digest(Path(item['path']).open('rb'),'sha256').hexdigest()!=item['sha256']:
            raise RuntimeError('Original actor animation source changed: '+item['path'])
    shutil.copyfile(args.actor_animations,assets/'actor-animations.bin')
    # Source definitions and source-frame event records stay bound to the same
    # original bank manifest used above. No game script is rewritten.
    sys.path.insert(0,str(PROJECT/'native/tools'))
    from export_actor_catalog import literal as source_lua_literal
    from stage_actor_lights import stage_actor_lights
    stage_actor_lights(args.actor_lights,assets)
    from stage_text_resources import stage_text_resources
    stage_text_resources(args.text_resources,assets)
    from stage_action_effects import stage_action_effects
    stage_action_effects(args.action_effects,assets)
    from stage_screen_animations import stage_screen_animations
    stage_screen_animations(args.screen_animations,assets)
    animation_events={'schema':'hades-actor-animation-events-v1','bank_sha256':action_manifest['packet']['sha256'],
        'clips':{clip['name']:{'definition':clip['definition'],
            'frames':{str(frame['original_frame']):frame['events'] for frame in clip['source_sequence'] if frame['events']}}
            for clip in action_manifest['clips']}}
    (assets/'actor-animation-events.lua').write_text('return '+source_lua_literal(animation_events)+'\n')

    facing_path=args.actor_animations.parent/'actor-facing.bin'
    facing=facing_path.read_bytes();action_hash=bytes.fromhex(action_manifest['packet']['sha256'])
    if len(facing)<44 or facing[:4]!=b'HAF1' or facing[4:36]!=action_hash:
        raise RuntimeError('Actor facing companion differs from exact source bank')
    shutil.copyfile(facing_path,assets/'actor-facing.bin')
    (assets/'actor-animations-sha256.bin').write_bytes(action_hash)
    marker_manifest=json.loads((args.actor_markers.parent/'manifest.json').read_text())
    marker_bytes=args.actor_markers.read_bytes()
    if marker_manifest['schema']!='hades-original-actor-markers-v1' or hashlib.sha256(marker_bytes).hexdigest()!=marker_manifest['packet']['sha256'] or marker_bytes[:4]!=b'HMR1' or marker_bytes[4:36]!=action_hash:
        raise RuntimeError('Original marker bank identity/binding changed')
    for item in [marker_manifest['bound_actor_bank'],marker_manifest['actor_manifest'],*[d['source']for d in marker_manifest['datasets']]]:
        if hashlib.file_digest(Path(item['path']).open('rb'),'sha256').hexdigest()!=item['sha256']:
            raise RuntimeError('Original actor marker source changed')
    shutil.copyfile(args.actor_markers,assets/'actor-markers.bin')
    properties_manifest=json.loads((args.actor_properties.parent/'manifest.json').read_text())
    properties=args.actor_properties.read_bytes()
    if properties_manifest['schema'] not in ('hades-primary-animation-properties-v1','hades-primary-animation-properties-v2','hades-primary-animation-properties-v3') or hashlib.sha256(properties).hexdigest()!=properties_manifest['packet']['sha256'] or properties[:4] not in (b'HAP1',b'HAP2',b'HAP3') or properties[4:36]!=action_hash:
        raise RuntimeError('Primary animation properties do not match original actor bank')
    if properties[:4] in (b'HAP2',b'HAP3'):
        for record in properties_manifest['records']:
            value=record['properties']['AirborneZ']
            if value['provenance']=='native-compiled-default' and value['value']!=100.0:
                raise RuntimeError('Primary AirborneZ must retain explicit native constructor default100')
    for item in [properties_manifest['bank'],properties_manifest['source_manifest']]:
        path=Path(item['path']);path=path if path.is_absolute() else PROJECT/path
        if hashlib.file_digest(path.open('rb'),'sha256').hexdigest()!=item['sha256']:
            raise RuntimeError('Original primary animation property source changed')
    shutil.copyfile(args.actor_properties,assets/'actor-properties.bin')

    effects_manifest=json.loads((args.effects.parent/'manifest.json').read_text())
    if effects_manifest['schema'] not in ('hades-laurel-initialization-export-v1','hades-original-room-spawner-assets-v1') or hashlib.file_digest(args.effects.open('rb'),'sha256').hexdigest()!=effects_manifest['lua']['sha256']:
        raise RuntimeError('Native effect catalog identity changed')
    effect_sources=list(effects_manifest.get('sources',[]))
    if effects_manifest['schema']=='hades-original-room-spawner-assets-v1':
        effect_sources += [effects_manifest[k] for k in ('registry','source_catalog','catalog','packet')]
        effect_sources += effects_manifest['source_manifests']
        registry=json.loads(Path(effects_manifest['registry']['path']).read_text())
        effect_sources += [registry[k] for k in ('creation','scene','hda','haa')]
        effect_sources += registry['sources']
        base_manifest=json.loads((Path(effects_manifest['source_catalog']['path']).parent/'manifest.json').read_text())
        effect_sources += base_manifest['sources']
    else:
        (assets/'spawner-frames.bin').unlink(missing_ok=True)
    for item in effect_sources:
        if hashlib.file_digest(Path(item['path']).open('rb'),'sha256').hexdigest()!=item['sha256']:
            raise RuntimeError('Original animation effect source changed')
    if effects_manifest['schema']=='hades-original-room-spawner-assets-v1':
        shutil.copyfile(Path(effects_manifest['packet']['path']),assets/'spawner-frames.bin')
    shutil.copyfile(args.effects,assets/'effects.lua')
    particle_manifest=json.loads((args.particle_frames.parent/'manifest.json').read_text())
    if particle_manifest['schema']!='hades-original-cinder-frame-bank-v1' or hashlib.file_digest(args.particle_frames.open('rb'),'sha256').hexdigest()!=particle_manifest['packet']['sha256']:
        raise RuntimeError('Original particle frame bank identity changed')
    for item in particle_manifest['sources']:
        if hashlib.file_digest(Path(item['path']).open('rb'),'sha256').hexdigest()!=item['sha256']:
            raise RuntimeError('Original particle frame bank source changed')
    shutil.copyfile(args.particle_frames,assets/'cinder-frames.bin')
    # Audio is packaged as original compressed single-sample FSB containers.
    # The receipt binds both executable Lua and its full provenance manifest.
    def verified_source_file(item):
        path=Path(item['path'])
        if not path.is_absolute():
            path=PROJECT/path
        if hashlib.file_digest(path.open('rb'),'sha256').hexdigest()!=item['sha256']:
            raise RuntimeError('Original audio identity changed: '+str(path))
        return path
    bloom=json.loads(args.bloom_settings.read_text())
    if bloom['schema']!='hades-original-bloom-presets-v1':raise RuntimeError('Unsupported original bloom settings')
    verified_source_file(bloom['source'])
    shutil.copyfile(verified_source_file(bloom['packet']),assets/'bloom-settings.bin')
    grading=json.loads(args.color_grading.read_text())
    if grading['schema']!='hades-original-color-grading-v1':raise RuntimeError('Unsupported original color grading')
    for key in ['source','package']:verified_source_file(grading[key])
    shutil.copyfile(verified_source_file(grading['packet']),assets/'color-grading.bin')
    audio_receipt=json.loads(args.audio_events.read_text())
    if audio_receipt['schema']!='hades-native-audio-event-receipt-v1':
        raise RuntimeError('Unsupported audio receipt')
    audio_catalog=verified_source_file(audio_receipt['catalog'])
    audio_manifest=json.loads(verified_source_file(audio_receipt['manifest']).read_text())
    if audio_manifest['schema']!='hades-native-audio-events-v1':
        raise RuntimeError('Unsupported audio event catalog')
    for item in [*audio_manifest['source_banks'].values(),audio_manifest['guids'],audio_manifest['namespace_source']]:
        verified_source_file(item)
    for key in ['voice_header','actor_definitions']:
        if key in audio_manifest:verified_source_file(audio_manifest[key])
    for subtitle in audio_manifest.get('voice_subtitles',{}).values():
        # source=None: dialogue voice with no original subtitle row (text '').
        if subtitle['source'] is not None:verified_source_file(subtitle['source'])
    stream_roots=[]
    for item in [audio_manifest['stream_manifest'],*audio_manifest.get('additional_stream_manifests',[])]:
        stream_roots.append(verified_source_file(item).parent)
    for sample in audio_manifest['samples'].values():
        relative=Path(sample['asset'])
        if relative.is_absolute() or '..' in relative.parts:
            raise RuntimeError('Unsafe original audio asset path')
        matches=[root/relative for root in stream_roots if (root/relative).is_file()]
        if not matches:
            raise RuntimeError('Original compressed audio asset is absent: '+str(relative))
        # Assets are content-addressed: catalogs that re-export a sample hold
        # identical bytes; every copy must match the recorded hash.
        for match in matches:verified_source_file({'path':str(match),'sha256':sample['sha256']})
        destination=assets/relative
        destination.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(matches[0],destination)
    shutil.copyfile(audio_catalog,assets/'audio-events.lua')

    leaf_manifest=json.loads((args.room_leaf_frames.parent/'manifest.json').read_text())
    if leaf_manifest['schema']!='hades-original-room-leaf-frames-v1':
        raise RuntimeError('Unsupported room leaf bank')
    for item in [leaf_manifest[k] for k in ('packet','facing','identity','registry')]+leaf_manifest['sources']:
        verified_source_file(item)
    leaf_hash=hashlib.file_digest(args.room_leaf_frames.open('rb'),'sha256').digest()
    leaf_facing=Path(leaf_manifest['facing']['path']).read_bytes()
    if leaf_facing[:4]!=b'HAF1' or leaf_facing[4:36]!=leaf_hash or Path(leaf_manifest['identity']['path']).read_bytes()!=leaf_hash:
        raise RuntimeError('Room leaf companion binding changed')
    for key in ('packet','facing','identity'):
        original=Path(leaf_manifest[key]['path'])
        shutil.copyfile(original,assets/original.name)

    primary_manifest=json.loads(args.primaries.read_text())
    if primary_manifest['schema'] not in ('hades-source-scene-primaries-v1','hades-source-scene-primaries-v2'):
        raise RuntimeError('Unsupported scene primaries')
    for key in ('catalog','lua','aux_bank','registry','source'):
        verified_source_file(primary_manifest[key])
    for item in primary_manifest['definition_provenance'].values():
        verified_source_file({'path':item['path'],'sha256':item['file_sha256']})
    for key in ('lua','aux_bank'):
        original=Path(primary_manifest[key]['path'])
        shutil.copyfile(original,assets/original.name)

    movie_manifest=json.loads((args.movie_preloads.parent/'manifest.json').read_text())
    if movie_manifest['schema']!='hades-native-movie-preload-v1' or hashlib.file_digest(args.movie_preloads.open('rb'),'sha256').hexdigest()!=movie_manifest['packet']['sha256']:
        raise RuntimeError('Original movie preload catalog identity changed')
    for item in movie_manifest['sources']:
        if hashlib.file_digest(Path(item['path']).open('rb'),'sha256').hexdigest()!=item['sha256']:
            raise RuntimeError('Original movie preload source changed: '+item['path'])
    for movie in movie_manifest['movies'].values():
        for item in (movie['original'],movie['metadata']):
            if hashlib.file_digest(Path(item['path']).open('rb'),'sha256').hexdigest()!=item['sha256']:
                raise RuntimeError('Original movie bytes or atlas metadata changed')
        payload=args.movie_preloads.parent/movie['path']
        if hashlib.file_digest(payload.open('rb'),'sha256').hexdigest()!=movie['sha256']:
            raise RuntimeError('Staged original compressed movie changed')
    shutil.copyfile(args.movie_preloads,assets/'movie-preloads.lua')
    shutil.copytree(args.movie_preloads.parent/'movie-preloads',assets/'movie-preloads',dirs_exist_ok=True)

    shutil.copyfile(args.scene, assets / 'scene.bin')
    with args.scene.open('rb') as source:
        scene_magic = source.read(4)
    if scene_magic == b'HDS3':
        manifest = json.loads((args.scene.parent / 'scene-manifest.json').read_text())
        scene_hash = hashlib.file_digest(args.scene.open('rb'), 'sha256').digest()
        (assets / 'scene-sha256.bin').write_bytes(scene_hash)
        with (assets / 'materials.bin').open('wb') as out:
            out.write(struct.pack('<4sI', b'HSM1', len(manifest['sprites'])))
            for sprite in manifest['sprites']:
                out.write(struct.pack('<3f', *sprite['material']['hsv']))
    else:
        (assets / 'materials.bin').unlink(missing_ok=True)
        (assets / 'scene-sha256.bin').unlink(missing_ok=True)
    ambient_report=None
    if args.ambient.is_file():
        ambient=args.ambient.read_bytes()
        if scene_magic!=b'HDS3' or len(ambient)<52 or ambient[:4]!=b'HLA1' or ambient[4:36]!=scene_hash:
            raise RuntimeError('Ambient companion is not bound to this exact HDS3 scene')
        ambient_count=struct.unpack_from('<I',ambient,36)[0]
        if ambient_count!=len(manifest['sprites']) or len(ambient)!=52+ambient_count*8:
            raise RuntimeError('Ambient companion sprite count/size differs from the scene')
        shutil.copyfile(args.ambient,assets / 'ambient.bin')
        ambient_report={'path':str(args.ambient.resolve()),'sprites':ambient_count,
                        'sha256':hashlib.sha256(ambient).hexdigest()}
    else:
        (assets / 'ambient.bin').unlink(missing_ok=True)
    composition_report=None
    if args.composition.is_file():
        composition=args.composition.read_bytes()
        if scene_magic!=b'HDS3' or len(composition)<40 or composition[:4]!=b'HGC1' or composition[4:36]!=scene_hash:
            raise RuntimeError('Composition companion is not bound to the exact HDS3 scene')
        count=struct.unpack_from('<I',composition,36)[0]
        if count!=len(manifest['sprites']) or len(composition)!=40+count*4:
            raise RuntimeError('Composition companion count/size mismatch')
        shutil.copyfile(args.composition,assets/'composition.bin')
        composition_report={'path':str(args.composition.resolve()),'sha256':hashlib.sha256(composition).hexdigest()}
    else:
        (assets/'composition.bin').unlink(missing_ok=True)
    if args.animations:
        with args.animations.open('rb') as source:
            header = source.read(44)
        if scene_magic != b'HDS3' or len(header) != 44 or header[:4] != b'HDA1' or header[12:44] != scene_hash:
            raise RuntimeError('Animation packet is not bound to this exact HDS3 scene')
        shutil.copyfile(args.animations, assets / 'animations.bin')
        animation_report = {'path': str(args.animations.resolve()), 'bytes': args.animations.stat().st_size,
                            'clips': struct.unpack_from('<I', header, 4)[0],
                            'bindings': struct.unpack_from('<I', header, 8)[0],
                            'timing_policy': 'Deterministic phase 0 and original minimum FPS; original random phase/order remains unresolved.'}
    else:
        (assets / 'animations.bin').unlink(missing_ok=True)
        animation_report = None
    if args.parallax.is_file():
        parallax=args.parallax.read_bytes()
        count=len(manifest['sprites']) if scene_magic==b'HDS3' else -1
        if len(parallax)!=40+12*count or parallax[:4]!=b'HPP1' or parallax[4:36]!=scene_hash or struct.unpack_from('<I',parallax,36)[0]!=count:
            raise RuntimeError('Parallax companion differs from the exact scene')
        shutil.copyfile(args.parallax,assets/'parallax.bin')
    else:
        (assets/'parallax.bin').unlink(missing_ok=True)
    if args.hulls.is_file():
        animation_hash=hashlib.file_digest(args.animations.open('rb'),'sha256').digest() if args.animations else bytes(32)
        hull=args.hulls.read_bytes()
        if len(hull)<76 or hull[:4]!=b'HHL1' or hull[4:36]!=scene_hash or hull[36:68]!=animation_hash:
            raise RuntimeError('Hull companion differs from scene/animation identity')
        shutil.copyfile(args.hulls,assets/'hulls.bin')
        (assets/'animations-sha256.bin').write_bytes(animation_hash)
    else:
        (assets/'hulls.bin').unlink(missing_ok=True)
        (assets/'animations-sha256.bin').unlink(missing_ok=True)
    geometry_path = args.geometry or args.scene.parent / 'room-geometry.json'
    camera = None
    if geometry_path.is_file():
        geometry = json.loads(geometry_path.read_text())
        room_metadata = json.loads((PROJECT / '.local/game/base/Content/Maps' / f'{geometry["room"]}.map_text').read_text())
        backdrop=room_metadata['BackdropColor']
        if isinstance(backdrop,dict):
            backdrop=[backdrop[k] for k in ('R','G','B','A')]
        if len(backdrop)!=4 or any(not isinstance(c,(int,float)) or not 0<=c<=255 for c in backdrop):
            raise RuntimeError('Invalid original room BackdropColor')
        (assets / 'render-options.bin').write_bytes(struct.pack('<4s5f',b'HRO1',*(c/255 for c in backdrop),1))
        with (assets / 'groups.bin').open('wb') as out:
            def text_field(value):
                # Original root ParentGroup.Name is JSON null (no parent).
                if value is None:
                    value = ''
                encoded = value.encode('utf-8')
                if len(encoded) > 65536:
                    raise RuntimeError('Map group name exceeds bounds')
                out.write(struct.pack('<I', len(encoded)))
                out.write(encoded)
            groups = room_metadata['ThingGroups']
            out.write(struct.pack('<4sI', b'SGP1', len(groups)))
            for group in groups:
                key, parent = group['Id'], group['ParentGroup']
                out.write(struct.pack('<iiiI', key['Id'], parent['Id'], group['BlendMode'], int(group['Visible'])))
                text_field(key['Name'])
                text_field(parent['Name'])
                out.write(struct.pack('<I', len(group['ChildGroups'])))
                for child in group['ChildGroups']:
                    out.write(struct.pack('<i', child['Id']))
                    text_field(child['Name'])
        anchors = [a for a in geometry['anchors'] if a['name'] == 'CameraIntroEnd']
        if len(anchors) == 1:
            camera = {'anchor': anchors[0], 'reference_height': 2160,
                      'android_default_height': 1680, 'android_height_range': [900,3200],
                      'zoom_basis': 'Android adaptation: smooth hero follow, Settings zoom and independent pinch. Original cinematic camera remains incomplete.'}
            (assets / 'camera.bin').write_bytes(struct.pack('<4s3f', b'HDC1',
                anchors[0]['location']['X'], anchors[0]['location']['Y'], camera['reference_height']))
    elif (assets / 'camera.bin').exists():
        (assets / 'camera.bin').unlink()
    scripts = assets / 'Scripts'
    scripts.mkdir(exist_ok=True)
    for script in (PROJECT / '.local/game/base/Content/Scripts').glob('*.lua'):
        shutil.copyfile(script, scripts / script.name)
    player = None
    if args.hero.is_dir():
        if not geometry_path.is_file():
            raise RuntimeError('Original room geometry is required for the player')
        sys.path.insert(0, str(PROJECT / 'native/tools'))
        from build_room_scene import Sjson
        unit_path = PROJECT / '.local/game/base/Content/Game/Units/PlayerUnits.sjson'
        units = Sjson(unit_path.read_text(encoding='utf-8-sig')).parse()['Units']
        unit = next(u for u in units if u['Name'] == '_PlayerUnit')
        # HeroStart is an airborne entrance marker overlapping the rear barrier.
        # The current slice skips the drop cinematic and starts at its HeroEnd.
        spawn = next(a for a in geometry['anchors'] if a['name'] == 'HeroEnd')
        hull = [(p['X'] * unit['Thing']['Scale'], p['Y'] * unit['Thing']['Scale']) for p in unit['Thing']['Points']]
        # Initially accept only transforms whose source interpretation is exact.
        collision=json.loads(args.collision.read_text())
        scene_manifest=args.scene.parent/'scene-manifest.json'
        if collision['schema']!='hades-native-collision-v1' or collision['room']!=geometry['room'] or \
           hashlib.file_digest(scene_manifest.open('rb'),'sha256').hexdigest()!=collision['input_scene_manifest']['sha256'] or \
           hashlib.file_digest(args.creation.open('rb'),'sha256').hexdigest()!=collision['input_creation_manifest']['sha256']:
            raise RuntimeError('Collision export does not match exact scene/creation metadata')
        exact=[p for p in collision['collision_candidates'] if p['world_transform_exact']]
        polygons=[p['world_points_exact'] for p in exact]
        polygon_ids=[p['id'] for p in exact]
        if len(set(polygon_ids))!=len(polygon_ids):raise RuntimeError('Duplicate collision source IDs')
        with (assets / 'player-world.bin').open('wb') as out:
            out.write(struct.pack('<4s5fI', b'HDP2', spawn['location']['X'], spawn['location']['Y'],
                                  unit['Speed'], unit['Acceleration'], unit['Thing']['Grip'], len(hull)))
            for point in hull:
                out.write(struct.pack('<2f', *point))
            out.write(struct.pack('<I', len(polygons)))
            for source_id,polygon in zip(polygon_ids,polygons):
                out.write(struct.pack('<iI', source_id,len(polygon)))
                for point in polygon:
                    out.write(struct.pack('<2f', *point))
        for clip in ('ZagreusIdle.bin', 'ZagreusRun.bin'):
            shutil.copyfile(args.hero / clip, assets / clip)
        manifest = json.loads((args.scene.parent / 'scene-manifest.json').read_text())
        objects = {r['object']['Id']: r['object'] for r in manifest['all_original_objects']}
        creation = json.loads(args.creation.read_text())
        if creation['schema'] != 'hades-room-creation-v1' or creation['room'] != manifest['room'] or creation['attachment_errors']:
            raise RuntimeError('Creation metadata does not describe this room or has attachment errors')
        source_map = PROJECT / '.local/game/base/Content/Win/Maps' / f'{manifest["room"]}.thing_bin'
        if hashlib.file_digest(source_map.open('rb'), 'sha256').hexdigest() != creation['map_source']['sha256']:
            raise RuntimeError('Creation metadata is not bound to the preserved original map')
        creation_records = {record['id']: record for record in creation['records']}
        if len(creation_records) != len(creation['records']) or set(creation_records) != set(objects):
            raise RuntimeError('Creation and graphics map identities differ')
        # A derived data table, not replacement game logic. Lua literals avoid
        # introducing a second JSON parser into the portable native core.
        with (assets / 'DecodedMapObjects.lua').open('w') as out:
            out.write('return {\n')
            for record in manifest['all_original_objects']:
                original = record['object']
                creation_record = creation_records[original['Id']]
                if creation_record['name'] != original['Name'] or creation_record['source_range'] != original['source_range']:
                    raise RuntimeError('Creation record source ranges/identity differ from the scene map')
                fields = {
                    'id': original['Id'], 'name': original['Name'],
                    'group': next((g for g in original['GroupNames'] if g), ''),
                    'x': original['Location']['X'], 'y': original['Location']['Y'], 'z': original['OffsetZ'],
                    'scale': original['Scale'], 'alpha': original['Color'][3] / 255, 'angle': original['Angle'],
                    'active': original['Active'], 'doCreate': original['DoCreate'],
                    'visible': not record['obstacle_thing'].get('Invisible', False),
                    'flipHorizontal': original['FlipHorizontal'],
                    'dataType': creation_record['data_type'], 'parentId': original['AttachToID'],
                    'activateAtRange': original['ActivateAtRange'], 'activationRange': original['ActivationRange'],
                    'triggerOnSpawn': creation_record['trigger_on_spawn']['value'],
                }
                if creation_record['player_controlled'] is not None:
                    fields['playerControlled'] = creation_record['player_controlled']
                def lua_literal(value):
                    return json.dumps(value, ensure_ascii=False, allow_nan=False)
                memberships = ','.join(lua_literal(group) for group in original['GroupNames'])
                children = ','.join(str(child) for child in original['AttachedIDs'])
                out.write('{' + ','.join(f'{key}={lua_literal(value)}' for key, value in fields.items())
                          + ',groups={' + memberships + '},childIds={' + children + '}},\n')
            out.write('}\n')
        with (assets / 'ordering.bin').open('wb') as out:
            out.write(struct.pack('<4sI', b'HDO1', len(manifest['sprites'])))
            for sprite in manifest['sprites']:
                original = objects[sprite['object_id']]
                out.write(struct.pack('<3f', sprite['source_layer'], original['SortIndex'], original['Location']['Y']))
        player = {'spawn': spawn, 'speed': unit['Speed'], 'acceleration': unit['Acceleration'],
                  'grip': unit['Thing']['Grip'], 'collision_polygons': len(polygons),
                  'skipped_collision_transforms': collision['counts']['unresolved_polygons'],
                  'collision_export':str(args.collision.resolve()),
                  'collision_export_sha256':hashlib.file_digest(args.collision.open('rb'),'sha256').hexdigest(),
                  'collision_scope':'Deterministic active obstacle polygons only; source random-scale placements remain unresolved.',
                  'hero_assets': str(args.hero), 'entry_presentation_skipped': True,
                  'movement_semantics': 'Grounded velocity and low-stick branch recovered; collision response and animation pivots remain provisional.'}
    else:
        for filename in ('ZagreusIdle.bin', 'ZagreusRun.bin', 'player-world.bin', 'ordering.bin'):
            (assets / filename).unlink(missing_ok=True)
    if args.assets_only:
        print(json.dumps({'assets': str(assets), 'player': player, 'camera': camera, 'animations': animation_report,'weapons':weapon_report}, indent=2))
        return
    unsigned = BUILD / 'unsigned.apk'
    # NativeActivity window lifecycle runs on Android's UI thread. This tiny
    # subclass owns system-bar visibility only; all game/UI rendering is C++.
    classes = BUILD / 'activity-classes'
    dex = BUILD / 'activity-dex'
    classes.mkdir(exist_ok=True)
    dex.mkdir(exist_ok=True)
    android_jar = SDK / 'platforms/android-36/android.jar'
    run([JAVA / 'bin/javac', '--release', '8', '-classpath', android_jar,
         '-d', classes, PROJECT / 'native/app/HadesActivity.java'])
    run([TOOLS / 'd8', '--lib', android_jar, '--min-api', '26', '--output', dex,
         classes / 'dev/hades/reconstruction/HadesActivity.class'])
    # Android resources: original launcher icon + GameManager game-mode config.
    res = BUILD / 'res'
    if res.exists():
        shutil.rmtree(res)
    shutil.copytree(PROJECT / 'native/app/res', res)
    icon_root = args.app_icon.parent
    for density in json.loads(args.app_icon.read_text())['mipmaps'].values():
        target = res / Path(density['path'])
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(icon_root / density['path'], target)
    compiled = BUILD / 'res-compiled.zip'
    run([TOOLS / 'aapt2', 'compile', '--dir', res, '-o', compiled])
    run([TOOLS / 'aapt2', 'link', '-I', SDK / 'platforms/android-36/android.jar',
         '--manifest', PROJECT / 'native/app/AndroidManifest.xml', '-A', assets, '-R', compiled, '--auto-add-overlay',
         '-0', 'bin', '-o', unsigned])
    stripped = BUILD / 'libhades_native.so'
    shutil.copyfile(library, stripped)
    run([NDK / 'toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-strip', '--strip-debug', stripped])
    with zipfile.ZipFile(unsigned, 'a') as archive:
        archive.write(dex / 'classes.dex', 'classes.dex', compress_type=zipfile.ZIP_DEFLATED)
        archive.write(stripped, 'lib/arm64-v8a/libhades_native.so', compress_type=zipfile.ZIP_STORED)
    aligned = BUILD / 'aligned.apk'
    run([TOOLS / 'zipalign', '-f', '-P', '16', '4', unsigned, aligned])
    key = BUILD / 'debug.keystore'
    if not key.exists():
        run([JAVA / 'bin/keytool', '-genkeypair', '-keystore', key, '-storepass', 'android',
             '-alias', 'androiddebugkey', '-keypass', 'android', '-keyalg', 'RSA', '-keysize', '2048',
             '-validity', '10000', '-dname', 'CN=Hades Native Development'])
    output = PROJECT / '.local/build/native/hades-native-engine-preview.apk'
    output.parent.mkdir(parents=True, exist_ok=True)
    run([TOOLS / 'apksigner', 'sign', '--ks', key, '--ks-pass', 'pass:android',
         '--key-pass', 'pass:android', '--out', output, aligned])
    run([TOOLS / 'apksigner', 'verify', '--verbose', output])
    permissions = subprocess.check_output([str(TOOLS / 'aapt2'), 'dump', 'permissions', str(output)], text=True)
    if 'android.permission.INTERNET' in permissions:
        raise RuntimeError('Native Hades must have no INTERNET permission')
    report = {
        'apk': str(output), 'bytes': output.stat().st_size,
        'sha256': hashlib.file_digest(output.open('rb'), 'sha256').hexdigest(),
        'package': 'dev.hades.reconstruction', 'abi': 'arm64-v8a',
        'native_engine': str(library), 'game_engine_runtime': 'Native Android C++/GLES3',
        'android_window_bridge': 'NativeActivity subclass for UI-thread system-bar lifecycle only',
        'network_permission': False, 'compatibility_runtime': False,
        'scene': str(args.scene.resolve()),
        'scene_sha256': hashlib.file_digest(args.scene.open('rb'), 'sha256').hexdigest(),
        'camera': camera,
        'player': player,
        'animations': animation_report,
        'ambient': ambient_report,
        'composition': composition_report,
        'weapons': weapon_report,
        'native_ui': {'assets': str(args.ui.resolve()), 'screens': ['title','loading','load-failed','pause','settings'],
                      'controls': ['floating or fixed movement stick','independent pinch zoom','pause','brightness','camera zoom','control size','control opacity','handedness'],
                      'loading': 'Title first; scene, player and original Lua precreation load in three reported stages after Play. Full room activation remains incomplete.',
                      'settings_persistence': 'HADES_UI_2 app-private local file; accepts HADES_UI_1; no network access'},
        'current_behavior': 'Native title and staged movement-slice loading, smooth camera/pinch, adjustable touch controls, original room/effect assets and source group composition; engine reconstruction in progress.',
        'complete_game': False, 'device_execution_verified': False,
        'remaining': ['Map activation/OnSpawn/Map::Begin', 'Complete Lua engine bridge', 'Entry presentation',
                      'Exact materials and lighting', 'Combat', 'Audio', 'Transitions', 'Saves'],
    }
    report_path = PROJECT / '.local/reports/native-build.json'
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
