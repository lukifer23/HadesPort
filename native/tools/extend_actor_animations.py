#!/usr/bin/env python3
"""Derive encounter Unit movie roots and append original frames to a reviewed bank."""
import argparse,copy,hashlib,json,struct
from pathlib import Path
from types import SimpleNamespace
from build_room_scene import Sjson,merge
from lua_static import LuaFile,top_level_tables
from export_actor_animations import export,merged,string,identity

def strings(value):
 if isinstance(value,str):yield value
 elif isinstance(value,dict):
  for key,item in value.items():
   if not str(key).startswith('__'):yield from strings(item)
 elif isinstance(value,list):
  for item in value:yield from strings(item)

def inherited(table,name,stack=()):
 if name in stack:raise ValueError('Source inheritance cycle '+name)
 own=table[name];parents=own.get('InheritFrom',[]);parents=[parents]if isinstance(parents,str)else parents;result={}
 for parent in parents:result=merge(result,inherited(table,parent,stack+(name,)))
 return merge(result,own)

def append_banks(base,addition,output,coverage):
 if output.exists():raise ValueError('Fresh combined actor output required')
 output.mkdir(parents=True);frames=[];clips=[];packets=[];manifests=[]
 for directory in [base,addition]:
  manifest=json.loads((directory/'manifest.json').read_text());packet=directory/'actor-animations.bin'
  if identity(packet)['sha256']!=manifest['packet']['sha256']:raise ValueError('Actor source packet identity changed')
  frame_base=len(frames);frames.extend(copy.deepcopy(manifest['frames']))
  for clip in copy.deepcopy(manifest['clips']):
   if any(c['name']==clip['name']for c in clips):raise ValueError('Duplicate actor clip')
   for slide in clip['source_sequence']:slide['frame_indices']=[n+frame_base for n in slide['frame_indices']]
   clips.append(clip)
  packets.append((packet,manifest));manifests.append(manifest)
 target=output/'actor-animations.bin'
 with target.open('xb')as out:
  out.write(b'HAA1'+struct.pack('<II',len(frames),len(clips)))
  index=0
  for packet,manifest in packets:
   with packet.open('rb')as source:
    magic,n,c=struct.unpack('<4sII',source.read(12))
    if magic!=b'HAA1'or n!=len(manifest['frames'])or c!=len(manifest['clips']):raise ValueError('Actor packet header mismatch')
    for row in manifest['frames']:
     header=source.read(52)
     if len(header)!=52:raise ValueError('Truncated actor frame')
     raw,packed,crc=struct.unpack_from('<III',header,40)
     if(raw,packed,crc)!=(row['raw_bytes'],row['compressed_bytes'],row['crc32']):raise ValueError('Actor frame manifest mismatch')
     data=source.read(packed)
     if len(data)!=packed:raise ValueError('Truncated actor payload')
     frames[index]['index']=index;frames[index]['packet_payload_offset']=out.tell()+52;out.write(header);out.write(data);index+=1
  for c in clips:
   for k in ['name','chain','move_animation']:string(out,c[k])
   out.write(struct.pack('<4I4f',c['original_angles'],len(c['angle_indices']),c['flags'],len(c['source_sequence']),c['world_scale'],c['offset_x'],c['offset_y'],c['seconds_per_tick']))
   out.write(struct.pack('<'+'I'*len(c['angle_indices']),*c['angle_indices']))
   for s in c['source_sequence']:
    out.write(struct.pack('<II',s['original_frame'],s['ticks']));out.write(struct.pack('<'+'I'*len(s['frame_indices']),*s['frame_indices']));string(out,json.dumps(s['events'],separators=(',',':')))
 report=copy.deepcopy(manifests[0]);report['packet']=identity(target);report['clips']=clips;report['frames']=frames
 report['sources']=list({x['path']:x for m in manifests for x in m['sources']}.values());report['movies']=[x for m in manifests for x in m['movies']]
 report['statistics']={'clips':len(clips),'movie_frames':len(frames),'cropped_raw_bytes':sum(f['raw_bytes']for f in frames),'zlib_bytes':sum(f['compressed_bytes']for f in frames),'max_frame_rgba_bytes':max(f['raw_bytes']for f in frames)}
 report['appended_banks']=[identity(p)for p,_ in packets];report['encounter_coverage']=coverage
 (output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
 return report

def main(a):
 catalog=json.loads(a.weapons.read_text());closure=catalog['forced_encounter_closure'];unit_names=[r['unit_name']for r in closure['units']]
 sources=sorted((a.content/'Game/Animations').glob('*.sjson'));animations={x['Name']:x for p in sources for x in Sjson(p.read_text()).parse()['Animations']}
 units={x['Name']:x for p in sorted((a.content/'Game/Units').glob('*.sjson'))for x in Sjson(p.read_text()).parse()['Units']}
 lua_enemies={};lua_weapons={};proof=[identity(a.weapons)]
 for pattern in ['EnemyData*.lua','WeaponData*.lua']:
  for path in sorted((a.content/'Scripts').glob(pattern)):
   proof.append(identity(path))
   for root,value,line,*_ in top_level_tables(LuaFile(path)):
    if root.startswith('UnitSetData.')and isinstance(value,dict):lua_enemies.update(value)
    elif root=='WeaponData'and isinstance(value,dict):lua_weapons.update(value)
 roots={};selected_weapons={n for row in closure['units']for n in row['lua_weapon_options']}
 for unit in unit_names:
  for origin,value in [('native-unit',inherited(units,unit)),('lua-enemy',inherited(lua_enemies,unit))]:
   for name in strings(value):
    if name in animations:roots.setdefault(name,[]).append({'kind':origin,'name':unit})
 for weapon in catalog['weapons']:
  if weapon['name']not in selected_weapons:continue
  for origin,value in [('native-weapon',weapon['effective_fields']),('lua-weapon',inherited(lua_weapons,weapon['name'])if weapon['name']in lua_weapons else{})]:
   for name in strings(value):
    if name in animations:roots.setdefault(name,[]).append({'kind':origin,'name':weapon['name']})
 admitted=[];rejected={};queue=list(roots)
 while queue:
  name=queue.pop(0)
  if name in admitted or name in rejected:continue
  d,_=merged(animations,name)
  if not d.get('VideoTexture')or d.get('Type')not in ['Slide','Book']:rejected[name]='Nonmovie animation belongs to existing action/scene banks';continue
  unsupported=[k for k in ['PlayBackwards','LoopFramesOnly','PingPong','StartRed','StartGreen','StartBlue','DurationFrames','Duration','StartAlpha','EndAlpha','Color']if d.get(k)]
  if unsupported:rejected[name]='Movie primary operation not reconstructed: '+','.join(unsupported);continue
  extra={k for slide in d.get('Slides',[])for k in slide}-{'DurationFrames','Sound','FootstepSound','FootstepFxR','FootstepFxL','VisualFx','RandomDurationFramesMin','RandomDurationFramesMax'}
  if extra:rejected[name]='Movie slide operation not reconstructed: '+','.join(sorted(extra));continue
  if d.get('Type')=='Book'and d.get('Slides'):rejected[name]='Authored Book slide-event expansion not reconstructed';continue
  admitted.append(name)
  for key in ['ChainTo','MoveAnimation','StopAnimation']:
   target=d.get(key)
   if target and target!='null'and target in animations:roots.setdefault(target,[]).append({'kind':key,'name':name});queue.append(target)
 coverage={'room':closure['room'],'encounter':closure['encounter'],'units':unit_names,'roots':roots,'admitted':admitted,'rejected':rejected,'sources':proof,'limits':['Bounded to forced encounter units and their Lua/native weapon options.','Unimplemented movie operations remain explicit; action-bank resources retain their own admission.']}
 a.decode_output.parent.mkdir(parents=True,exist_ok=True)
 export(SimpleNamespace(content=a.content,decoder=a.decoder,output=a.decode_output,directions=8,sources=sources,names=admitted,unit_scale=1))
 report=append_banks(a.base,a.decode_output,a.output,coverage);print(json.dumps(report['statistics']))

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for flag in ['content','weapons','decoder','base','decode-output','output']:p.add_argument('--'+flag,type=Path,required=True)
 main(p.parse_args())
