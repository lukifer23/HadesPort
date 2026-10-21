#!/usr/bin/env python3.13
"""Export a seekable HDA1 companion bound to one unchanged HDS3 SHA-256.

Original RGBA frames are shared by clip; frame-specific world quads are small
and precomputed per scene instance. Does not infer original RNG scheduling.
"""
import argparse,hashlib,json,math,struct
from pathlib import Path
from build_graphics_scene import atlas_center,project_quad,CAMERA_Z_SCALE

def frame_quad(sprite,rec,clip,frame):
 ob=rec['object'];definition=clip['definition'];atlas=frame['sprite'];origin=[definition.get('OriginX',atlas['original'][0]/2),definition.get('OriginY',atlas['original'][1]/2)];ox,oy=atlas_center(atlas,origin,ob['FlipHorizontal'],ob['FlipVertical']);scale=ob['Scale']*rec['obstacle_thing'].get('Scale',1)*definition.get('Scale',1);offset=rec['obstacle_thing'].get('Offset',{});ox=(ox+offset.get('X',0)+definition.get('OffsetX',0))*scale;oy=(oy+offset.get('Y',0)+definition.get('OffsetY',0))*scale;angle=math.radians(ob['Angle']);c,ss=math.cos(angle),math.sin(angle);w,h=frame['width']*atlas['scale_ratio'][0]*scale,frame['height']*atlas['scale_ratio'][1]*scale
 if sprite.get('native_transform_profile')=='v5':
  # GetDrawLocation 0x180297d60: draw Offset x attached Body scale is part of
  # the v5 draw anchor (build_graphics_scene records it); never re-derived here.
  body=ob['Scale']*rec['obstacle_thing'].get('Scale',1);expected=[definition.get('OffsetX',0)*body,definition.get('OffsetY',0)*body]
  if (expected[0] or expected[1]) and sprite.get('animation_draw_offset')!=expected:raise ValueError('Explicit Animation.Offset absent from the v5 draw anchor')
  ox,oy=atlas_center(atlas,origin,ob['FlipHorizontal'],ob['FlipVertical']);ox*=scale;oy*=scale;anchor=sprite['draw_anchor'];draw=[0,anchor[0]+ox*c-oy*ss,anchor[1]+ox*ss+oy*c,w,h,0,0,1,1,angle,1]
  return project_quad(draw,ob,definition.get('PostRotateScaleX',1),definition.get('PostRotateScaleY',1),anchor=anchor,mesh_type=rec['obstacle_thing'].get('MeshType','Flat'),full_stretch=bool(sprite['material']['squash_stretch_required']))
 draw=[0,ob['Location']['X']+ox*c-oy*ss,ob['Location']['Y']+ox*ss+oy*c-ob['OffsetZ']*CAMERA_Z_SCALE,w,h,0,0,1,1,angle,1]
 return project_quad(draw,ob,definition.get('PostRotateScaleX',1),definition.get('PostRotateScaleY',1))

def build(scene,out):
 if out.exists():raise ValueError('Output must be new')
 m=json.loads((scene/'scene-manifest.json').read_text());clips=json.loads((scene/'animation-clips.json').read_text());original={r['object']['Id']:r for r in m['all_original_objects']};scene_path=scene/'scene-affine.bin';scene_sha=hashlib.sha256(scene_path.read_bytes()).digest();names=list(clips);binding=[(i,s)for i,s in enumerate(m['sprites'])if s.get('animation')];out.mkdir(parents=True);records=[];bound=[]
 with(out/'animations.bin').open('xb')as f:
  f.write(b'HDA1'+struct.pack('<II',len(clips),len(binding))+scene_sha)
  for ci,name in enumerate(names):
   clip=clips[name];definition=clip['definition'];frames=clip['frames'];encoded=name.encode('utf-8')
   if clip['missing_frames']or not 0<len(frames)<=4096 or len(encoded)>4096:raise ValueError('Clip bounds/incomplete')
   if 'NumLoopFrames'in definition or 'LoopStartFrame'in definition:raise ValueError('Unmapped original loop range')
   if definition['StartFrame']!=frames[0]['frame']or definition['EndFrame']!=frames[-1]['frame']:raise ValueError('Original clip range mismatch')
   if len(frames)>1:
    lo=definition.get('PlaySpeed',definition.get('RandomPlaySpeedMin'));hi=definition.get('PlaySpeed',definition.get('RandomPlaySpeedMax'))
    if lo is None or hi is None or not 0<lo<=hi<=1000:raise ValueError('Missing/invalid original timing')
   else:lo=hi=0.
   flags=sum(bit for key,bit in [('Loop',1),('LoopFramesOnly',2),('RandomStartFrame',4),('ReRandomizeOnLoop',8)]if definition.get(key,False));loop_start=0
   f.write(struct.pack('<I',len(encoded))+encoded+struct.pack('<IffII',len(frames),lo,hi,flags,loop_start));r={'index':ci,'name':name,'frame_count':len(frames),'fps_min':lo,'fps_max':hi,'flags':flags,'loop_start_index':loop_start,'definition':definition,'frames':[]}
   r['unapplied_procedural_fields']={k:v for k,v in definition.items()if any(t in k for t in('PingPong','StartAngle','EndAngle','Ease','StartOffset','EndOffset'))}
   for frame in frames:
    rgba=(scene/frame['rgba_path']).read_bytes();w,h=frame['width'],frame['height']
    if not 0<w*h<=16*1024*1024 or len(rgba)!=w*h*4 or hashlib.sha256(rgba).hexdigest()!=frame['sha256']:raise ValueError('Frame pixels/hash invalid')
    f.write(struct.pack('<III',frame['frame'],w,h));at=f.tell();f.write(rgba);r['frames'].append({'original_frame':frame['frame'],'width':w,'height':h,'rgba_offset':at,'rgba_bytes':len(rgba),'sha256':frame['sha256'],'original_atlas':frame['atlas'],'original_atlas_sprite':frame['sprite']})
   records.append(r)
  for index,sprite in binding:
   ci=names.index(sprite['animation']);clip=clips[sprite['animation']];f.write(struct.pack('<III',index,ci,len(clip['frames'])));at=f.tell()
   for fi,frame in enumerate(clip['frames']):
    # Frame zero is byte-for-byte the existing HDS3 geometry, including its
    # explicitly provisional choices. Later frames use their own atlas pivots.
    quad=sprite['draw_quad']if fi==0 else frame_quad(sprite,original[sprite['object_id']],clip,frame)
    f.write(struct.pack('<8f',*(v for p in quad for v in p)))
   bound.append({'sprite_index':index,'object_id':sprite['object_id'],'clip_index':ci,'frame_count':len(clip['frames']),'quad_offset':at})
 manifest={'format':'HDA1','scene':str(scene_path.resolve()),'scene_sha256':scene_sha.hex(),'header_bytes':44,'clips':records,'bindings':bound,'sources':m['sources'],'playback_profile':'Debug deterministic: phase0,fpsMin; original flags and fps ranges preserved. Original RNG schedule is not reconstructed.','limits':['Geometry uses exact transform profile of bound HDS3, including documented remaining limitations.','Two ghost-hands clips have unimplemented procedural angle/vertical bob interpolation; exact source fields retained.','Source default origins/shader selection retain HDS3 limitations.','RGBA is original straight alpha; each binding retains scene UV/tint/material and render order.']}
 (out/'animation-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');return validate(out)

