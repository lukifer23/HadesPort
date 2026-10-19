#!/usr/bin/env python3
"""Bind original positive parallax/ground origins to unchanged v5 scene."""
import argparse,collections,hashlib,json,math,struct
from pathlib import Path
from build_room_scene import identity

def export(scene_dir,output):
 if output.exists():raise ValueError('New output required')
 manifest_path=scene_dir/'scene-manifest.json';m=json.loads(manifest_path.read_text());scene=identity(scene_dir/'scene-affine.bin')
 if scene['sha256']!=m['scene']['sha256']:raise ValueError('Scene identity mismatch')
 records=[]
 for i,s in enumerate(m['sprites']):
  mat=s['material'];p=mat['parallax']
  if not math.isfinite(p)or p<=0 or p>16:raise ValueError('Screen-space/invalid parallax requires different original camera matrix')
  if mat['native_y_offset']!=0:raise ValueError('Nonzero nativeYOffset needs vertical mesh-origin join')
  # RenderMesh::Set passes original Drawable.mLocation.xyz. SetFlatOrVertical
  # writes vertex origin=(x,2*y) for flat or (x,2*y+2*YOffset) for vertical.
  # All selected YOffset are zero. Project at native Z=0, undoing only height
  # already applied to HDS3 anchors; Thing.Offset remains part of ground XY.
  origin=[s['draw_anchor'][0],s['draw_anchor'][1]+mat['offset_z']*math.sqrt(3)/2]
  origin,p=list(struct.unpack('<2f',struct.pack('<2f',*origin))),struct.unpack('<f',struct.pack('<f',p))[0]
  records.append({'sprite_index':i,'object_id':s['object_id'],'name':s['name'],'origin_ground_xy':origin,'parallax':p,'source_draw_anchor':s['draw_anchor'],'source_offset_z':mat['offset_z'],'source_group':s['source_layer']})
 output.mkdir(parents=True)
 with (output/'parallax.bin').open('xb')as f:
  f.write(b'HPP1'+bytes.fromhex(scene['sha256'])+struct.pack('<I',len(records)))
  for r in records:f.write(struct.pack('<3f',*r['origin_ground_xy'],r['parallax']))
 result={'schema':'hades-original-parallax-v1','scene':scene,'packet':identity(output/'parallax.bin'),'source_manifest':identity(manifest_path),'format':'HPP1,32raw sceneSHA256,u32spriteCount; each3float originGroundX,originGroundY,parallax','records':records,'affected_count':sum(r['parallax']!=1 for r in records),'factor_counts':dict(collections.Counter(str(r['parallax'])for r in records)),'transform':'For current orthographic SceneView center C: each projected world corner +=(originGroundXY-C)*(1/parallax-1). Sprite shape/size/tint/order unchanged.','source':'Original vertexHelpers.inc calcPosition plus exact matching RenderMesh::SetFlatOrVertical; references in native/analysis/parallax-contract.json.','limits':['Positive original factors only; screen-space alternate matrix route not implemented.','Current orthographic camera/fixed original world projection; no claim of full native camera state.','Static source origins; future actor movement must update origin as well as corners.','Original geometry sort graph remains incomplete.']}
 (output/'manifest.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'packet':result['packet'],'affected':result['affected_count'],'factors':result['factor_counts']}))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--scene',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();export(a.scene,a.output)
