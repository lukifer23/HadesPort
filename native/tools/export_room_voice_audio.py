#!/usr/bin/env python3
"""Append preserved voice samples reached by one original room's data closure."""
import argparse,json
from pathlib import Path
from lua_static import LuaFile,top_level_tables
from build_room_scene import identity
from export_voice_evidence import export as voice_export
from export_audio_streams import export as stream_export
from export_audio_events import export as event_export


def export(a):
 if a.coverage.exists():raise ValueError('Fresh room voice coverage report required')
 rooms={};sources={}
 for path in sorted((a.content/'Scripts').glob('RoomData*.lua')):
  proof=identity(path)
  for root,value,line,*_ in top_level_tables(LuaFile(path)):
   if not root.startswith('RoomSetData.')or not isinstance(value,dict):continue
   for name,record in value.items():
    if not isinstance(record,dict):continue
    if name in rooms:raise ValueError('Ambiguous original room data '+name)
    rooms[name]=record;sources[name]={**proof,'root':root,'line':line,'record':name}
 cues={};visited=[]
 def walk(value,path,room):
  if isinstance(value,dict):
   for key,item in value.items():
    if key=='Cue'and isinstance(item,str)and item.startswith('/VO/'):
     cues.setdefault(item,{'kind':'programmer_voice','provenance':[]})['provenance'].append({'room':room,'path':path+[key],'source':sources[room]})
    else:walk(item,path+[str(key)],room)
  elif isinstance(value,list):
   for index,item in enumerate(value):walk(item,path+[index+1],room)
 def room(name,stack=()):
  if name in stack:raise ValueError('Room data inheritance cycle')
  if name in visited:return
  own=rooms[name];parents=own.get('InheritFrom',[])
  for parent in([parents]if isinstance(parents,str)else parents):room(parent,stack+(name,))
  visited.append(name);walk(own,[],name)
 room(a.room)
 report={'schema':'hades-audio-coverage-v1','scope':{'room':a.room,'room_inheritance':visited,'boundary':'Literal VO Cue fields in the selected original room and its inherited room data only.'},'cues':cues,'sources':[sources[n]for n in visited]}
 a.coverage.parent.mkdir(parents=True,exist_ok=True);a.coverage.write_text(json.dumps(report,indent=2)+'\n')
 voice_export(a.content,a.decoder,a.evidence,[],32*1024*1024,a.coverage)
 stream_export(a.content,a.evidence/'manifest.json',a.decoder,a.streams)
 previous=json.loads(a.base_manifest.read_text())
 return event_export(a.content,Path(previous['stream_manifest']['path']),a.output,
                     [*[Path(p['path'])for p in previous['additional_stream_manifests']],a.streams/'manifest.json'],
                     bool(previous.get('mixing_control')),True)


if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--room',required=True)
 for flag in ['content','decoder','base-manifest','coverage','evidence','streams','output']:p.add_argument('--'+flag,type=Path,required=True)
 result=export(p.parse_args());print(json.dumps(result))
