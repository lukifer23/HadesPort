#!/usr/bin/env python3.13
"""Export bounded original Slide schedules and independently decoded Bink frames.
No game/installer code is executed. HAA1 stores zlib-compressed cropped RGBA;
source movies, definitions and SGA event data remain read-only.
"""
import argparse,collections,hashlib,json,math,os,struct,subprocess,time,zlib
from pathlib import Path
from PIL import Image
from build_room_scene import Sjson,identity
from hades_assets import Reader
NAMES=['ZagreusIdle','ZagreusRun','ZagreusSwordRun','ZagreusSwordDashAttack','ZagreusSwordParryCharge','ZagreusSwordParryFire','ZagreusSwordCharge1','ZagreusSwordAttack1','ZagreusSwordCharge2','ZagreusSwordAttack2','ZagreusSwordCharge3','ZagreusSwordAttack3','ZagreusDashNoCollide','ZagreusDashCollide','ZagreusRangedWeapon_Charge','ZagreusRangedWeapon_Fire','ZagreusHeroLanding_Start','ZagreusHeroLanding_Air','ZagreusHeroLanding_Fire','ZagreusHeroLanding_LandingHold','ZagreusHeroLanding_ReturnToIdle']
FLAGS=['Loop','HoldLastFrame','AllowRestart','CancelOnOwnerMove','DisableOwnerWeapons','DisableOwnerRotation','DisableOwnerInput','DisableOwnerManualInteract','OwnerHasNoCollision']
def string(f,s):
 b=(s or '').encode();f.write(struct.pack('<I',len(b)));f.write(b)
def merged(defs,name,stack=()):
 if name in stack:raise ValueError('Animation inheritance cycle')
 raw=defs[name];p=raw.get('InheritFrom');d=merged(defs,p,stack+(name,))[0] if p and p!='null'else {};d={**d,**raw};return d,list(stack)+( [p]if p else [])
