#!/usr/bin/env python3.13
"""Original Constant primary graphics and merged source catalog; fresh outputs."""
import argparse,hashlib,json,struct,zlib
from pathlib import Path
from build_room_scene import identity
from hades_assets import Package,xnb_image
from export_actor_animations import string
from export_effect_initialization import lua

def export(a):
 if a.output.exists():raise ValueError('Fresh spawner output required')
 registry=json.loads(a.registry.read_text());catalog=json.loads(a.laurel.read_text())
 rows={r['name']:r['definition'] for r in registry['definitions']}
 # Derived from this room's map primary spawners (registry first-appearance
 # order); RoomOpening reproduces the previously reviewed Dust/Bat/Ember set.
 names=list(dict.fromkeys(x['name'] for x in registry['map_primary_spawners']))
 if not names:
  # A map without primary spawners (e.g. A_Combat02) loads the world spawner
  # catalog alone: no PrimaryRecipes, no spawner bank (lua_runtime.cpp loads
  # spawner-frames.bin only for PrimaryRecipes). Nothing is substituted.
  a.output.mkdir(parents=True);catalog['Definitions'].update(rows)
  (a.output/'effects.lua').write_text('return '+lua(catalog)+'\n');(a.output/'catalog.json').write_text(json.dumps(catalog,indent=2)+'\n')
  report={'schema':'hades-original-room-spawner-assets-v1','registry':identity(a.registry),'source_catalog':identity(a.laurel),'packet':None,'lua':identity(a.output/'effects.lua'),'catalog':identity(a.output/'catalog.json'),'graphics':[],'source_manifests':[],'source_payloads':[],'limits':['Room has no map primary spawners; no spawner bank exported.']}
  (a.output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'lua':report['lua'],'recipes':0}));return
 wanted={rows[n]['FilePath'] for n in names};matches={};manifests=[]
 # Exact namespaces already loaded for this room. Do not select arbitrary
 # duplicates from unrelated packages or replace a missing graphic.
 for package in ['Dev','Fx','Tartarus','RoomManager']:
  p=a.content/'Win/Packages'/(package+'.pkg_manifest');manifests.append(identity(p))
  for e in Package(p).entries():
   for s in e.get('sprites',[]):
    if s['name'] in wanted:
     if s['name'] in matches:raise ValueError('Ambiguous room primary graphic '+s['name'])
     matches[s['name']]={'package':package,'atlas':e['name'],'sprite':s}
 if set(matches)!=wanted:raise ValueError('Missing original room primary graphics '+repr(wanted-set(matches)))
 images={};payload_sources=[]
 for package in sorted({m['package'] for m in matches.values()}):
  p=a.content/'Win/Packages'/(package+'.pkg')
  for e in Package(p).entries():
   need=[(n,m)for n,m in matches.items()if m['package']==package and m['atlas']==e.get('name')]
   if not need:continue
   im,info=xnb_image(e['data']);payload_sources.append({'package':package,'atlas':e['name'],'xnb_sha256':hashlib.sha256(e['data']).hexdigest(),'location':e['location'],'decoded':info})
   for name,m in need:
    s=m['sprite'];x,y,w,h=s['rect']
    if min(x,y,w,h)<0 or x+w>im.width or y+h>im.height or s['scale_ratio']!=[1.,1.]:raise ValueError('Unsupported primary source image geometry')
    rgba=im.crop((x,y,x+w,y+h)).tobytes();images[name]={'rgba':rgba,'packed':zlib.compress(rgba,6),'width':w,'height':h,'origin':[s['original'][0]/2-s['trim'][0],s['original'][1]/2-s['trim'][1]],**m}
 a.output.mkdir(parents=True);packet=a.output/'spawner-frames.bin';graphics=[]
 with packet.open('xb')as out:
  out.write(b'HAA1'+struct.pack('<II',len(names),len(names)))
  for i,name in enumerate(names):
   graphic=rows[name]['FilePath'];f=images[graphic];w,h=f['width'],f['height'];s=f['sprite'];out.write(struct.pack('<8I2f',w,h,0,0,w,h,0,0,*f['origin']));out.write(struct.pack('<III',len(f['rgba']),len(f['packed']),zlib.crc32(f['rgba'])));out.write(f['packed']);graphics.append({'animation':name,'graphic':graphic,'clip_index':i,'atlas':f['atlas'],'width':w,'height':h,'rect':s['rect'],'trim':s['trim'],'original':s['original'],'rgba_sha256':hashlib.sha256(f['rgba']).hexdigest()})
  for i,name in enumerate(names):
   for value in [name,'','']:string(out,value)
   out.write(struct.pack('<4I4f',1,1,0,1,1.,0.,0.,1/60));out.write(struct.pack('<IIII',0,1,1,i));string(out,'{}')
 # Source omission remains separate in registry flags/provenance. Native
 # defaults in this consumed catalog are restricted to the reviewed loader.
 catalog['Definitions'].update(rows);catalog['PrimaryRecipes']=[x for x in registry['primary_recipes']if x['source_active'] and x['graphic'] in names];catalog['PrimaryGraphics']=graphics
 (a.output/'effects.lua').write_text('return '+lua(catalog)+'\n');(a.output/'catalog.json').write_text(json.dumps(catalog,indent=2)+'\n')
 report={'schema':'hades-original-room-spawner-assets-v1','registry':identity(a.registry),'source_catalog':identity(a.laurel),'packet':identity(packet),'lua':identity(a.output/'effects.lua'),'catalog':identity(a.output/'catalog.json'),'graphics':graphics,'source_manifests':manifests,'source_payloads':payload_sources,'limits':['Original images/source data only; current native map primary initializer must run at actual map creation/RNG point.','Only three selected Constant source definitions supported for native map init; their VFX leaf families remain explicit runtime dependencies.','Visibility and native SkipUpdate transitions require source owner state; alpha0 does not imply offscreen.']}
 (a.output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'packet':report['packet'],'lua':report['lua'],'recipes':len(catalog['PrimaryRecipes']),'graphics':graphics}))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for k in ['content','registry','laurel','output']:p.add_argument('--'+k,type=Path,required=True)
 export(p.parse_args())
