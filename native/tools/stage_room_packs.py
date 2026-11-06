#!/usr/bin/env python3
"""Stage actual exported room resources in a fresh, source-verified map bundle."""
import argparse,hashlib,json,shutil,sys,tempfile
from pathlib import Path
from types import SimpleNamespace
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from build_native import stage_room_scene
from stage_actor_lights import stage_actor_lights
from room_pack_contract import TARGET, validate_room_name, validate_room_manifest

def identity(path):
 path=Path(path).resolve();return {'path':str(path),'bytes':path.stat().st_size,'sha256':hashlib.file_digest(path.open('rb'),'sha256').hexdigest()}
def verify(row):
 path=Path(row['path'])
 if not path.is_file() or ('bytes'in row and path.stat().st_size!=row['bytes']):raise ValueError('Room source identity changed: '+str(path))
 with path.open('rb')as stream:
  if hashlib.file_digest(stream,'sha256').hexdigest()!=row['sha256']:raise ValueError('Room source identity changed: '+str(path))
 return path
def copy_verified(row,destination):
 shutil.copyfile(verify(row),destination)
 verify({**row,'path':str(destination)})
def room_output_path(path):
 path=Path(path).resolve()
 if path.is_relative_to((ROOT/'.local/game').resolve()):raise ValueError('Room output cannot overwrite originals')
 return path
def build_room_pack(source,output,lights=None,actors=None):
 return publish_room_pack(output,lambda candidate:_build_room_pack(source,candidate,lights,actors))
def publish_room_pack(output,build):
 """Publish a complete immutable bundle; failed producers leave no stale output."""
 if Path(output).exists()or Path(output).is_symlink():raise ValueError('Fresh room output required')
 output=room_output_path(output)
 output.parent.mkdir(parents=True,exist_ok=True)
 with tempfile.TemporaryDirectory(prefix='room-build-',dir=output.parent)as temporary:
  candidate=Path(temporary)/output.name
  result=build(candidate);validate_room_manifest(result)
  for name,row in result['files'].items():
   verify(row)
   # Identity records refer to the published bundle, not its scratch path.
   row['path']=str(output/name)
  (candidate/'manifest.json').write_text(json.dumps(result,indent=2)+'\n')
  if output.exists()or output.is_symlink():raise ValueError('Fresh room output required')
  candidate.rename(output)
 return result
def _build_room_pack(source,output,lights=None,actors=None):
 source,output=Path(source).resolve(),Path(output).resolve()
 geometry=json.loads((source/'geometry.json').read_text());room=geometry['room']
 validate_room_name(room)
 output.mkdir(parents=True)
 args=SimpleNamespace(scene=source/'scene/scene-affine.bin',animations=source/'scene-animation/animations.bin',geometry=source/'geometry.json',creation=source/'creation.json/creation.json',ambient=source/'ambient/ambient.bin',composition=source/'composition/composition.bin',collision=source/'collision/room-geometry.json',parallax=source/'parallax/parallax.bin',hulls=source/'hulls/hulls.bin',hero=ROOT/'.local/native-assets/hero-eight-directions')
 reports=stage_room_scene(args,output,include_shared=False)
 actor=json.loads(Path(actors or source/'actors/manifest.json').read_text())
 if actor['schema']!='hades-source-actor-catalog-v1' or actor['room']!=room or actor.get('native_unit_angles')is not True:raise ValueError('Room actor schema/name mismatch')
 for row in [actor['packet'],actor['creation'],actor['defaults'],*actor['source_files']]:verify(row)
 copy_verified(actor['packet'],output/'actors.lua')
 effects=json.loads((source/'spawners/manifest.json').read_text())
 if effects['schema']!='hades-original-room-spawner-assets-v1':raise ValueError('Room spawner schema')
 registry=json.loads(verify(effects['registry']).read_text())
 for row in [effects[k]for k in ['lua','source_catalog','catalog','packet']]+effects['source_manifests']+[registry[k]for k in ['creation','scene','hda','haa']]+registry['sources']:verify(row)
 for key,name in [('lua','effects.lua'),('packet','spawner-frames.bin')]:copy_verified(effects[key],output/name)
 leaf=json.loads((source/'leaf/manifest.json').read_text())
 if leaf['schema']!='hades-original-room-leaf-frames-v1':raise ValueError('Room leaf schema')
 for row in [leaf[k]for k in ['packet','facing','identity','registry']]+leaf['sources']:verify(row)
 for key in ['packet','facing','identity']:
  path=verify(leaf[key]);copy_verified(leaf[key],output/path.name)
 primary=json.loads((source/'primaries/manifest.json').read_text())
 if primary['schema']not in ['hades-source-scene-primaries-v1','hades-source-scene-primaries-v2']:raise ValueError('Room primary schema')
 for key in ['catalog','lua','aux_bank','registry','source']:verify(primary[key])
 for row in primary['definition_provenance'].values():verify({'path':row['path'],'sha256':row['file_sha256']})
 for key in ['lua','aux_bank']:
  path=verify(primary[key]);copy_verified(primary[key],output/path.name)
 stage_actor_lights(lights or source/'lights/manifest.json',output)
 result={'schema':'hades-room-pack-v1','target_sha256':TARGET,'room':room,'source':str(source),'files':{p.name:identity(p)for p in sorted(output.iterdir())if p.is_file()},'reports':reports}
 return result
