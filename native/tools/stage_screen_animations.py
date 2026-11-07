"""Validate and stage original screen images and source-bound Book movies."""
import hashlib,json,math,shutil,struct
from pathlib import Path
# ActorAnimationBank::load (native/app/actor_animation.cpp) validates each HAA1
# geometry/clip and caps aggregate decoded frames at2GiB. Screen staging keeps
# its stricter512MiB packet cap, independently of lazy native decode/cache.
MAX_SCREEN_PACKET_BYTES=512*1024*1024

def _same(a,b):return math.isfinite(a)and math.isfinite(b)and math.isclose(a,b,rel_tol=1e-6,abs_tol=1e-5)

def _packet(path,frames,count):
 """Read actual packet metadata without inflating the retained pixel bank."""
 images=[];clips={};raw_total=0;slide_total=0
 with Path(path).open('rb')as f:
  def read(n):
   value=f.read(n)
   if len(value)!=n:raise ValueError('Truncated original screen packet')
   return value
  def text(limit=4096):
   size=struct.unpack('<I',read(4))[0]
   if size>limit:raise ValueError('Screen packet text bound')
   value=read(size).decode('utf-8')
   if '\0'in value:raise ValueError('Screen packet embedded null')
   return value
  if read(12)!=b'HAA1'+struct.pack('<II',frames,count):raise ValueError('Screen source packet/header differs from validated closure')
  for _ in range(frames):
   v=struct.unpack('<8I2f3I',read(52));w,h,x,y,cw,ch,_,angle,ox,oy,raw,packed,_=v
   if not(0<w<=2048 and 0<h<=2048 and 0<cw<=w and 0<ch<=h and x+cw<=w and y+ch<=h and raw==cw*ch*4 and raw<=16*1024*1024 and 0<packed<=raw+65536 and angle<1024 and math.isfinite(ox)and math.isfinite(oy)):raise ValueError('Screen original frame outside native geometry bounds')
   raw_total+=raw
   if raw_total>2*1024*1024*1024:raise ValueError('Screen decoded frame budget exceeded')
   if f.seek(packed,1)>Path(path).stat().st_size:raise ValueError('Screen compressed frame outside packet')
   images.append(v)
  for _ in range(count):
   name,chain,move=[text()for _ in range(3)];original,angles,flags,slides,scale,ox,oy,seconds=struct.unpack('<4I4f',read(32));slide_total+=slides
   if not name or name in clips or not(0<angles<=64 and angles<=original<=1024 and flags<=1023 and 0<slides<=10000 and slide_total<=100000 and 0<scale<=16 and 0<seconds<=1 and all(math.isfinite(v)for v in[scale,ox,oy,seconds])):raise ValueError('Screen original clip outside native bounds')
   angle_ids=list(struct.unpack('<'+'I'*angles,read(4*angles)))
   if angle_ids!=sorted(set(angle_ids))or angle_ids[-1]>=original:raise ValueError('Screen original angle order')
   seq=[];ticks=0
   for _ in range(slides):
    frame,duration=struct.unpack('<II',read(8));ticks+=duration;indices=list(struct.unpack('<'+'I'*angles,read(4*angles)));events=json.loads(text(65536))
    if frame<1 or not 0<duration<=100000 or ticks>1000000 or any(i>=frames or images[i][7]!=angle_ids[a]for a,i in enumerate(indices)):raise ValueError('Screen original slide binding/time bound')
    seq.append({'original_frame':frame,'ticks':duration,'frame_indices':indices,'events':events})
   clips[name]={'angles':angle_ids,'original_angles':original,'flags':flags,'scale':scale,'offset':[ox,oy],'seconds':seconds,'sequence':seq,'chain':chain,'move':move}
  if f.read(1):raise ValueError('Unexpected screen packet suffix')
 return images,clips,raw_total

