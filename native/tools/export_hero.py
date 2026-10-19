#!/usr/bin/env python3.13
"""Decode bounded original Zagreus clips with an independently built Bink2 tool.

HDH1: magic, uint32 frames,width,height,angles; float fps,worldScale,offsetY;
then exact frameCount*angles*width*height RGBA8 frame bytes.
"""
import argparse,json,struct,subprocess,hashlib,os
from pathlib import Path
from PIL import Image
from hades_assets import Reader
from build_room_scene import Sjson,identity
p=argparse.ArgumentParser();p.add_argument('--content',type=Path,required=True);p.add_argument('--decoder',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--all-angles',action='store_true');p.add_argument('--directions',type=int,default=1);p.add_argument('--idle-frames',type=int);a=p.parse_args()
if a.output.exists():raise ValueError('Output must be new')
a.output.mkdir(parents=True);animation_file=a.content/'Game/Animations/CharacterAnimationsHero.sjson';defs=Sjson(animation_file.read_text()).parse()['Animations'];animations={x['Name']:x for x in defs};unitfile=a.content/'Game/Units/PlayerUnits.sjson';units=Sjson(unitfile.read_text()).parse()['Units'];unit=next(x for x in units if x['Name']=='_PlayerUnit');unit_scale=unit['Thing']['Scale'];reports=[]
for name in ['ZagreusIdle','ZagreusRun']:
 anim=animations[name];movie=a.content/'Movies'/(anim['VideoTexture']+'.bik');atlas=movie.with_suffix('.bik_atlas');r=Reader(atlas.read_bytes());ver=r.integer();atlas_name=r.smallstring();w=r.integer();h=r.integer();orig=[r.integer(),r.integer()];atlas_scale=r.floating()
 if ver!=3 or r.pos!=len(r.data)or not(0<w<2048 and 0<h<2048):raise ValueError('Unsupported hero atlas')
 header=movie.open('rb').read(44)
 if header[:4]!=b'KB2j':raise ValueError('Unsupported hero movie')
 total_frames=int.from_bytes(header[8:12],'little');fw=int.from_bytes(header[20:24],'little');fh=int.from_bytes(header[24:28],'little');fps_num=int.from_bytes(header[28:32],'little');fps_den=int.from_bytes(header[32:36],'little')
 if(fw,fh)!=(w,h)or total_frames!=anim['NumFrames']*anim['NumAngles']:raise ValueError('Hero SJSON/movie count mismatch')
 angles=anim['NumAngles'] if a.all_angles else a.directions
 if not 1<=angles<=anim['NumAngles']:raise ValueError('Invalid angle count')
 frames=min(anim['NumFrames'],a.idle_frames or anim['NumFrames']) if name=='ZagreusIdle' else anim['NumFrames']
 angle_indices=[i*anim['NumAngles']//angles for i in range(angles)];selected=[angle*anim['NumFrames']+f for angle in angle_indices for f in range(frames)];selected_set=set(selected);count=max(selected)+1;expected=len(selected)*w*h*4;fps=anim.get('PlaySpeed',fps_num/fps_den);worldscale=unit_scale*anim.get('Scale',1)/atlas_scale;offsetY=anim.get('OffsetY',0)*unit_scale
 # OffsetY belongs to animation space, hence unit scaling. Exact engine pivot
 # semantics remain pending verification against runtime/reference rendering.
 target=a.output/(name+'.bin');cmd=[str(a.decoder.resolve()),'-v','error','-threads','1','-i',str(movie.resolve()),'-frames:v',str(count),'-f','rawvideo','-pix_fmt','rgba','-y','/dev/fd/1']
 with(a.output/(name+'.decode.log')).open('xb')as log,target.open('xb')as out:
  out.write(b'HDH1'+struct.pack('<4I3f',frames,w,h,angles,fps,worldscale,offsetY));proc=subprocess.Popen(cmd,stdout=subprocess.PIPE,stderr=log,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
  n=0;first=bytearray();framebytes=w*h*4
  for movie_frame in range(count):
   b=bytearray()
   while len(b)<framebytes:
    part=proc.stdout.read(framebytes-len(b))
    if not part:raise ValueError('Truncated hero frame '+str(movie_frame))
    b.extend(part)
   if movie_frame in selected_set:
    n+=len(b)
    if n>expected:proc.kill();raise ValueError('Hero output bound exceeded')
    if not first:first=b
    out.write(b)
  if proc.stdout.read(1):proc.kill();raise ValueError('Unexpected extra hero frames')
  if proc.wait()!=0 or n!=expected:raise ValueError('Incomplete hero decode '+name)
 Image.frombytes('RGBA',(w,h),bytes(first)).save(a.output/(name+'-frame0.png'))
 reports.append({'name':name,'sources':[identity(movie),identity(atlas),identity(animation_file),identity(unitfile)],'animation_definition':anim,'movie_frames':total_frames,'original_angles':anim['NumAngles'],'exported_angles':angles,'original_angle_indices':angle_indices,'exported_frames_per_angle':frames,'width':w,'height':h,'atlas_original_canvas':orig,'atlas_scale':atlas_scale,'unit_scale':unit_scale,'world_scale':worldscale,'offset_y':offsetY,'play_fps':fps,'fps_source':'animationPlaySpeed'if'PlaySpeed'in anim else'movieheader fallback, nativeengine defaultnotverified','original_sjson_offset_y':anim.get('OffsetY',0),'frame_order':'provisional angle-major; actualmovie sequentialframespreserved','decoder':identity(a.decoder),'command':cmd,'output':identity(target),'rgba_bytes':n})
(a.output/'hero-manifest.json').write_text(json.dumps({'format':'HDH1:magic,uint32frames,w,h,angles;floatfps,worldScale,offsetY;sequentialRGBA8frames','clips':reports,'limitations':['Original idle/run visual assets only; no original movement/combat implementation.','Angle-major frame ordering and animation offset projection are provisional until reference validated.','Animation scalar multiplied byunitScale anddivided byatlasScale; rawsourcefields retained.']},indent=2)+'\n')
print(json.dumps([{'name':r['name'],'frames':r['exported_frames_per_angle'],'angles':r['exported_angles'],'bytes':r['output']['bytes'],'world_scale':r['world_scale'],'offset_y':r['offset_y']}for r in reports]))
