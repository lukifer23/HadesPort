#!/usr/bin/env python3.13
"""Export native normalized atlas hulls, bound to immutable HDS3/HDA1."""
import argparse,collections,json,struct,math
from pathlib import Path
from hades_assets import Package
from build_room_scene import identity

def f32(v):return struct.unpack('<f',struct.pack('<f',v))[0]
def normalized(sprite):
 points=sprite['hull'];w,h=sprite['rect'][2:];hx,hy=w//2,h//2
 if not points:return [],0.
 if not hx or not hy or len(points)<3 or len(points)>128:raise ValueError('Hull outside native bounded scope')
 uv=[[min(1.,max(0.,f32(f32(x+hx)/(2*hx)))),min(1.,max(0.,f32(f32(y+hy)/(2*hy))))]for x,y in points]
 area=0.
 for i,(x,y)in enumerate(uv):
  px,py=uv[i-1];area=f32(area+f32(f32(py-y)*f32(x+px)))
 area=f32(area*.5)
 # Exact NormalizeHull source winding/area guard: full rects/tiny/invalid
 # polygons are cleared and rendered through the ordinary rectangle route.
 return (uv if -1.<area<f32(-.05)else []),area

def export(content,scene_dir,animation_dir,output):
 if output.exists():raise ValueError('New output required')
 m=json.loads((scene_dir/'scene-manifest.json').read_text());a=json.loads((animation_dir/'animation-manifest.json').read_text());clips=json.loads((scene_dir/'animation-clips.json').read_text());scene=identity(scene_dir/'scene-affine.bin');animation=identity(animation_dir/'animations.bin')
 if scene['sha256']!=m['scene']['sha256']or a['scene_sha256']!=scene['sha256']:raise ValueError('Source scene binding mismatch')
 index=collections.defaultdict(list);sources=[]
 for package in ['Tartarus','Fx']:
  p=content/'Win/Packages'/f'{package}.pkg_manifest';sources.append(identity(p))
  for entry in Package(p).entries():
   for s in entry.get('sprites',[]):index[(entry['name'],s['name'],tuple(s['rect']))].append(s)
 textures=[]
 for t in m['textures']:
  candidates=index[(t['atlas'],t['graphic'],tuple(t['atlas_rect']))]
  if not candidates:raise ValueError('Source atlas join failed')
  polygons=[normalized(s)for s in candidates]
  if any(p!=polygons[0]for p in polygons):raise ValueError('Ambiguous source hull')
  uv,area=polygons[0];textures.append({'texture_index':t['index'],'graphic':t['graphic'],'atlas':t['atlas'],'rect':t['atlas_rect'],'source_hull':candidates[0]['hull'],'normalized_hull':uv,'signed_area':area})
 animated=[]
 for ac in a['clips']:
  source=clips[ac['name']]['frames'];frames=[]
  if len(source)!=ac['frame_count']:raise ValueError('Animation frame count mismatch')
  for original,actual in zip(source,ac['frames']):
   if original['frame']!=actual['original_frame']or original['sha256']!=actual['sha256']:raise ValueError('Animation frame identity mismatch')
   uv,area=normalized(original['sprite']);frames.append({'original_frame':original['frame'],'source_hull':original['sprite']['hull'],'normalized_hull':uv,'signed_area':area})
  animated.append({'clip_index':ac['index'],'name':ac['name'],'frames':frames})
 output.mkdir(parents=True)
 def polygon(file,points):
  file.write(struct.pack('<I',len(points)))
  for point in points:file.write(struct.pack('<2f',*point))
 with(output/'hulls.bin').open('xb')as file:
  file.write(b'HHL1'+bytes.fromhex(scene['sha256'])+bytes.fromhex(animation['sha256'])+struct.pack('<II',len(textures),len(animated)))
  for t in textures:polygon(file,t['normalized_hull'])
  for clip in animated:
   file.write(struct.pack('<I',len(clip['frames'])))
   for frame in clip['frames']:polygon(file,frame['normalized_hull'])
 report={'schema':'hades-original-atlas-hulls-v1','scene':scene,'animation':animation,'packet':identity(output/'hulls.bin'),'sources':sources+[identity(scene_dir/'scene-manifest.json'),identity(scene_dir/'animation-clips.json'),identity(animation_dir/'animation-manifest.json')],'format':'HHL1+32raw sceneSHA+32raw animationsSHA+u32textureCount+u32clipCount; eachtexture polygon; eachclip u32frameCount,frame polygons; polygon=u32pointCount+pointCount*(2floatUV)','textures':textures,'clips':animated,'counts':{'static_textures':len(textures),'static_hulls':sum(bool(t['normalized_hull'])for t in textures),'clips':len(animated),'frames':sum(len(c['frames'])for c in animated),'frame_hulls':sum(bool(f['normalized_hull'])for c in animated for f in c['frames'])},'source_normalization':'Exact centered half-dimension float32 normalization/clamp and signed-area guard; public REA ev_f90b7ea0542367faf01b136da0836304eea8bd79eb7d29b8400554c27167256f.','limits':['Original geometry order remains source group/SortIndex/Y until native scene graph reconstruction.','Positive parallax/affine HDS geometry handled separately.','Static actor geometry only; dynamic native mesh rebuilds remain future work.','Original scene/animation/texture bytes unchanged.']}
 (output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'packet':report['packet'],'counts':report['counts']}))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--content',type=Path,required=True);p.add_argument('--scene',type=Path,required=True);p.add_argument('--animations',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();export(a.content,a.scene,a.animations,a.output)
