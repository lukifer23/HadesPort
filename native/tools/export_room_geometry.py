#!/usr/bin/env python3.13
"""Export original source-bound spawn markers and collision candidates."""
import argparse,json,math
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('scene_manifest',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();m=json.loads(a.scene_manifest.read_text());anchors=[];polys=[]
for e in m['all_original_objects']:
 o=e['object'];t=e['obstacle_thing']
 if o['Name']in('_PlayerUnit','HeroStart','HeroEnd','HeroExit','CameraIntroStart','CameraIntroEnd','CameraClamp'):
  anchors.append({'name':o['Name'],'id':o['Id'],'location':o['Location'],'source_range':o['source_range']})
 if not o['DoCreate']or not o['Active']or not o['Collision']or t.get('StopsUnits')is False:continue
 pts=o['Points']or[[v['X'],v['Y']]for v in t.get('Points',[])]
 if len(pts)<3:continue
 angle=math.radians(o['Angle']);c=math.cos(angle);s=math.sin(angle);world=[]
 for x,y in pts:
  x*=o['Scale']*(-1 if o['FlipHorizontal']else 1);y*=o['Scale']*(-1 if o['FlipVertical']else 1)
  world.append([o['Location']['X']+x*c-y*s,o['Location']['Y']+x*s+y*c])
 polys.append({'id':o['Id'],'name':o['Name'],'original_points':pts,'points_source':'map instance override'if o['Points']else'obstacle Thing.Points with inherited definitions','location':o['Location'],'scale':o['Scale'],'angle_degrees':o['Angle'],'flip_horizontal':o['FlipHorizontal'],'flip_vertical':o['FlipVertical'],'skew_angle':o['SkewAngle'],'skew_scale':o['SkewScale'],'world_points_provisional':world,'world_transform_exact_for_unskewed_zeroangle':o['Angle']==0 and o['SkewScale']==1,'source_range':o['source_range']})
r={'room':m['room'],'source_files':[s for s in m['sources']if '/Maps/'in s['path']or'/Obstacles/'in s['path']],'anchors':anchors,'collision_candidates':polys,'count':len(polys),'limitations':['Collision candidates honor actual instance Collision/Active/DoCreate and explicit StopsUnits=false. Default StopsUnits semantics and collision runtime flags remain unverified.','World polygon transform is provisional for rotations/skew; original raw points and all transform fields are retained. No collision gameplay validation performed.']}
with a.output.open('x')as f:json.dump(r,f,indent=2);f.write('\n')
print({'output':str(a.output),'anchors':len(anchors),'collision_candidates':len(polys)})
