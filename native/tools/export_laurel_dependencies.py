#!/usr/bin/env python3.13
"""Export bounded original Laurel animation dependency records, no RNG invented."""
import argparse,json,re,hashlib
from pathlib import Path
from build_room_scene import Sjson,identity
KEYS=['InheritFrom','ChildAnimation','CreateAnimation','VisualFx']
def export(a):
 if a.output.exists():raise ValueError('New output required')
 source=a.content/'Game/Animations/Fx.sjson';data=source.read_bytes();text=data.decode();defs={x['Name']:x for x in Sjson(text).parse()['Animations']};queue=['LaurelCindersSpawner'];rows=[];seen=set()
 def resolve(n,stack=()):
  if n in stack:raise ValueError('Cycle')
  d=defs[n];p=d.get('InheritFrom');return{**(resolve(p,stack+(n,))if p and p!='null'else{}),**d}
 while queue:
  name=queue.pop(0)
  if name in seen:continue
  seen.add(name);raw=defs[name];d=resolve(name);refs=[]
  for k in KEYS:
   v=raw.get(k)
   if v and v!='null':refs.append({'field':k,'name':v})
  for k in ['Random','CreateAnimations']:
   for v in raw.get(k,[]):
    if v.get('Name'):refs.append({'field':k,'name':v['Name']})
  for ref in refs:
   if ref['name']not in defs:raise ValueError('Missing dependency '+ref['name'])
   queue.append(ref['name'])
  needle=re.compile(rb'Name\s*=\s*"'+re.escape(name.encode())+rb'"');matches=list(needle.finditer(data))
  # Every name may also appear as a dependency; only own top-level declaration
  # immediately follows an object brace at line indentation6 in this source.
  declarations=[m for m in matches if data[data.rfind(b'\n',0,m.start())+1:m.start()]==b'      ']
  if len(declarations)!=1:raise ValueError('Ambiguous original record declaration')
  pos=declarations[0].start();rows.append({'name':name,'name_declaration_range':[pos,declarations[0].end()-pos],'source_line':data[:pos].count(b'\n')+1,'raw':raw,'resolved':d,'dependencies':refs})
 a.output.mkdir(parents=True)
 report={'schema':'hades-laurel-source-dependencies-v1','source':identity(source),'root':'LaurelCindersSpawner','records':rows,'count':len(rows),'source_behavior':{'root':'Invisible Constant loop,FX_Terrain group, distance-triggered VisualFx LaurelCinders with40..50distance interval and manager cap200.','attached_child':'LaurelCindersSpawnerAmbient invisible Constant loop; head_TRACKING marker binding; random0.45..0.5s VisualFx interval.','children':'Random selector resolves original LaurelBurnA..E and ambient variants; individual source behavior/texture paths retained.','entry_request':'Original hero data AttachedAnimationName=LaurelCindersSpawner, requested by actual hero setup before later light/properties.','not_static_drawable':'A successful invisible texture lookup alone cannot implement this animation: live distance/time spawners, child ownership, head marker, RNG/caps and original lifecycle needed.'},'limits':['Definition resolution/dependency data only; no live FX creation or RNG seed/order synthesized.','No claim of full native default/property/event semantics.','Random child animations are not baked into a synthetic static effect.']}
 (a.output/'laurel-dependencies.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'records':len(rows),'output':str(a.output)}))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--content',type=Path,required=True);p.add_argument('--output',type=Path,required=True);export(p.parse_args())
