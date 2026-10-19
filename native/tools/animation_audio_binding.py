"""Source-class audio closure identity, retained through cumulative stream rolls."""
import hashlib,json
from pathlib import Path

def verified(row):
 path=Path(row['path']);raw=path.read_bytes()
 if hashlib.sha256(raw).hexdigest()!=row['sha256']:raise ValueError('Selected animation audio provenance changed: '+str(path))
 return raw

def from_stream(path):
 proof=json.loads(Path(path).read_text());evidence=json.loads(verified(proof['pcm_reference']))
 selected=evidence['catalog'].get('coverage')
 if not selected:return None
 coverage=json.loads(verified(selected))
 if coverage.get('scope',{}).get('class')!='SelectedAnimationDefinitions':return None
 sources={str(Path(row['path']).resolve()):row for row in coverage['sources']};roles={}
 for name in coverage['scope']['catalog_records']:
  source=sources[str(Path(name).resolve())];manifest=json.loads(verified(source))
  role={'hades-action-effect-resources-v1':'action','hades-original-actor-animations-v1':'actor'}.get(manifest['schema'])
  if not role:raise ValueError('Selected animation audio manifest class changed')
  roles[role]={'manifest':source,'packet':manifest['packet']}
  if role=='action':roles[role]['catalog']=manifest['catalog']
 return {'coverage':selected,'roles':roles}

def validate(receipt_path,action_path,actor_path):
 receipt=json.loads(Path(receipt_path).read_text());manifest=json.loads(verified(receipt['manifest']));verified(receipt['catalog'])
 expected={}
 for role,path in [('action',action_path),('actor',actor_path)]:
  path=Path(path);expected[role]=hashlib.sha256(path.read_bytes()).hexdigest()
 closures=receipt.get('selected_animation_audio_closures',[])
 if closures!=manifest.get('selected_animation_audio_closures',[]):raise ValueError('Selected animation audio receipt/manifest closure mismatch')
 for closure in closures:
  roles=closure['roles']
  if all(roles.get(role,{}).get('manifest',{}).get('sha256')==digest for role,digest in expected.items()):
   verified(closure['coverage'])
   for role in roles.values():
    for row in role.values():verified(row)
   return
 raise ValueError('Stale selected-animation audio closure: selected action/actor catalogs differ from audio receipt. Re-export selected animation cue class against these manifests before packaging: '+str(action_path)+' ; '+str(actor_path))