def stage_room_pack(manifest,assets):
 m=json.loads(Path(manifest).read_text());room=validate_room_manifest(m)
 assets=room_output_path(assets)
 if not assets.is_dir():raise ValueError('Existing asset stage required')
 rooms=assets/'rooms'
 if rooms.is_symlink():raise ValueError('Room stage cannot be a symlink')
 destination=rooms/room
 if destination.exists()or destination.is_symlink():raise ValueError('Duplicate room selection: '+room)
 # Check the entire manifest before writing, then verify the actual copied
 # bytes too. A failed copy or changing source cannot publish a partial map.
 for row in m['files'].values():verify(row)
 rooms.mkdir(exist_ok=True)
 with tempfile.TemporaryDirectory(prefix='room-stage-',dir=rooms)as temporary:
  candidate=Path(temporary)/room;candidate.mkdir()
  for name,row in m['files'].items():copy_verified(row,candidate/name)
  if destination.exists()or destination.is_symlink():raise ValueError('Duplicate room selection: '+room)
  candidate.rename(destination)
 return {'room':room,'files':len(m['files'])}
def revise_room_actor_catalog(manifest,actors,output,primaries=None):
 """Fresh bundle: retain immutable room bytes, replace validated catalogs."""
 return publish_room_pack(output,lambda candidate:_revise_room_actor_catalog(manifest,actors,candidate,primaries))
def _revise_room_actor_catalog(manifest,actors,output,primaries=None):
 manifest,actors,output=Path(manifest).resolve(),Path(actors).resolve(),Path(output).resolve()
 base=json.loads(manifest.read_text());actor=json.loads(actors.read_text())
 validate_room_manifest(base)
 if actor['schema']!='hades-source-actor-catalog-v1' or actor['room']!=base['room'] or actor.get('native_unit_angles')is not True:raise ValueError('Room revision actor schema/name/angles')
 for row in [actor['packet'],actor['creation'],actor['defaults'],*actor['source_files']]:verify(row)
 replacements={'actors.lua':actor['packet']}
 if primaries:
  primary=json.loads(Path(primaries).read_text())
  if primary['schema']not in ['hades-source-scene-primaries-v1','hades-source-scene-primaries-v2']:raise ValueError('Room revision primary schema')
  for key in ['catalog','lua','aux_bank','registry','source']:verify(primary[key])
  for row in primary['definition_provenance'].values():verify({'path':row['path'],'sha256':row['file_sha256']})
  if primary.get('dynamic_coverage'):verify(primary['dynamic_coverage'])
  catalog=json.loads(verify(primary['catalog']).read_text())
  if catalog['SceneSha256']!=verify(base['files']['scene-sha256.bin']).read_bytes().hex() or catalog['AnimationSha256']!=verify(base['files']['animations-sha256.bin']).read_bytes().hex():raise ValueError('Room revision primary scene binding')
  replacements.update({'primaries.lua':primary['lua'],'primary-child-frames.bin':primary['aux_bank']})
 output.mkdir(parents=True)
 for name,row in base['files'].items():
  if Path(name).name!=name:raise ValueError('Room revision filename')
  source=verify(row)
  if name in replacements:copy_verified(replacements[name],output/name)
  else:
   (output/name).hardlink_to(source)
   verify({**row,'path':str(output/name)})
 revised={**base,'revision_source':identity(manifest),'actor_catalog_source':identity(actors),
  'files':{p.name:identity(p)for p in sorted(output.iterdir())if p.is_file()}}
 if primaries:revised['primary_catalog_source']=identity(primaries)
 return revised
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--actor-lights',type=Path);p.add_argument('--actors',type=Path);a=p.parse_args();r=build_room_pack(a.source,a.output,a.actor_lights,a.actors);print(json.dumps({'room':r['room'],'files':len(r['files']),'output':str(a.output)}))
