#!/usr/bin/env python3.13
"""Export one original map's complete room-bound resource closure.

Runs the existing source exporters (the same ones that produced RoomSimple01)
for any original map, in dependency order, into a fresh work directory, then
stages a validated room pack (stage_room_packs.build_room_pack). Map selection
is the caller's: every `<Map>.thing_bin`/`.map_text` pair under Content is
exportable; nothing here names a room, enemy or animation. Packs record the
exact inputs they were built from; later staging re-verifies every file hash.

Bulky intermediate crops (static/, scene/textures, scene/animations) are
pruned after the pack validates; HDS/HDA packets are APFS-cloned into the pack
so a room costs its packed size once on disk.
"""
import argparse, hashlib, json, os, shutil, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / 'native/tools'
CONTENT = ROOT / '.local/game/base/Content'
ASSETS = ROOT / '.local/native-assets'
# Shared inputs the RoomSimple01 pack was built from (recorded in its manifests).
DEFAULT_INPUTS = {
    'hero_actions': ASSETS / 'hero-actions-v3/manifest.json',
    'actor_defaults': ROOT / '.local/reports/native-actor-compiled-defaults-v5.json',
    'laurel': ASSETS / 'laurel-effects-v2/catalog.json',
    'facing': ASSETS / 'hero-facing-v1/actor-facing.bin',
    'animation_coverage': ROOT / '.local/reports/anim-coverage/animation-coverage-v18.json',
}
PY = shutil.which('python3.13') or sys.executable


def valid_map(name):
    return bool(name) and len(name) <= 64 and name.isascii() and name.replace('_', '').isalnum()


def run(log, *args):
    command = [PY, *(str(a) for a in args)]
    start = time.monotonic()
    result = subprocess.run(command, cwd=TOOLS, capture_output=True, text=True)
    log.append({'command': [Path(command[1]).name, *command[2:]], 'seconds': round(time.monotonic() - start, 2),
                'returncode': result.returncode, 'stdout': result.stdout[-2000:], 'stderr': result.stderr[-4000:]})
    if result.returncode:
        raise RuntimeError(f'{Path(command[1]).name} failed: {result.stderr.strip()[-1500:]}')


def export_sources(room, work, inputs, log):
    c = CONTENT
    t = lambda name: TOOLS / name
    scene = work / 'scene'
    creation = work / 'creation.json/creation.json'
    run(log, t('build_room_scene.py'), '--content', c, '--output', work / 'static', '--room', room)
    run(log, t('build_graphics_scene.py'), '--content', c, '--base', work / 'static', '--output', scene)
    run(log, t('export_scene_animation.py'), '--scene', scene, '--output', work / 'scene-animation')
    run(log, t('export_room_geometry.py'), scene / 'scene-manifest.json', '--output', work / 'geometry.json')
    run(log, t('export_room_creation.py'), '--content', c, '--output', work / 'creation.json', '--room', room)
    run(log, t('export_native_collision.py'), scene / 'scene-manifest.json', '--creation', creation, '--output', work / 'collision')
    run(log, t('export_scene_ambient.py'), '--content', c, '--scene', scene, '--creation', creation, '--output', work / 'ambient')
    run(log, t('export_scene_composition.py'), '--content', c, '--scene', scene, '--output', work / 'composition')
    run(log, t('export_scene_parallax.py'), '--scene', scene, '--output', work / 'parallax')
    run(log, t('export_scene_hulls.py'), '--content', c, '--scene', scene, '--animations', work / 'scene-animation', '--output', work / 'hulls')
    run(log, t('export_room_animation_registry.py'), '--content', c, '--creation', creation,
        '--scene', scene / 'scene-manifest.json', '--animations', work / 'scene-animation/animation-manifest.json',
        '--hero', inputs['hero_actions'], '--output', work / 'registry')
    registry = work / 'registry/registry.json'
    run(log, t('export_actor_catalog.py'), '--content', c, '--creation', creation, '--defaults', inputs['actor_defaults'], '--output', work / 'actors')
    run(log, t('export_room_spawner_assets.py'), '--content', c, '--registry', registry, '--laurel', inputs['laurel'], '--output', work / 'spawners')
    run(log, t('export_room_leaf_frames.py'), '--content', c, '--registry', registry, '--facing', inputs['facing'], '--output', work / 'leaf')
    run(log, t('export_scene_primaries.py'), '--content', c, '--registry', registry, '--animation-coverage', inputs['animation_coverage'], '--output', work / 'primaries')
    run(log, t('export_actor_lights.py'), '--content', c, '--creation', creation, '--output', work / 'lights')


