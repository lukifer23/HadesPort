#!/usr/bin/env python3
"""Stage actual exported room resources in a fresh, source-verified map bundle."""
import argparse,hashlib,json,shutil,sys
from pathlib import Path
from types import SimpleNamespace
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from build_native import stage_room_scene
from stage_actor_lights import stage_actor_lights

TARGET='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'
def identity(path):
 path=Path(path).resolve();return {'path':str(path),'bytes':path.stat().st_size,'sha256':hashlib.file_digest(path.open('rb'),'sha256').hexdigest()}
def verify(row):
 path=Path(row['path'])
 if not path.is_file() or hashlib.file_digest(path.open('rb'),'sha256').hexdigest()!=row['sha256']:raise ValueError('Room source identity changed: '+str(path))
 return path
def build_room_pack(source,output,lights=None,actors=None):
 source,output=Path(source).resolve(),Path(output).resolve()
 if output.exists():raise ValueError('Fresh room output required')
 if output.is_relative_to(ROOT/'.local/game'):raise ValueError('Room output cannot overwrite originals')
 geometry=json.loads((source/'geometry.json').read_text());room=geometry['room']
 if not room or len(room)>64 or not room.isascii() or not room.replace('_','').isalnum():raise ValueError('Invalid original room name')
 output.mkdir(parents=True)
 args=SimpleNamespace(scene=source/'scene/scene-affine.bin',animations=source/'scene-animation/animations.bin',geometry=source/'geometry.json',creation=source/'creation.json/creation.json',ambient=source/'ambient/ambient.bin',composition=source/'composition/composition.bin',collision=source/'collision/room-geometry.json',parallax=source/'parallax/parallax.bin',hulls=source/'hulls/hulls.bin',hero=ROOT/'.local/native-assets/hero-eight-directions')
 reports=stage_room_scene(args,output,include_shared=False)
 actor=json.loads(Path(actors or source/'actors/manifest.json').read_text())
 if actor['schema']!='hades-source-actor-catalog-v1' or actor['room']!=room or actor.get('native_unit_angles')is not True:raise ValueError('Room actor schema/name mismatch')
 for row in [actor['packet'],actor['creation'],actor['defaults'],*actor['source_files']]:verify(row)
 shutil.copyfile(verify(actor['packet']),output/'actors.lua')
 effects=json.loads((source/'spawners/manifest.json').read_text())
 if effects['schema']!='hades-original-room-spawner-assets-v1':raise ValueError('Room spawner schema')
 registry=json.loads(verify(effects['registry']).read_text())
 for row in [effects[k]for k in ['lua','source_catalog','catalog','packet']]+effects['source_manifests']+[registry[k]for k in ['creation','scene','hda','haa']]+registry['sources']:verify(row)
 for key,name in [('lua','effects.lua'),('packet','spawner-frames.bin')]:shutil.copyfile(verify(effects[key]),output/name)
 leaf=json.loads((source/'leaf/manifest.json').read_text())
 if leaf['schema']!='hades-original-room-leaf-frames-v1':raise ValueError('Room leaf schema')
 for row in [leaf[k]for k in ['packet','facing','identity','registry']]+leaf['sources']:verify(row)
 for key in ['packet','facing','identity']:
  path=verify(leaf[key]);shutil.copyfile(path,output/path.name)
 primary=json.loads((source/'primaries/manifest.json').read_text())
 if primary['schema']not in ['hades-source-scene-primaries-v1','hades-source-scene-primaries-v2']:raise ValueError('Room primary schema')
 for key in ['catalog','lua','aux_bank','registry','source']:verify(primary[key])
 for row in primary['definition_provenance'].values():verify({'path':row['path'],'sha256':row['file_sha256']})
 for key in ['lua','aux_bank']:
  path=verify(primary[key]);shutil.copyfile(path,output/path.name)
 stage_actor_lights(lights or source/'lights/manifest.json',output)
 result={'schema':'hades-room-pack-v1','target_sha256':TARGET,'room':room,'source':str(source),'files':{p.name:identity(p)for p in sorted(output.iterdir())if p.is_file()},'reports':reports}
 (output/'manifest.json').write_text(json.dumps(result,indent=2)+'\n');return result
def stage_room_pack(manifest,assets):
 m=json.loads(Path(manifest).read_text());room=m['room']
 if m['schema']!='hades-room-pack-v1' or m['target_sha256']!=TARGET or not room.isascii() or not room.replace('_','').isalnum():raise ValueError('Room pack identity/name mismatch')
 destination=Path(assets)/'rooms'/room;destination.mkdir(parents=True,exist_ok=True)
 for name,row in m['files'].items():
  if Path(name).name!=name:raise ValueError('Room asset path must be a filename')
  shutil.copyfile(verify(row),destination/name)
 return {'room':room,'files':len(m['files'])}
def revise_room_actor_catalog(manifest,actors,output):
 """Fresh bundle: retain immutable validated room bytes, replace actor catalog."""
 manifest,actors,output=Path(manifest),Path(actors),Path(output)
 if output.exists():raise ValueError('Fresh room revision output required')
 base=json.loads(manifest.read_text());actor=json.loads(actors.read_text())
 if base['schema']!='hades-room-pack-v1' or base['target_sha256']!=TARGET:raise ValueError('Room revision source identity')
 if actor['schema']!='hades-source-actor-catalog-v1' or actor['room']!=base['room'] or actor.get('native_unit_angles')is not True:raise ValueError('Room revision actor schema/name/angles')
 for row in [actor['packet'],actor['creation'],actor['defaults'],*actor['source_files']]:verify(row)
 output.mkdir(parents=True)
 for name,row in base['files'].items():
  if Path(name).name!=name:raise ValueError('Room revision filename')
  source=verify(row)
  if name=='actors.lua':shutil.copyfile(verify(actor['packet']),output/name)
  else:(output/name).hardlink_to(source)
 revised={**base,'revision_source':identity(manifest),'actor_catalog_source':identity(actors),
  'files':{p.name:identity(p)for p in sorted(output.iterdir())if p.is_file()}}
 (output/'manifest.json').write_text(json.dumps(revised,indent=2)+'\n');return revised
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--actor-lights',type=Path);p.add_argument('--actors',type=Path);a=p.parse_args();r=build_room_pack(a.source,a.output,a.actor_lights,a.actors);print(json.dumps({'room':r['room'],'files':len(r['files']),'output':str(a.output)}))
