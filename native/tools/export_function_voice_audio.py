#!/usr/bin/env python3
"""Append complete original voice tables selected by source presentation functions."""
import argparse,json
from pathlib import Path
from lua_static import LuaFile,top_level_tables
from build_room_scene import identity
from export_voice_evidence import export as voice_export
from export_audio_streams import export as stream_export
from export_audio_events import export as event_export

def export(a):
 if a.coverage.exists():raise ValueError('Fresh function voice coverage required')
 scripts=[LuaFile(path)for path in sorted((a.content/'Scripts').glob('*.lua'))]
 tables={};proof={}
 for script in scripts:
  for name,value,line,*_ in top_level_tables(script):
   if name=='HeroVoiceLines' or name.startswith('GlobalVoiceLines.'):
    tables[name]=value;proof[name]={**identity(script.path),'line':line}
 selected={};consumers=[]
 for function in a.function:
  matches=[(script,rec)for script in scripts for rec in script.functions if rec['name']==function]
  if len(matches)!=1:raise ValueError('Unique original voice consumer required: '+function)
  script,rec=matches[0];tokens=script.tokens[rec['start']:rec['end']+1];consumer={**identity(script.path),'function':function,'line':rec['line']};consumers.append(consumer)
  for i in range(len(tokens)-2):
   if tokens[i].kind!='name' or tokens[i].value not in ('HeroVoiceLines','GlobalVoiceLines') or tokens[i+1].value!='.' or tokens[i+2].kind!='name':continue
   root,key=tokens[i].value,tokens[i+2].value;name=root+'.'+key
   value=tables.get(name)if root=='GlobalVoiceLines'else tables[root].get(key)
   if value is None:raise ValueError('Original voice table unresolved: '+name)
   selected[name]={'value':value,'source':proof[name if root=='GlobalVoiceLines'else root],'consumer':consumer}
 cues={}
 def walk(value,path,record):
  if isinstance(value,dict):
   for key,item in value.items():
    if key=='Cue' and isinstance(item,str)and item.startswith('/VO/'):
     cues.setdefault(item,{'kind':'programmer_voice','provenance':[]})['provenance'].append({'table':record,'path':path+[key],'source':selected[record]['source'],'consumer':selected[record]['consumer']})
    else:walk(item,path+[str(key)],record)
  elif isinstance(value,list):
   for index,item in enumerate(value):walk(item,path+[index+1],record)
 for name,record in selected.items():walk(record['value'],[],name)
 if not cues:raise ValueError('Source functions selected no original voice tables')
 report={'schema':'hades-audio-coverage-v1','scope':{'functions':a.function,'voice_tables':sorted(selected),'boundary':'All original Cue records in literal HeroVoiceLines/GlobalVoiceLines fields selected by original function bodies, including every conditional alternative.'},'cues':cues,'sources':consumers}
 a.coverage.parent.mkdir(parents=True,exist_ok=True);a.coverage.write_text(json.dumps(report,indent=2)+'\n')
 voice_export(a.content,a.decoder,a.evidence,[],64*1024*1024,a.coverage)
 stream_export(a.content,a.evidence/'manifest.json',a.decoder,a.streams)
 previous=json.loads(a.base_manifest.read_text())
 return event_export(a.content,Path(previous['stream_manifest']['path']),a.output,[*[Path(p['path'])for p in previous['additional_stream_manifests']],a.streams/'manifest.json'],bool(previous.get('mixing_control')),True)

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--function',action='append',required=True)
 for flag in ['content','decoder','base-manifest','coverage','evidence','streams','output']:p.add_argument('--'+flag,type=Path,required=True)
 print(json.dumps(export(p.parse_args())))
