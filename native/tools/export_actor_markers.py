#!/usr/bin/env python3
"""Source-bound original hero marker tracks; never executes game code."""
import argparse,hashlib,json,math,struct
from pathlib import Path
from build_room_scene import identity
from hades_assets import Reader

def string(out,value):
 b=value.encode();out.write(struct.pack('<I',len(b)));out.write(b)
def markers(path):
 r=Reader(path.read_bytes())
 if r.read(4)!=b'SGB1':raise ValueError('Original marker signature')
 count=r.uint('<')
 if not 0<count<=128:raise ValueError('Original marker count bound')
 rows=[];names=set()
 for _ in range(count):
  start=r.pos;n=r.uint('<')
  if not 0<n<=4096:raise ValueError('Original marker name bound')
  name=r.read(n).decode()
  if name in names or '\0'in name:raise ValueError('Original marker name duplicate/null')
  names.add(name);first,last,w,h,points=[r.uint('<')for _ in range(5)]
  if first<1 or last<first or last-first+1!=points or points>1000000 or not 0<w<=16384 or not 0<h<=16384:raise ValueError('Original marker range/dimensions')
  values=[]
  for _ in range(points):
   x,y=r.floating('<'),r.floating('<')
   if abs(x)>100 or abs(y)>100:raise ValueError('Original normalized marker bounds')
   values.append((x,y))
  rows.append({'name':name,'first':first,'last':last,'width':w,'height':h,'points':values,'source_range':[start,r.pos-start]})
 if r.pos!=len(r.data):raise ValueError('Unparsed original marker bytes')
 return rows
def export(a):
 if a.output.exists():raise ValueError('Fresh marker output directory required')
 m=json.loads(a.animations.read_text());packet=Path(m['packet']['path'])
 if identity(packet)['sha256']!=m['packet']['sha256']:raise ValueError('Original actor bank changed')
 data=[];byfile={};clips=[]
 for clip in m['clips']:
  d=clip['definition'];logical=(d.get('FrameDataFile')or'').replace('\\','/')
  # GameDataManager.GetAnimFrameDataSet b510a/b5157 -> b5349 returns
  # no dataset for empty or literal null; retain that absence explicitly.
  if logical in ('','null'):
   clips.append({'name':clip['name'],'dataset':0xffffffff,'frames':d['NumFrames'],'angles':d['NumAngles'],'own_scale':d.get('Scale',1),'offset_x':d.get('OffsetX',0),'offset_y':d.get('OffsetY',0),'partial_source_tracks':[],'native_no_dataset':True})
   continue
  # Matching GameDataManager::GetAnimFrameDataSet inserts _bin at FrameData/.
  if logical.count('FrameData/')!=1:raise ValueError('Unsupported original marker path')
  p=a.content/logical.replace('FrameData/','FrameData_bin/')
  if p not in byfile:
   rows=markers(p);byfile[p]=len(data);data.append({'source':identity(p),'tracks':rows})
  frames,angles=d['NumFrames'],d['NumAngles']
  if any(t['last']>frames*angles for t in data[byfile[p]]['tracks']):raise ValueError('Original marker exceeds selected movie')
  clips.append({'name':clip['name'],'dataset':byfile[p],'frames':frames,'angles':angles,'own_scale':d.get('Scale',1),'offset_x':d.get('OffsetX',0),'offset_y':d.get('OffsetY',0),'partial_source_tracks':[t['name']for t in data[byfile[p]]['tracks']if(t['first'],t['last'])!=(1,frames*angles)]})
 a.output.mkdir(parents=True);outpath=a.output/'actor-markers.bin'
 with outpath.open('xb')as out:
  out.write(b'HMR1'+bytes.fromhex(m['packet']['sha256'])+struct.pack('<II',len(data),len(clips)))
  for dataset in data:
   out.write(struct.pack('<I',len(dataset['tracks'])))
   for t in dataset['tracks']:
    string(out,t['name']);out.write(struct.pack('<5I',t['first'],t['last'],t['width'],t['height'],len(t['points'])))
    for point in t['points']:out.write(struct.pack('<2f',*point))
  for c in clips:
   string(out,c['name']);out.write(struct.pack('<3I3f',c['dataset'],c['frames'],c['angles'],c['own_scale'],c['offset_x'],c['offset_y']))
 report={'schema':'hades-original-actor-markers-v1','packet':identity(outpath),'bound_actor_bank':m['packet'],'actor_manifest':identity(a.animations),'clips':clips,'datasets':[{'source':d['source'],'tracks':[{k:v for k,v in t.items()if k!='points'}for t in d['tracks']]}for d in data],'points':sum(len(t['points'])for d in data for t in d['tracks']),'formula':'fullOriginalAngle*definition.NumFrames + originalFrame1; inclusive first/last. Rotate normalized point by actual GetDrawAngle; multiply width/2 and -height/2 by actual native Animation::GetScale; add actual bodyScale*animationOffset and Animation::GetLocation; Y -= actual attachedThingZ*sqrt(3)/2; add actual ShakeOffset XY. Result native Vector2, not invented ground XYZ.','defaults':'Source absent own Scale=1 and OffsetX/Y=0 from reviewed matching constructor only. Live owner/body/animation transitions remain caller supplied.','evidence':['.local/reports/native-effect-marker-data-manager.json','.local/reports/native-effect-marker-from-file-static.json','.local/reports/native-effect-book-marker-offset.json','.local/reports/native-effect-book-marker-location.json'],'limits':['Every original marker direction retained; visible actor bank still selects eight source directions.','Actual animation frame/facing/drawAngle/owner body scale/Z/animation scale/ShakeOffset required, not synthesized.','Native missing-marker/child fallback outside strict selected-track API.','No original native binary executed.']}
 (a.output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'packet':report['packet'],'datasets':len(data),'clips':len(clips),'points':report['points']}))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--content',type=Path,required=True);p.add_argument('--animations',type=Path,required=True);p.add_argument('--output',type=Path,required=True);export(p.parse_args())
