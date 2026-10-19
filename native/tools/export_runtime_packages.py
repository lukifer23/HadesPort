#!/usr/bin/env python3.13
"""Export source SGG manifests and decoded atlas payloads for the native loader."""
import argparse,hashlib,json,sys
from pathlib import Path
from hades_assets import Package,xnb_image
from export_actor_catalog import literal as lua_literal

def identity(path):
 return {'path':str(path.resolve()),'sha256':hashlib.file_digest(path.open('rb'),'sha256').hexdigest(),'bytes':path.stat().st_size}

def main():
 p=argparse.ArgumentParser();p.add_argument('--content',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--names',nargs='+',required=True);a=p.parse_args()
 if a.output.exists():raise ValueError('Choose a fresh output directory')
 a.output.mkdir(parents=True);textures=a.output/'package-textures';textures.mkdir();catalog={'schema':'hades-native-asset-packages-v1','packages':{}};sources=[]
 for name in a.names:
  if not name or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-'for c in name):raise ValueError('Invalid package name')
  stem=a.content/'Win/Packages'/name;manifest=stem.with_suffix('.pkg_manifest');payload=stem.with_suffix('.pkg');rows=list(Package(manifest).entries());raw=list(Package(payload).entries());atlas={x['name']:x for x in rows if x['type']=='atlas'}
  includes=[x['name']for x in rows if x['type']=='include'];entries=[]
  for entry in raw:
   if entry['type']!='texture':raise ValueError('Unsupported portable package entry '+entry['type'])
   im,info=xnb_image(entry['data']);rgba=im.tobytes();digest=hashlib.sha256(rgba).hexdigest();target=textures/(digest+'.rgba')
   if not target.exists():target.write_bytes(rgba)
   meta=atlas.pop(entry['name'],None)
   if meta is None:raise ValueError('Texture has no source atlas manifest')
   entries.append({'name':entry['name'],'width':im.width,'height':im.height,'path':'package-textures/'+target.name,'sha256':digest,'sprites':meta['sprites'],'xnb_sha256':hashlib.sha256(entry['data']).hexdigest()})
  if atlas:raise ValueError('Unresolved atlas payloads')
  catalog['packages'][name]={'manifest_sha256':identity(manifest)['sha256'],'package_sha256':identity(payload)['sha256'],'includes':includes,'textures':entries};sources +=[identity(manifest),identity(payload)]
 packet=a.output/'packages.lua';packet.write_text('return '+lua_literal(catalog)+'\n');(a.output/'manifest.json').write_text(json.dumps({'schema':catalog['schema'],'sources':sources,'packet':identity(packet),'packages':list(catalog['packages']),'texture_bytes':sum(x.stat().st_size for x in textures.iterdir()),'limits':['Original source pixels/atlas geometry, decoded to portable RGBA. CPU residency and GPU texture uploads are distinct states; no audio or movie coverage implied.']},indent=2)+'\n');print(json.dumps({'packet':identity(packet),'packages':list(catalog['packages'])}))
if __name__=='__main__':main()