def _movie_bindings(m,images,packet_clips):
 from hades_assets import Reader
 sources={row['sha256']:[]for row in m['sources']}
 for row in m['sources']:sources[row['sha256']].append(row)
 proofs=m.get('movie_decode_proofs',[])
 for c in m['clips']:
  binding=c.get('movie_binding');d=c['definition']
  if not binding:continue # Legacy verified movie rows retain their exact schema.
  keys={'video_texture','source_sha256','atlas_sha256','canvas','original_size','atlas_scale','source_frame_count','exported_source_frames'}
  if set(binding)!=keys or d.get('Type')!='Book'or d.get('VideoTexture')!=binding['video_texture']or d.get('FrameDataFile'):raise ValueError('Unsupported screen movie binding schema/consumer')
  video=binding['video_texture']
  if not isinstance(video,str)or not video or Path(video).name!=video or '\\'in video or '..'in video:raise ValueError('Screen movie original name bound')
  def source(key,suffix):
   rows=[r for r in sources.get(binding[key],[])if Path(r['path']).name==video+suffix]
   if len({str(Path(r['path']).resolve())for r in rows})!=1:raise ValueError('Screen movie original source identity not bound')
   return Path(rows[0]['path'])
  movie=source('source_sha256','.bik');atlas=source('atlas_sha256','.bik_atlas')
  with movie.open('rb')as f:header=f.read(44)
  r=Reader(atlas.read_bytes());version=r.integer();name=r.smallstring();w,h,ow,oh=[r.integer()for _ in range(4)];scale=r.floating();n=binding['source_frame_count']
  if len(header)!=44 or header[:4]!=b'KB2j'or version!=3 or r.pos!=len(r.data)or name!=video or list(struct.unpack_from('<II',header,20))!=[w,h]or struct.unpack_from('<I',header,8)[0]!=n or binding['canvas']!=[w,h]or binding['original_size']!=[ow,oh]or not _same(binding['atlas_scale'],scale)or not 0<scale<=8 or min(ow,oh)<=0:raise ValueError('Screen movie original carrier/atlas metadata differs')
  if type(n)is not int or not 0<n<=1000 or binding['exported_source_frames']!=list(range(1,n+1))or d.get('StartFrame',1)!=1 or d.get('EndFrame',n)!=n or d.get('NumFrames',1)!=n:raise ValueError('Screen movie must retain every authored original frame')
  matches=[p for p in proofs if p.get('movies',{}).get(c['name'])==binding]
  if len(matches)!=1:raise ValueError('Screen movie lacks one original decoder proof')
  proof=matches[0]
  if proof.get('source_variant')!='KB2j'or proof.get('pixel_format')!='rgba'or proof.get('decoder_matches_retained_bank')is not True or any(proof['decoder'][k]!=m['decoder'][k]for k in ['sha256','bytes']):raise ValueError('Screen movie decoder proof differs from retained bank')
  for field in['decoder','alpha_source','alpha_patch']:
   row=proof[field]
   if row not in m['sources']:raise ValueError('Screen movie decoder proof is not source-bound')
  if 'c->has_alpha = !!(c->flags & BINK_FLAG_ALPHA);'not in Path(proof['alpha_source']['path']).read_text()or '+    c->has_alpha = !!(c->flags & BINK_FLAG_ALPHA);'not in Path(proof['alpha_patch']['path']).read_text():raise ValueError('Screen movie alpha normalization evidence missing')
  packet=packet_clips[c['name']];seq=packet['sequence'];speed=d.get('PlaySpeed',0)or 60
  if packet['angles']!=[0]or packet['original_angles']!=1 or not packet['flags']&512 or not _same(packet['scale'],d.get('Scale',1)/scale)or not all(_same(v,d.get(k,0))for v,k in zip(packet['offset'],['OffsetX','OffsetY']))or not _same(packet['seconds'],1/speed)or len(seq)!=n:raise ValueError('Screen movie Book geometry/timing differs from source')
  origin=[ow/2-math.trunc((ow-w)/2),oh/2-math.trunc((oh-h)/2)]
  for number,row in enumerate(seq,1):
   if row['original_frame']!=number or row['ticks']!=1 or row['events']!={}:raise ValueError('Screen movie Book source schedule differs')
   frame=images[row['frame_indices'][0]]
   if list(frame[:2])!=[w,h]or frame[6]!=number-1 or frame[7]!=0 or not all(_same(a,b)for a,b in zip(frame[8:10],origin)):raise ValueError('Screen movie frame geometry/source ordering differs')