def prune(work):
    freed = 0
    for relative in ('static', 'scene/textures', 'scene/animations'):
        path = work / relative
        if path.is_dir():
            freed += sum(p.stat().st_size for p in path.rglob('*') if p.is_file())
            shutil.rmtree(path)
    return freed


def export_room(room, work_root, pack_root, inputs=None, keep_intermediates=False):
    if not valid_map(room):
        raise ValueError('Invalid original map name')
    if not (CONTENT / 'Win/Maps' / f'{room}.thing_bin').is_file() or not (CONTENT / 'Maps' / f'{room}.map_text').is_file():
        raise ValueError('Original map absent: ' + room)
    inputs = {**DEFAULT_INPUTS, **(inputs or {})}
    for key, path in inputs.items():
        if not Path(path).is_file():
            raise ValueError(f'Shared exporter input missing: {key}={path}')
    work, pack = Path(work_root).resolve() / room, Path(pack_root).resolve() / room
    if work.exists() or pack.exists():
        raise ValueError('Fresh work and pack directories required: ' + room)
    work.mkdir(parents=True)
    log, start = [], time.monotonic()
    sys.path.insert(0, str(TOOLS))
    from stage_room_packs import build_room_pack
    try:
        export_sources(room, work, inputs, log)
        result = build_room_pack(work, pack)
    except BaseException:
        # Our own fresh outputs only: keep the step log, drop partial data so a
        # corrected exporter can rerun into fresh directories.
        failed = Path(work_root).resolve() / '_failed'
        failed.mkdir(exist_ok=True)
        (failed / f'{room}.json').write_text(json.dumps({'room': room, 'status': 'failed', 'steps': log}, indent=1) + '\n')
        shutil.rmtree(work, ignore_errors=True)
        shutil.rmtree(pack, ignore_errors=True)
        raise
    freed = 0 if keep_intermediates else prune(work)
    summary = {'room': room, 'status': 'ok', 'seconds': round(time.monotonic() - start, 1),
               'pack': str(pack / 'manifest.json'), 'files': len(result['files']),
               'pack_bytes': sum(r['bytes'] for r in result['files'].values()), 'pruned_bytes': freed,
               'inputs': {k: str(Path(v).resolve()) for k, v in inputs.items()}, 'steps': log}
    (work / 'export-log.json').write_text(json.dumps(summary, indent=1) + '\n')
    return summary


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--map', action='append', required=True, help='Original map name; repeat. A glob like "A_*" selects every matching original map')
    p.add_argument('--work', type=Path, required=True, help='Work root (fresh <Map>/ per map)')
    p.add_argument('--packs', type=Path, required=True, help='Pack root (fresh <Map>/ per map)')
    p.add_argument('--keep-intermediates', action='store_true')
    p.add_argument('--continue-on-error', action='store_true')
    a = p.parse_args()
    rooms = []
    for pattern in a.map:
        matched = sorted(x.stem for x in (CONTENT / 'Win/Maps').glob(pattern + '.thing_bin'))
        if not matched:
            raise ValueError('No original map matches ' + pattern)
        rooms += [r for r in matched if r not in rooms]
    results = []
    for room in rooms:
        try:
            summary = export_room(room, a.work, a.packs, keep_intermediates=a.keep_intermediates)
            results.append({k: summary[k] for k in ('room', 'status', 'seconds', 'files', 'pack_bytes')})
        except Exception as error:
            results.append({'room': room, 'status': 'failed', 'error': str(error)[-2000:]})
            if not a.continue_on_error:
                print(json.dumps(results, indent=1))
                raise
        print(json.dumps(results[-1]), flush=True)
    print(json.dumps({'exported': [r['room'] for r in results if r['status'] == 'ok'],
                      'failed': {r['room']: r['error'] for r in results if r['status'] != 'ok'}}, indent=1))


if __name__ == '__main__':
    main()
