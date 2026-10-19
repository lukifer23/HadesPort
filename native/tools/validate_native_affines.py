#!/usr/bin/env python3
"""Check actual non-axis room quads against native 3D mesh instruction reduction.

Independent 3D matrix path follows RenderMesh::SetFlatOrVertical and
SquashAndStretch, rather than invoking exporter's 2D quad function.
"""
import argparse,json,math
from pathlib import Path

def validate(directory):
 m=json.loads((directory/'scene-manifest.json').read_text());objects={r['object']['Id']:r for r in m['all_original_objects']};sin60=math.sqrt(3)/2;rows=[]
 def camera(p):return[p[0],.5*p[1]+sin60*p[2],-sin60*p[1]+.5*p[2]]
 def inverse_camera(p):return[p[0],.5*p[1]-sin60*p[2],sin60*p[1]+.5*p[2]]
 for i,s in enumerate(m['sprites']):
  ob=objects[s['object_id']]['object'];sk=math.radians(ob['SkewAngle'])
  if not s['material']['squash_stretch_required']or abs(math.sin(sk))<1e-6:continue
  d=s['draw'];anchor=s['draw_anchor'];angle=d[9];c,ss=math.cos(angle),math.sin(angle);center_x=(d[1]-anchor[0])*c+(d[2]-anchor[1])*ss;center_y=-(d[1]-anchor[0])*ss+(d[2]-anchor[1])*c;vertical=s['material']['mesh_type']=='Vertical';quads=[]
  if s['material']['native_y_offset']!=0:raise ValueError('This bounded derivation covers actual zero-Animation.Offset cases')
  for x,y in((-d[3]/2,-d[4]/2),(d[3]/2,-d[4]/2),(-d[3]/2,d[4]/2),(d[3]/2,d[4]/2)):
   lx,ly=center_x+x,center_y+y
   p=[lx,0.,ly/sin60]if vertical else[lx,2*ly,0.]
   # Exact native scalar sequence (with original animationYOffset=0):
   term=(p[2]if vertical else .5*p[1])*(ob['SkewScale']-1)
   term=math.cos(sk)*p[0]*(ob['SkewScale']-1)-math.sin(sk)*term
   p[0]+=math.cos(sk)*term;p[2]-=math.sin(sk)*term
   v=camera(p);v=[c*v[0]-ss*v[1],ss*v[0]+c*v[1],v[2]];v[0]*=s['material']['post_rotate_scale'][0];v[1]*=s['material']['post_rotate_scale'][1]
   # Native transforms back into the tilted plane before view projection.
   v=camera(inverse_camera(v));quads.append([anchor[0]+v[0],anchor[1]+v[1]])
  error=max(abs(a-b)for p,q in zip(quads,s['draw_quad'])for a,b in zip(p,q))
  if error>1.e-8:raise ValueError(f'Native affine disagreement object{ob["Id"]}: {error}')
  rows.append({'sprite_index':i,'object_id':ob['Id'],'mesh_type':s['material']['mesh_type'],'skew_angle':ob['SkewAngle'],'skew_scale':ob['SkewScale'],'native_y_offset':0,'max_world_error':error,'native_3d_quads':quads})
 return {'method':'Original flat/vertical3D vertex construction ->native scalar squash/stretch ->camera rotation ->sprite rotation/post-scale ->inverse camera ->view projection','actual_non_axis_placements':len(rows),'maximum_error':max((r['max_world_error']for r in rows),default=0),'records':rows,'limits':'Analytic equivalence for recovered affine math. Default atlas origin, procedural animation shifts and official renderer pixel comparison remain outside this check.'}
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('directory',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();r=validate(a.directory);a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({k:v for k,v in r.items()if k!='records'}))
