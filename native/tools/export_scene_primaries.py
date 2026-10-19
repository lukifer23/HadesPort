#!/usr/bin/env python3.13
"""Bind source primary recipes to original scene resources and door child art."""
import argparse,hashlib,json,struct,zlib
from pathlib import Path
from build_room_scene import identity
from export_room_creation import RangedSjson
from export_effect_initialization import lua
from export_actor_animations import string
from hades_assets import Package,xnb_image

def export(a):
 if a.output.exists():raise ValueError('Fresh output required')
 registry=json.loads(a.registry.read_text());scene=json.loads(Path(registry['scene']['path']).read_text());hda=json.loads(Path(registry['hda']['path']).read_text())
 if registry['target_sha256']!='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254':raise ValueError('Native target mismatch')
 definitions={r['name']:r['definition']for r in registry['definitions']};provenance={r['name']:r['source']for r in registry['definitions']}
 source=a.content/'Game/Animations/Fx.sjson';raw=source.read_bytes();parser=RangedSjson(raw.decode('utf-8-sig'));original={d['Name']:d for d in parser.parse()['Animations']}
 def resolve(name):
  if name in definitions:return definitions[name]
  own=original[name];parent=own.get('InheritFrom');merged={**(resolve(parent)if parent and parent!='null'else{}),**own};definitions[name]=merged
  span=parser.ranges[id(own)];provenance[name]={'path':str(source),'file_sha256':hashlib.sha256(raw).hexdigest(),'range':span,'range_sha256':hashlib.sha256(raw[span[0]:sum(span)]).hexdigest()};return merged
 resolve('DoorLocked_MetaReward');resolve('RoomRewardUnavailable_MetaReward')
 gui=a.content/'Game/Animations/GUIAnimations.sjson';gui_raw=gui.read_bytes();gp=RangedSjson(gui_raw.decode('utf-8-sig'));gd={d['Name']:d for d in gp.parse()['Animations']};v=gd['VignetteOverlay'];definitions['VignetteOverlay']=v;span=gp.ranges[id(v)];provenance['VignetteOverlay']={'path':str(gui),'file_sha256':hashlib.sha256(gui_raw).hexdigest(),'range':span,'range_sha256':hashlib.sha256(gui_raw[span[0]:sum(span)]).hexdigest()}
 dev=a.content/'Game/Animations/1_DevAnimations.sjson';dev_raw=dev.read_bytes();dp=RangedSjson(dev_raw.decode('utf-8-sig'));blank=next(d for d in dp.parse()['Animations']if d['Name']=='Blank');definitions['Blank']=blank;span=dp.ranges[id(blank)];provenance['Blank']={'path':str(dev),'file_sha256':hashlib.sha256(dev_raw).hexdigest(),'range':span,'range_sha256':hashlib.sha256(dev_raw[span[0]:sum(span)]).hexdigest()}
 obstacle=a.content/'Game/Animations/ObstacleAnimations.sjson';obstacle_raw=obstacle.read_bytes();op=RangedSjson(obstacle_raw.decode('utf-8-sig'));torch=next(d for d in op.parse()['Animations']if d['Name']=='TorchMarker');definitions['TorchMarker']=torch;span=op.ranges[id(torch)];provenance['TorchMarker']={'path':str(obstacle),'file_sha256':hashlib.sha256(obstacle_raw).hexdigest(),'range':span,'range_sha256':hashlib.sha256(obstacle_raw[span[0]:sum(span)]).hexdigest()}
 auxiliary=['RoomRewardUnavailable','RoomRewardUnavailable_MetaReward']
 auxiliary += sorted(name for name,d in definitions.items()if d.get('Type','ConstantNoUpdate')=='ConstantNoUpdate' and d.get('FilePath'))
 auxiliary += ['VignetteOverlay']
 auxiliary += sorted({r['graphic']for r in registry['primary_recipes']if r['kind']!='named'and not r['resources']['scene_sprite_indices']and r['graphic']!='3DGhostIdle'})
 clip_names={}
 for name in auxiliary:
  d=definitions.get(name,{});prefix=d.get('FilePath',name);n=d.get('NumFrames',1)
  clip_names[name]=[prefix+f'{i:04}'for i in range(1,n+1)]if n>1 else[prefix]
 wanted={n for row in clip_names.values()for n in row};matches={};packages=a.content/'Win/Packages';manifests=[]
 # Original manifests can contain several scale variants. Prefer the actual
 # ratio closest to1, retaining the exact selected atlas geometry and package.
 for package in ['RoomManager','Tartarus','Dev','GUI','Fx','Temple']:
  path=packages/(package+'.pkg_manifest')
  if not path.exists():continue
  manifests.append(identity(path))
  for e in Package(path).entries():
   for s in e.get('sprites',[]):
    if s['name']in wanted:
     candidate={'package':package,'atlas':e['name'],'sprite':s}
     old=matches.get(s['name'])
     if old is None or sum(abs(x-1)for x in s['scale_ratio'])<sum(abs(x-1)for x in old['sprite']['scale_ratio']):matches[s['name']]=candidate
 if set(matches)!=wanted:raise ValueError('Missing original door child frames')
 images={};payloads=[]
 for package in sorted({r['package']for r in matches.values()}):
  for e in Package(packages/(package+'.pkg')).entries():
   if e.get('name')in {r['atlas']for r in matches.values()if r['package']==package}:
    images[(package,e['name'])],info=xnb_image(e['data']);payloads.append({'package':package,'atlas':e['name'],'location':e['location'],'xnb_sha256':hashlib.sha256(e['data']).hexdigest(),'image':info})
 frames=[]
 for name,m in sorted(matches.items()):
  s=m['sprite'];x,y,w,h=s['rect'];im=images[(m['package'],m['atlas'])];rx,ry=s['scale_ratio']
  if min(x,y,w,h)<0 or min(rx,ry)<=0 or max(rx,ry)>16 or x+w>im.width or y+h>im.height:raise ValueError('Invalid original atlas transform')
  rgba=im.crop((x,y,x+w,y+h)).tobytes();frames.append({'name':name,**m,'rgba':rgba,'packed':zlib.compress(rgba,6),'origin':[s['original'][0]/rx*.5-s['trim'][0],s['original'][1]/ry*.5-s['trim'][1]],'rgba_sha256':hashlib.sha256(rgba).hexdigest()})
 a.output.mkdir(parents=True);packet=a.output/'primary-child-frames.bin'
 with packet.open('xb')as out:
  out.write(b'HAA1'+struct.pack('<II',len(frames),len(auxiliary)))
  for i,f in enumerate(frames):
   w,h=f['sprite']['rect'][2:];out.write(struct.pack('<8I2f',w,h,0,0,w,h,i,0,*f['origin']));out.write(struct.pack('<III',len(f['rgba']),len(f['packed']),zlib.crc32(f['rgba'])));out.write(f['packed'])
  frame_ids={f['name']:i for i,f in enumerate(frames)}
  for name in auxiliary:
   for s in [name,'','']:string(out,s)
   rows=clip_names[name];out.write(struct.pack('<4I4f',1,1,513 if len(rows)>1 else 0,len(rows),1.,0.,0.,1/30));out.write(struct.pack('<I',0))
   for i,row in enumerate(rows):out.write(struct.pack('<III',i+1,1,frame_ids[row]));string(out,'{}')
 hda_by_name={c['name']:c['index']for c in hda['clips']};resources={}
 for name in dict.fromkeys(list(definitions)+auxiliary):
  d=definitions.get(name,{})
  kind='hda'if name in hda_by_name else 'aux'if name in auxiliary else 'source-data'
  index=hda_by_name.get(name,auxiliary.index(name)if name in auxiliary else None)
  if name=='DoorLocked_MetaReward':kind,index='hda',hda_by_name['DoorLocked']
  resources[name]={'Kind':kind,'Index':index,'FilePath':d.get('FilePath',name),'AtlasScale':matches[clip_names[name][0]]['sprite']['scale_ratio']if name in auxiliary else[1.,1.]}
 recipes=[]
 for r in registry['primary_recipes']:
  recipes.append({'OwnerId':r['owner_map_id'],'Graphic':r['graphic'],'Type':r['resolved_type'],'Named':r['kind']=='named','SceneSprites':r['resources']['scene_sprite_indices'],'HaaClip':r['resources']['haa_clip_index'],'Active':r['source_active'],'DoCreate':r['source_do_create']})
 catalog={'Schema':'HadesScenePrimaries1','Target':registry['target_sha256'],'SceneSha256':scene['scene']['sha256'],'AnimationSha256':hashlib.sha256((Path(registry['hda']['path']).parent/'animations.bin').read_bytes()).hexdigest(),'Definitions':definitions,'Resources':resources,'Recipes':recipes,'AuxBankSha256':hashlib.sha256(packet.read_bytes()).hexdigest(),'DynamicGraphics':['VignetteOverlay','Blank','TorchMarker'],'CompiledDefaults':{'OnlyWhenVisible':False,'CancelOverCap':False,'Loop':False,'LoopFramesOnly':False,'AllowRestart':False,'BlockNewOwnerAnims':False,'OwnerImmobile':False,'OwnerHasNoCollision':False,'DisableOwnerWeapons':False,'Scale':1.,'OffsetX':0.,'OffsetY':0.,'OffsetZ':0.,'AirborneZ':0.,'Hue':0.,'Saturation':0.,'Value':0.}}
 (a.output/'primaries.lua').write_text('return '+lua(catalog)+'\n');(a.output/'catalog.json').write_text(json.dumps(catalog,indent=2)+'\n')
 report={'schema':'hades-source-scene-primaries-v1','catalog':identity(a.output/'catalog.json'),'lua':identity(a.output/'primaries.lua'),'aux_bank':identity(packet),'registry':identity(a.registry),'source':identity(source),'definition_provenance':provenance,'source_payloads':payloads,'aux_frames':[{k:v for k,v in f.items()if k not in ('rgba','packed')}for f in frames],'limits':['Source recipes are not live allocation counts. Runtime must initialize at actual registration before eachOnSpawn.','Source-data resources absent current scene are retained explicitly; no invented pixels or assumption resource exists.','Original v5 scene and HDA bytes untouched. Door child was missing from the prior v5 render and is supplied by this fresh original frame bank.','Compiled defaults restricted to exact target constructor zero template/copy and explicitScale1 writes; source omission remains separate in provenance.']}
 (a.output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'catalog':report['lua'],'aux':report['aux_bank'],'definitions':len(definitions),'recipes':len(recipes)}))

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for k in ['content','registry','output']:p.add_argument('--'+k,type=Path,required=True)
 export(p.parse_args())