def validate_screen_animations(manifest_path,assets=None):
 m=json.loads(Path(manifest_path).read_text())
 items=[m['packet'],m['screen_catalog'],*m['sources']]
 if 'independent_movie_predecode'in m:items.append(m['independent_movie_predecode'])
 checked={}
 for item in items:
  p=Path(item['path']);key=str(p.resolve());value=(item['bytes'],item['sha256'])
  if key in checked:
   if checked[key]!=value:raise ValueError('Conflicting original screen source identities')
   continue
  if p.stat().st_size!=item['bytes']or hashlib.file_digest(p.open('rb'),'sha256').hexdigest()!=item['sha256']:raise ValueError('Original screen resource/source changed: '+str(p))
  checked[key]=value
 graph_edges=m.get('gui_review',{}).get('closure_edges',[])
 action_children={e['child']for e in graph_edges if e.get('backend')=='action'and e['field']=='ChildAnimation'}
 if any(e['field']=='ChildAnimation'and e.get('backend')!='action'and e['child']in action_children for e in graph_edges):raise ValueError('Screen child name requires conflicting per-parent backend consumers')
 needs_action=any(e.get('backend')=='action'for e in graph_edges)or any(c['definition'].get('CreateAnimation')not in(None,'','null')or c['definition'].get('CreateAnimations')for c in m['clips'])
 if needs_action and 'action_binding'not in m:raise ValueError('Screen graph requires an action resource binding')
 if assets is not None and 'action_binding'in m:
  assets=Path(assets);binding=m['action_binding']
  if (assets/'action-effect-sha256.bin').read_bytes().hex()!=binding['packet_sha256']or hashlib.file_digest((assets/'action-effects.lua').open('rb'),'sha256').hexdigest()!=binding['lua']['sha256']:raise ValueError('Staged action resources differ from screen graph binding')
 base={'RemBGIntro','RemBG','Backgrounds\\rectangle_01','Blank','VignetteOverlay'}
 count,frames=m['statistics']['clips'],m['statistics']['movie_frames'];names=[c['name']for c in m['clips']]
 expected=base if(count,frames)==(5,133)else(base|{'RoomTransitionIn','RoomTransitionOut'}if(count,frames)==(7,153)else None)
 gui=m.get('gui_review')
 if gui:
  admitted=set(gui['reviewed_animations'])|{e['child']for e in gui['closure_edges']if e.get('backend')!='action'}|set(gui['reviewed_graphics'])
  expected=base|{'RoomTransitionIn','RoomTransitionOut'}|admitted
 if expected is None or set(names)!=expected or len(names)!=len(expected)or count!=len(names):raise ValueError('Unexpected bounded screen original source closure')
 if m['packet']['bytes']>MAX_SCREEN_PACKET_BYTES or not 0<frames<=65536 or not 0<count<=4096:raise ValueError('Screen original packet exceeds native HAA1 size/count bounds')
 images,clips,raw_total=_packet(m['packet']['path'],frames,count)
 if set(clips)!=set(names):raise ValueError('Screen packet clips differ from manifest names')
 by_index={r['index']:r for r in m['frames']}
 if len(by_index)!=len(m['frames'])or any(type(i)is not int or not 0<=i<frames for i in by_index):raise ValueError('Screen original frame metadata index bound')
 for i,row in by_index.items():
  v=images[i]
  if (row['raw_bytes'],row['compressed_bytes'],row['crc32'])!=tuple(v[10:13]):raise ValueError('Screen frame payload metadata differs')
  if 'movie'in row and (row['canvas']!=list(v[:2])or row['crop']!=list(v[2:6])or row['movie_frame0']!=v[6]or row['original_angle']!=v[7]):raise ValueError('Screen movie frame metadata differs')
 static=m.get('screen_static_graphics',[]);static_ids=set()
 for row in static:
  seq=clips[row['name']]['sequence']
  if len(seq)!=1 or len(seq[0]['frame_indices'])!=1:raise ValueError('Screen static original schedule differs')
  i=seq[0]['frame_indices'][0];static_ids.add(i)
  if images[i][10]!=row['raw_rgba_bytes']:raise ValueError('Screen static original byte count differs')
 if len(static_ids)!=len(static)or set(by_index)&static_ids or set(by_index)|static_ids!=set(range(frames)):raise ValueError('Screen original movie/GUI/static frame partition differs')
 gui_rows=[row for row in m['frames']if 'movie'not in row]
 if len(gui_rows)!=m['statistics'].get('gui_original_images',0)or sum(row['raw_bytes']for row in gui_rows)!=m['statistics'].get('gui_raw_rgba_bytes',0):raise ValueError('Screen GUI original frame partition differs')
 for c in m['clips']:
  if c['source_sequence']and c['source_sequence']!=clips[c['name']]['sequence']:raise ValueError('Screen source schedule differs from actual packet: '+c['name'])
 if count>=7:
  out=next(c for c in m['clips']if c['name']=='RoomTransitionOut')
  if not out['definition'].get('PlayBackwards')or out['definition'].get('PlaySpeed')!=30 or [r['original_frame']for r in out['source_sequence']]!=list(range(20,0,-1)):raise ValueError('Original transition backward20-frame schedule changed')
 _movie_bindings(m,images,clips)
 return m,{'packet_sha256':m['packet']['sha256'],'bytes':m['packet']['bytes'],'original_movie_frames':len(m['frames'])-len(gui_rows),'original_static_images':len(static),'original_gui_images':len(gui_rows),'gui_clips':count-(7 if count>5 else 5),'decoded_rgba_bytes':raw_total}

def stage_screen_animations(manifest_path,assets):
 m,report=validate_screen_animations(manifest_path,assets);assets=Path(assets)
 shutil.copyfile(m['packet']['path'],assets/'screen-animation-frames.bin');shutil.copyfile(m['screen_catalog']['path'],assets/'screen-animations.lua');(assets/'screen-animation-sha256.bin').write_bytes(bytes.fromhex(m['packet']['sha256']))
 return report