def validate(out):
 m=json.loads((out/'animation-manifest.json').read_text());path=out/'animations.bin';size=path.stat().st_size;count=0;rgba_total=0
 scene_data=Path(m['scene']).read_bytes()
 if hashlib.sha256(scene_data).hexdigest()!=m['scene_sha256']or scene_data[:4]!=b'HDS3':raise ValueError('Actual bound scene identity mismatch')
 textures,sprite_count=struct.unpack_from('<II',scene_data,4);scene_records=12
 for _ in range(textures):
  w,h=struct.unpack_from('<II',scene_data,scene_records);scene_records+=8+w*h*4
 if scene_records+sprite_count*76!=len(scene_data):raise ValueError('Bound HDS3 length mismatch')
 with path.open('rb')as f:
  def read(n):
   data=f.read(n)
   if len(data)!=n:raise ValueError('Truncated HDA1')
   return data
  if read(4)!=b'HDA1':raise ValueError('Magic')
  cc,bc=struct.unpack('<II',read(8))
  if cc!=len(m['clips'])or bc!=len(m['bindings'])or read(32).hex()!=m['scene_sha256']:raise ValueError('Header mismatch')
  for ci,clip in enumerate(m['clips']):
   n,=struct.unpack('<I',read(4))
   if n>4096 or read(n).decode('utf-8')!=clip['name']:raise ValueError('Name mismatch')
   fc,lo,hi,flags,loop=struct.unpack('<IffII',read(20))
   if fc!=clip['frame_count']or flags&~15 or not 0<=loop<fc or not all(math.isfinite(v)for v in(lo,hi))or lo>hi:raise ValueError('Clip fields')
   for frame in clip['frames']:
    number,w,h=struct.unpack('<III',read(12))
    if(number,w,h)!=(frame['original_frame'],frame['width'],frame['height'])or not 0<w*h<=16*1024*1024 or f.tell()!=frame['rgba_offset']:raise ValueError('Frame fields')
    data=read(w*h*4)
    if hashlib.sha256(data).hexdigest()!=frame['sha256']:raise ValueError('Frame checksum')
    count+=1;rgba_total+=len(data)
  for b in m['bindings']:
   index,ci,fc=struct.unpack('<III',read(12))
   if(index,ci,fc)!=(b['sprite_index'],b['clip_index'],b['frame_count'])or fc!=m['clips'][ci]['frame_count']or f.tell()!=b['quad_offset']:raise ValueError('Binding fields')
   if index>=sprite_count:raise ValueError('Binding scene index outside bound HDS3')
   for fi in range(fc):
    raw=read(32)
    if not all(math.isfinite(v)for v in struct.unpack('<8f',raw)):raise ValueError('Nonfinite quad')
    if fi==0 and raw!=scene_data[scene_records+index*76+4:scene_records+index*76+36]:raise ValueError('First-frame quad differs from bound scene')
  if f.tell()!=size:raise ValueError('Trailing bytes')
 report={'format':'HDA1','clips':cc,'bindings':bc,'frames':count,'rgba_bytes':rgba_total,'bytes':size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'bound_scene_sha256':m['scene_sha256'],'all_frame_hashes_valid':True,'all_binding_first_frame_quads_equal_actual_hds3':True,'actual_scene_sha_valid':True,'exact_file_length':True}
 (out/'animation-validation.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--scene',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--validate',action='store_true');a=p.parse_args();print(json.dumps(validate(a.output)if a.validate else build(a.scene,a.output)))
