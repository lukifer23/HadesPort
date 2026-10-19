"""Verify and stage source Thing.Light data plus lossless original frame bank."""
from pathlib import Path
import hashlib,json,shutil
TARGET='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'
def stage_actor_lights(manifest_path,assets):
 manifest_path=Path(manifest_path);assets=Path(assets);manifest=json.loads(manifest_path.read_bytes())
 if manifest['schema']!='hades-actor-light-resources-v1' or manifest['target_sha256']!=TARGET:raise ValueError('Actor light source target mismatch')
 def verified(record):
  path=Path(record['path'])
  if path.is_symlink()or not path.is_file()or hashlib.file_digest(path.open('rb'),'sha256').hexdigest()!=record['sha256']:raise ValueError('Original actor light identity changed: '+str(path))
  return path
 generated={k:verified(manifest[k])for k in ['packet','lua','catalog']}
 for record in [manifest['creation'],manifest['animation_source'],*manifest['manifests']]:verified(record)
 for payload in manifest['payloads']:verified(payload['package'])
 creation=json.loads(verified(manifest['creation']).read_bytes());verified(creation['map_source'])
 for record in creation['definition_sources']:verified(record)
 catalog=json.loads(generated['catalog'].read_bytes())
 if catalog['Schema']!='HadesActorLightResources1' or catalog['Target']!=TARGET or catalog['ImageBankSha256']!=manifest['packet']['sha256']:raise ValueError('Actor light catalog/frame-bank mismatch')
 original_root=Path(manifest['animation_source']['path']).parents[2].resolve()
 if assets.resolve().is_relative_to(original_root):raise ValueError('Actor light staging cannot overwrite originals')
 assets.mkdir(parents=True,exist_ok=True)
 for key,name in [('packet','actor-light-frames.bin'),('lua','actor-lights.lua')]:
  destination=assets/name
  if destination.is_symlink():raise ValueError('Actor light output is a symlink')
  shutil.copyfile(generated[key],destination)
 identity=assets/'actor-light-sha256.bin'
 if identity.is_symlink():raise ValueError('Actor light identity output is a symlink')
 identity.write_bytes(bytes.fromhex(manifest['packet']['sha256']))
 return {'controllers':manifest['controllers'],'actors':manifest['actors'],'packet_sha256':manifest['packet']['sha256']}