def export(a):
 if a.output.exists():raise ValueError('New output required')
 a.output.mkdir(parents=True);source=getattr(a,'source',None)or a.content/'Game/Animations/CharacterAnimationsHero.sjson';source_files=getattr(a,'sources',None)or[source];defs={x['Name']:x for p in source_files for x in Sjson(p.read_text()).parse()['Animations']};unitfile=a.content/'Game/Units/PlayerUnits.sjson';unit=next(x for x in Sjson(unitfile.read_text()).parse()['Units']if x['Name']=='_PlayerUnit');unit_scale=1.0 if getattr(a,'unit_scale',None)is None else a.unit_scale;names=list(getattr(a,'names',None)or NAMES);clips=[];movies={};sources=[*[identity(p)for p in source_files],identity(unitfile)]
 for n in names:
  d,_=merged(defs,n);d.setdefault('NumAngles',1);chain=d.get('ChainTo');
  if chain and chain!='null'and chain!='ZagreusIdle'and chain not in names:names.append(chain)
  if d.get('Type')not in ('Slide','Book')or not d.get('VideoTexture'):raise ValueError('Nonmovie action '+n)
  if d.get('RandomPlaySpeedMin',0)or d.get('RandomPlaySpeedMax',0)or d.get('RandomStartFrame',False):raise ValueError('Nondeterministic action timing '+n)
  movie=d['VideoTexture'];path=(getattr(a,'movie_directory',None)or a.content/'Movies')/(movie+'.bik');atlas=path.with_suffix('.bik_atlas')
  if movie not in movies:
   r=Reader(atlas.read_bytes());version=r.integer();aname=r.smallstring();w,h=r.integer(),r.integer();ow,oh=r.integer(),r.integer();atlas_scale=r.floating()
   if version!=3 or aname!=movie or r.pos!=len(r.data)or not 0<w<=2048 or not 0<h<=2048 or not 0<atlas_scale<=8:raise ValueError('Unsupported original Bink atlas')
   with path.open('rb')as f:header=f.read(44)
   total,fw,fh=struct.unpack_from('<I',header,8)[0],struct.unpack_from('<I',header,20)[0],struct.unpack_from('<I',header,24)[0]
   if header[:4]!=b'KB2j'or(fw,fh)!=(w,h)or (total!=d['NumFrames']*d['NumAngles']and not(getattr(a,'allow_unused_movie_tail',False)and d['NumAngles']==1 and d['NumFrames']<=total<=d['NumFrames']+1)):raise ValueError('Original movie identity/dimension mismatch')
   movie_info={'name':movie,'path':path,'width':w,'height':h,'original_width':ow,'original_height':oh,'atlas_scale':atlas_scale,'num_frames':d['NumFrames'],'actual_movie_frames':total,'num_angles':d['NumAngles'],'selected':set(),'frame_ids':{}};movies[movie]=movie_info;sources.extend([identity(path),identity(atlas)])
  info=movies[movie]
  if(info['num_frames'],info['num_angles'])!=(d['NumFrames'],d['NumAngles']):raise ValueError('Shared movie definitions disagree')
  slides=d.get('Slides',[])
  if d['Type']=='Book':
   events={x['Frame']:x for x in slides}
   slides=[{'DurationFrames':1,**{k:v for k,v in events.get(i,{}).items()if k not in ('Frame','DurationFrames')}}for i in range(d.get('StartFrame',1),d.get('EndFrame',d['NumFrames'])+1)]
  if len(slides)!=d['NumFrames']:raise ValueError('Slides/movie frame count mismatch')
  if any('DurationFrames'not in s for s in slides):raise ValueError('Native absent slide duration not recovered')
  seq=[]
  for frame,s in enumerate(slides,1):
   duration=s['DurationFrames']
   if not isinstance(duration,int)or not 0<=duration<=100000:raise ValueError('Duration outside bounds')
   if duration:
    if set(s)-{'DurationFrames','Sound','FootstepSound','FootstepFxR','FootstepFxL','VisualFx','RandomDurationFramesMin','RandomDurationFramesMax'}:raise ValueError('Unsupported per-slide transform')
    seq.append({'original_frame':frame,'ticks':duration,'events':{k:v for k,v in s.items()if k!='DurationFrames'}})
  if not seq:raise ValueError('No visible slides')
  angles=min(a.directions,d['NumAngles']);indices=[i*d['NumAngles']//angles for i in range(angles)]
  for angle in indices:
   for s in seq:info['selected'].add(angle*d['NumFrames']+s['original_frame']-1)
  frame_data=d.get('FrameDataFile')
  if frame_data:
   fp=a.content/Path(frame_data.replace('\\','/'))
   if fp.exists():sources.append(identity(fp))
  # Exact AnimationData ctor zero template initializes bool fields before
  # explicit writes. Only these reviewed defaults are carried as resolved here.
  resolved={k:bool(d.get(k,False))for k in FLAGS};flags=sum(int(resolved[k])<<i for i,k in enumerate(FLAGS))|(512 if d['Type']=='Book'else 0)
  clips.append({'name':n,'movie':movie,'definition':d,'source_raw':defs[n],'source_sequence':seq,'angle_indices':indices,'original_angles':d['NumAngles'],'flags':flags,'resolved_flags':resolved,'unit_scale':unit_scale,'world_scale':unit_scale*d.get('Scale',1)/info['atlas_scale'],'offset_x':unit_scale*d.get('OffsetX',0),'offset_y':unit_scale*d.get('OffsetY',0),'chain':chain if chain and chain!='null'else'','move_animation':d.get('MoveAnimation',''),'seconds_per_tick':1/(d.get('PlaySpeed',0)or 60)if d['Type']=='Book'else 1/60,'time_modifier_fraction':d.get('TimeModifierFraction',None)})
 frames=[];start=time.monotonic();payload=a.output/'frames.tmp';max_raw=0;raw_bytes=0;compressed_bytes=0
 with payload.open('xb')as data:
  for movie,info in movies.items():
   count=max(info['selected'])+1;w,h=info['width'],info['height'];size=w*h*4;command=[str(a.decoder.resolve()),'-v','error','-threads','1','-i',str(info['path'].resolve()),'-frames:v',str(count),'-f','rawvideo','-pix_fmt','rgba','-y','/dev/fd/1'];log=a.output/(movie+'.decode.log')
   with log.open('xb')as err:
    proc=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=err,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
    try:
     for number in range(count):
      pixels=bytearray()
      while len(pixels)<size:
       block=proc.stdout.read(size-len(pixels))
       if not block:raise ValueError('Truncated decode '+movie+':'+str(number))
       pixels.extend(block)
      if number not in info['selected']:continue
      image=Image.frombytes('RGBA',(w,h),bytes(pixels));box=image.getchannel('A').getbbox()
      if box:box=(max(0,box[0]-1),max(0,box[1]-1),min(w,box[2]+1),min(h,box[3]+1))
      else:box=(0,0,1,1)
      crop=image.crop(box);raw=crop.tobytes();packed=zlib.compress(raw,6);offset=data.tell();data.write(packed);idx=len(frames);info['frame_ids'][number]=idx
      original_origin=[info['original_width']/2-math.trunc((info['original_width']-w)/2),info['original_height']/2-math.trunc((info['original_height']-h)/2)]
      rec={'index':idx,'movie':movie,'movie_frame0':number,'original_angle':number//info['num_frames'],'original_frame1':number%info['num_frames']+1,'canvas':[w,h],'crop':[box[0],box[1],crop.width,crop.height],'origin':original_origin,'raw_bytes':len(raw),'compressed_bytes':len(packed),'crc32':zlib.crc32(raw),'payload_offset':offset,'source_rgba_sha256':hashlib.sha256(pixels).hexdigest(),'crop_rgba_sha256':hashlib.sha256(raw).hexdigest()};frames.append(rec);max_raw=max(max_raw,len(raw));raw_bytes+=len(raw);compressed_bytes+=len(packed)
      if idx==0 or number==min(info['selected']):crop.save(a.output/(movie+'-first.png'))
     if proc.stdout.read(1):raise ValueError('Excess decoder output')
     if proc.wait()!=0:raise ValueError('Decoder failed '+movie)
    finally:
     if proc.poll()is None:proc.kill();proc.wait()
   info['command']=command;print(json.dumps({'movie':movie,'kept_frames':len(info['selected']),'decoded':count,'bytes_so_far':compressed_bytes}),flush=True)
 for clip in clips:
  info=movies[clip['movie']]
  for slide in clip['source_sequence']:slide['frame_indices']=[info['frame_ids'][angle*info['num_frames']+slide['original_frame']-1]for angle in clip['angle_indices']]
 target=a.output/'actor-animations.bin'
 with target.open('xb')as out:
  out.write(b'HAA1'+struct.pack('<II',len(frames),len(clips)))
  for f in frames:
   out.write(struct.pack('<8I2f',*f['canvas'],*f['crop'],f['movie_frame0'],f['original_angle'],*f['origin']));out.write(struct.pack('<III',f['raw_bytes'],f['compressed_bytes'],f['crc32']))
   with payload.open('rb')as p:p.seek(f['payload_offset']);out.write(p.read(f['compressed_bytes']))
  for c in clips:
   for k in ['name','chain','move_animation']:string(out,c[k])
   out.write(struct.pack('<4I4f',c['original_angles'],len(c['angle_indices']),c['flags'],len(c['source_sequence']),c['world_scale'],c['offset_x'],c['offset_y'],c['seconds_per_tick']))
   out.write(struct.pack('<'+'I'*len(c['angle_indices']),*c['angle_indices']))
   for s in c['source_sequence']:
    out.write(struct.pack('<II',s['original_frame'],s['ticks']));out.write(struct.pack('<'+'I'*len(s['frame_indices']),*s['frame_indices']));string(out,json.dumps(s['events'],separators=(',',':')))
 payload.unlink();elapsed=time.monotonic()-start
 report={'schema':'hades-original-actor-animations-v1','packet':identity(target),'decoder':identity(a.decoder),'sources':list({x['path']:x for x in sources}.values()),'clips':clips,'frames':frames,'movies':[{k:v for k,v in m.items()if k not in('path','selected','frame_ids')}for m in movies.values()],'flag_bits':FLAGS+['Book'],'statistics':{'clips':len(clips),'movie_frames':len(frames),'cropped_raw_bytes':raw_bytes,'zlib_bytes':compressed_bytes,'max_frame_rgba_bytes':max_raw,'export_seconds':elapsed},'timing':'Slide: native DurationFrames/60 with one-positive-transition-per-update and0.0001threshold; NextSlide skips zero durations. Book: source PlaySpeed or native DefaultDataPlaySpeed60, fractional progression. Modified time/speed multiplier remains caller responsibility.','limits':['Selected8 evenly spaced source angles only; runtime exact angle remapping requires source owner-angle chain.','Event names retained, not silently executed. Child FX/sound/footstep dependencies require native systems.','Source positive slide schedules; original dynamic owner time, transition/animation blocking and callbacks remain separate.','Original source canvas/crop and atlas pivot metadata retained; no imitation assets.']}
 (a.output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report['statistics']),flush=True)
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--content',type=Path,required=True);p.add_argument('--decoder',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--directions',type=int,default=8);a=p.parse_args()
 if not 1<=a.directions<=8:raise ValueError('Bounded selected source directions only')
 export(a)
