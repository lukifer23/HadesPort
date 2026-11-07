"""Coverage-derived original GUI animation admission for the native screen bank.

Every admitted name is an original Game/Animations record (InheritFrom
resolved with the native Read rules already used for action effects) or an
original obstacle Thing.Graphic sprite path (native Dataless animation). The
script provenance names the original call site that reaches it. Pixels are
lossless crops of the original GUI package atlases; no art is synthesized.
"""
import copy,hashlib,json,struct,zlib,tempfile
from io import BytesIO
from pathlib import Path
from types import SimpleNamespace
from build_room_scene import Sjson,identity
from hades_assets import Package,xnb_image
from export_actor_animations import string,export as export_movies
from export_screen_transition import clips as rebase_movie_clips

# Admitted names are not hand-listed: they come from the static animation
# coverage report (animation_coverage.py) - every original animation
# record the original scripts reach in scope whose properties this consumer
# reconstructs, plus obstacle Thing.Graphic sprite paths named by reachable
# SpawnObstacle/CreateScreenObstacle calls. Each keeps its script provenance;
# names the screen consumer cannot represent are listed with the reason.
# Source keys whose runtime semantics are reconstructed by screen_animation.cpp.
# Any other key (VisualFx, RandomPlaySpeed, CreateAnimations, ...)
# rejects the export rather than shipping an unimplemented record.
# Slides: SlideAnimation per-slide DurationFrames (SetupSlideIndex
# 0x1802b5010 copies each 0x110-byte slide record's +0x1c duration; NextSlide
# 0x1802b46d0 skips zero-duration slides and loops to StartFrame; SlideStart
# 0x1802b4080 replaces instance duration from the shared bounded RNG, then adds
# duration/60). GetTexture 0x1802b4ae0 uses nonempty per-slide FilePath verbatim.
# Sound: animation-level cue played by Animation.InitData 0x180291c70
# (SoundAllowedAfterFrame gate, default 2).
KEYS={'Name','InheritFrom','FilePath','Type','StartFrame','EndFrame','NumFrames','PlaySpeed','OffsetX','OffsetY','OriginX','OriginY','Scale','ScaleX','ScaleY','Alpha','AddColor','Material','SortMode','Loop','HoldLastFrame','ChildAnimation','AngleFromOwner','ScaleFromOwner','ColorFromOwner','DieWithOwner','Red','Green','Blue','Slides','Sound','Color','StartAlpha','EndAlpha','Duration','ChainTo','AttachedTextScalarX','AttachedTextScalarY','Ambient',
 # Constant transition helpers (screen_animation.cpp: InitData
 # 0x180292566..0x1802926e3 colour channels, GetX/YShift 0x180297b40/
 # 0x180297a50 + GetValue 0x180298750, scale helper 0x18029217d..0x1802922fd,
 # duration Loop via Animation::Loop 0x180296310).
 'StartRed','StartGreen','StartBlue','EndRed','EndGreen','EndBlue','EaseIn','EaseOut','PingPongColor',
 'StartOffsetX','EndOffsetX','StartOffsetY','EndOffsetY','PingPongShiftOverDuration','PhaseOffsetShiftOverDuration',
 'StartScale','EndScale','PingPongScale','CancelAllAttached','CreateAnimation','CreateAnimations','TimeModifierFraction',
 # Shared helper contract: InitData RVA0x29217d..0x2922fd (axis scale),
 # RVA0x2928ab..0x292941 (angle), GetValue RVA0x291290 (duration), and
 # Loop RVA0x296414..0x296476 (duration rerandomization). See the matching
 # target/PDB identities in screen-animation-helpers-contract.json.
 'StartScaleX','EndScaleX','StartScaleY','EndScaleY','StartAngle','EndAngle','PingPongAngle','PhaseOffsetAngle',
 'RandomDurationMin','RandomDurationMax','ReRandomizeOnLoop','VideoTexture'}
# Transition keys driven by the Constant duration fraction only.
CONSTANT_TRANSITION_KEYS={'Duration','StartAlpha','EndAlpha','StartRed','StartGreen','StartBlue','EndRed','EndGreen','EndBlue','EaseIn','EaseOut','PingPongColor','StartOffsetX','EndOffsetX','StartOffsetY','EndOffsetY','PingPongShiftOverDuration','PhaseOffsetShiftOverDuration','StartScale','EndScale','PingPongScale','StartScaleX','EndScaleX','StartScaleY','EndScaleY','StartAngle','EndAngle','PingPongAngle','PhaseOffsetAngle','RandomDurationMin','RandomDurationMax','ReRandomizeOnLoop'}
TYPES={'Constant','ConstantNoUpdate','Book','Slide'}
SLIDE_KEYS={'DurationFrames','FilePath','RandomDurationFramesMin','RandomDurationFramesMax'}

class Ranged(Sjson):
 """Character ranges of each parsed record (converted to UTF-8 bytes once)."""
 def __init__(self,text):super().__init__(text);self.spans={};self.start=0
 def token(self):self.skip();self.start=self.pos;return super().token()
 def value(self,t=None):
  if t is None:t=self.token()
  begin=self.start;r=super().value(t)
  if isinstance(r,dict):self.spans[id(r)]=(begin,self.pos)
  return r

def load_records(content):
 records={};origin={};sources=[]
 # Screen animation names can come from any original bank (the use prompt
 # references Fx.sjson through UIData). Consumer property review, rather than
 # source filename, determines admission; unsupported shapes stay explicit.
 for path in sorted((content/'Game/Animations').glob('*.sjson')):
  raw=path.read_bytes();text=raw.decode('utf-8-sig');parser=Ranged(text);sources.append(path)
  bom=len(raw)-len(text.encode('utf-8'))
  for d in parser.parse()['Animations']:
   if 'Name'not in d:continue
   if d['Name']in records:raise ValueError('Ambiguous original animation '+d['Name'])
   b,e=parser.spans[id(d)]
   records[d['Name']]=d;origin[d['Name']]=(path,raw,text,bom,b,e)
 return records,origin,sources

def byte_span(text,bom,b,e):
 head=len(text[:b].encode('utf-8'));return [bom+head,len(text[b:e].encode('utf-8'))]

def resolve_all(records,origin,roots,action_definitions=None):
 action_definitions=action_definitions or {}
 resolved={};provenance={};edges=[]
 def resolve(name,stack=()):
  if name in resolved:return resolved[name]
  if name in stack:raise ValueError('Original animation inheritance cycle '+name)
  if name not in records:raise ValueError('Original GUI animation record missing '+name)
  own=records[name];parent=own.get('InheritFrom');d=copy.deepcopy(resolve(parent,stack+(name,)))if parent and parent!='null'else{}
  if own.get('ClearSlides'):d['Slides']=[]
  if own.get('ClearCreateAnimations'):d['CreateAnimations']=[]
  old=d.get('CreateAnimations',[]);d.update(copy.deepcopy(own))
  if 'CreateAnimations'in own:d['CreateAnimations']=old+copy.deepcopy(own['CreateAnimations'])
  path,raw,text,bom,b,e=origin[name];span=byte_span(text,bom,b,e)
  provenance[name]={'path':str(path.resolve()),'file_sha256':hashlib.sha256(raw).hexdigest(),'range':span,'range_sha256':hashlib.sha256(raw[span[0]:sum(span)]).hexdigest(),'inherit_from':parent,'source':own}
  resolved[name]=d;return d
 names=list(roots)
 for name in names:
  d=resolve(name)
  unknown=set(d)-KEYS
  if unknown:raise ValueError('Unreviewed GUI animation property '+name+': '+repr(sorted(unknown)))
  # Keep admission bounded to the recovered screen cancellation route.
  if d.get('CancelAllAttached') and d.get('ChildAnimation') not in (None,'','null'):raise ValueError('Native screen CancelAllAttached with new ChildAnimation not reconstructed: '+name)
  if d.get('Type')not in TYPES:raise ValueError('Unreviewed GUI animation type '+name)
  # Book.Init 0x1802af310 / SetupFrameUpdateMode 0x1802b1040 never
  # initialize or dispatch SlideData (export_actor_animations.py): Book
  # Slides are inert. Other non-Slide types with Slides stay unreviewed.
  if 'Slides'in d and d.get('Type')not in('Slide','Book'):raise ValueError('Unreviewed non-Slide Slides data '+name)
  if d.get('Type')=='Slide':slide_durations(d)
  if any(k in d for k in CONSTANT_TRANSITION_KEYS) and d.get('Type')!='Constant':raise ValueError('Unreviewed non-Constant screen duration/transition '+name)
  for field in ['ChildAnimation','ChainTo']:
   child=d.get(field)
   if child and child!='null':
    child_record=resolve(child)
    if field=='ChildAnimation' and child in action_definitions and (set(child_record)-KEYS or child_record.get('Type')not in TYPES):
     edges.append({'parent':name,'field':field,'child':child,'backend':'action'});continue
    if field=='ChildAnimation' and resolve(child).get('CancelAllAttached'):raise ValueError('Native screen child CancelAllAttached parent cancellation not reconstructed: '+child)
    edges.append({'parent':name,'field':field,'child':child})
    if child not in names:names.append(child)
  created=[d.get('CreateAnimation')]+[r.get('Name')for r in d.get('CreateAnimations',[])]
  for child in created:
   if child and child!='null':
    if child not in action_definitions:raise ValueError('Original GUI created animation requires selected action resource: '+child)
    edges.append({'parent':name,'field':'CreateAnimation','child':child,'backend':'action'})
  if len(names)>4096:raise ValueError('Bounded GUI closure exceeded')
 return {n:resolved[n]for n in names},provenance,edges

def slide_durations(d):
 """Per-slide original DurationFrames of a Slide record (all slides kept)."""
 slides=d.get('Slides')
 if not isinstance(slides,list)or not slides:raise ValueError('Slide record without original Slides '+d['Name'])
 if any(not isinstance(x,dict)or set(x)-SLIDE_KEYS or 'DurationFrames'not in x for x in slides):raise ValueError('Unreviewed per-slide property '+d['Name'])
 out=[x['DurationFrames']for x in slides]
 if any(type(v)is not int or not 0<=v<=100000 for v in out)or sum(out)>1000000:raise ValueError('Slide duration outside bounds '+d['Name'])
 for slide in slides:
  lo,hi=slide.get('RandomDurationFramesMin',0),slide.get('RandomDurationFramesMax',0)
  if any(type(v)is not int or not 0<=v<=100000 for v in (lo,hi))or(lo>0 and hi<lo):raise ValueError('Slide random duration outside bounds '+d['Name'])
  if 'FilePath'in slide and not isinstance(slide['FilePath'],str):raise ValueError('Slide FilePath must be original text '+d['Name'])
 # SetupSlideIndex enters at StartFrame-1 then NextSlide; NextSlide's bound is
 # the slide-record count, so playback covers slides StartFrame..len(Slides).
 # Only StartFrame 1 and EndFrame absent/== len(Slides) are reviewed.
 if d.get('StartFrame',1)!=1 or d.get('EndFrame',len(out))!=len(out) or d.get('NumFrames',len(out))!=len(out):raise ValueError('Unreviewed Slide frame range '+d['Name'])
 if not any(out):raise ValueError('Slide record without positive duration '+d['Name'])
 # Plain Loop wraps to StartFrame; PingPong remains an unadmitted top-level
 # key. Slide inherits Book PrepDraw/SubmitDraw (vtable0x1805f80d0+10/+18),
 # including its unscaled screen OffsetX/Y path.
 return out

def image_names(d):
 if d.get('VideoTexture'):
  if d.get('Type')!='Book':raise ValueError('Original screen movie requires Book '+d['Name'])
  return [] # Actual movie frames are appended by their separate validated backend.
 if d.get('Type')=='Slide':
  # Approximation boundary: zero-duration slides are never started (NextSlide
  # skips them), so only positive-duration slide images are exported. The
  # HoldLastFrame end state (NextSlide sets index=EndFrame) is enforced at
  # runtime (screen_animation.cpp fails loudly if it needs an unexported image).
  return [(i,d['Slides'][i-1].get('FilePath')or d['FilePath']+f'{i:04}')for i,v in enumerate(slide_durations(d),1)if v]
 n=d.get('NumFrames',1);s=d.get('StartFrame',1);e=d.get('EndFrame',n)
 if not(isinstance(n,int)and isinstance(s,int)and isinstance(e,int)and 1<=s<=e<=n<=1000):raise ValueError('Unreviewed GUI frame range '+d['Name'])
 if n==1:return [(1,d['FilePath'])]
 return [(i,d['FilePath']+f'{i:04}')for i in range(s,e+1)]

def split(raw):
 if raw[:4]!=b'HAA1':raise ValueError('Original screen bank magic')
 nf,nc=struct.unpack_from('<II',raw,4);at=12
 for _ in range(nf):at+=52+struct.unpack_from('<I',raw,at+44)[0]
 if at>len(raw):raise ValueError('Original screen bank frame bound')
 return nf,nc,at

def coverage_roots(a,records,origin):
 """Screen roots/graphics from the coverage report, with per-name rejections."""
 cov=json.loads(Path(a.coverage).read_text())
 if cov.get('schema')!='hades-animation-coverage-v1':raise ValueError('Animation coverage schema')
 reviewed,rejected={},{}
 for name,prov in cov['provenance'].items():
  if name not in records:continue
  # Provenance rows are strings, or source-bound dicts (forced encounter).
  reviewed[name]='; '.join(x if isinstance(x,str)else json.dumps(x,sort_keys=True)for x in prov[:3]) or 'coverage closure'
 graphics={g:'; '.join(x if isinstance(x,str)else json.dumps(x,sort_keys=True)for x in p[:3]) for g,p in cov.get('obstacle_graphics',{}).items()}
 return reviewed,graphics,rejected,cov

def admissible(records,origin,names,rejected,action_definitions=None,content=None,existing=()):
 """Drop (with reason) names whose own or ChildAnimation-closure record this consumer cannot represent."""
 ok=[]
 for name in names:
  try:
   d,_,_=resolve_all(records,origin,[name],action_definitions)
   for child,x in d.items():
    image_names(x)
    if x.get('VideoTexture')and child not in existing:movie_source(content,x)
   ok.append(name)
  except ValueError as e:rejected[name]=str(e)
 return ok

def movie_source(content,d):
 """Original single-angle Book carrier/atlas facts; no decoding or defaults."""
 if content is None:raise ValueError('Original screen movie source required')
 name=d['Name'];video=d.get('VideoTexture');n=d.get('NumFrames',1);start=d.get('StartFrame',1);end=d.get('EndFrame',n)
 if d.get('FrameDataFile'):raise ValueError('Original screen movie FrameDataFile consumer not reconstructed: '+name)
 if d.get('Type')!='Book'or d.get('NumAngles',1)!=1 or not isinstance(video,str)or not video or Path(video).name!=video or '\\'in video or '..'in video:raise ValueError('Unreviewed screen movie backend '+name)
 if any(type(v)is not int for v in[n,start,end])or not 1<=start<=end<=n<=1000:raise ValueError('Unreviewed screen movie frame window '+name)
 if start!=1 or end!=n:raise ValueError('Screen movie partial frame window not reconstructed: '+name)
 if d.get('OriginX',0)!=0 or d.get('OriginY',0)!=0:raise ValueError('Screen movie explicit origin not reconstructed: '+name)
 if d.get('RandomStartFrame')or d.get('RandomPlaySpeedMin',0)or d.get('RandomPlaySpeedMax',0):raise ValueError('Screen movie random frame/speed not reconstructed: '+name)
 movie=content/'Movies/720p'/(video+'.bik');atlas=movie.with_suffix('.bik_atlas')
 if not movie.is_file()or not atlas.is_file():raise ValueError('Original screen movie/atlas unavailable: '+name)
 from hades_assets import Reader
 raw=atlas.read_bytes();reader=Reader(raw);version=reader.integer();atlas_name=reader.smallstring();w,h,ow,oh=[reader.integer()for _ in range(4)];scale=reader.floating()
 with movie.open('rb')as stream:header=stream.read(44)
 if len(header)!=44:raise ValueError('Original screen movie header truncated: '+name)
 frames=struct.unpack_from('<I',header,8)[0];canvas=struct.unpack_from('<II',header,20)
 if version!=3 or reader.pos!=len(raw)or atlas_name!=video or header[:4]!=b'KB2j'or canvas!=(w,h)or not 0<w<=2048 or not 0<h<=2048 or not 0<scale<=8 or min(ow,oh)<=0:raise ValueError('Original screen movie/atlas identity or bounds mismatch: '+name)
 if frames!=n:raise ValueError('Original screen movie frame count differs from authored Book: '+name+f' actual={frames} authored={n}')
 sources=[identity(movie),identity(atlas)]
 binding={'video_texture':video,'source_sha256':sources[0]['sha256'],'atlas_sha256':sources[1]['sha256'],'canvas':[w,h],'original_size':[ow,oh],'atlas_scale':scale,'source_frame_count':frames,'exported_source_frames':list(range(start,end+1))}
 return binding,sources

def append_movie_definitions(a,m,definitions,provenance,reviewed):
 if not definitions:return m
 decoder=identity(a.decoder)
 if decoder['sha256']!=m['decoder']['sha256']:raise ValueError('Screen movie decoder differs from retained verified decoder')
 source=Path(a.decoder).resolve().parent/'libavcodec/bink2.c';patch=Path(__file__).with_name('bink2-alpha-normalization.patch')
 if not source.is_file()or 'c->has_alpha = !!(c->flags & BINK_FLAG_ALPHA);'not in source.read_text():raise ValueError('Original Bink2 decoder alpha normalization source unavailable')
 proof={'decoder':decoder,'alpha_source':identity(source),'alpha_patch':identity(patch),'source_variant':'KB2j','pixel_format':'rgba','decoder_matches_retained_bank':True,'movies':{}}
 _,_,anim_sources=load_records(a.content)
 bindings={};inputs=[]
 for name,d in definitions.items():
  bindings[name],rows=movie_source(a.content,d);inputs.extend(rows);proof['movies'][name]=bindings[name]
 with tempfile.TemporaryDirectory(prefix='movie-decode-',dir=a.output)as temporary:
  args=SimpleNamespace(content=a.content,decoder=a.decoder,output=Path(temporary)/'source',sources=anim_sources,names=list(definitions),existing_names=set(definitions)|{c['name']for c in m['clips']},unit_scale=1.,directions=1,movie_directory=a.content/'Movies/720p')
  export_movies(args);extra=json.loads((args.output/'manifest.json').read_text());raw=Path(m['packet']['path']).read_bytes();fresh=Path(extra['packet']['path']).read_bytes();nf,nc,cut=split(raw);xf,xc,xcut=split(fresh)
  if set(c['name']for c in extra['clips'])!=set(definitions):raise ValueError('Original movie decode escaped reviewed resource closure')
  target=a.output/'actor-animations.bin';target.write_bytes(b'HAA1'+struct.pack('<II',nf+xf,nc+xc)+raw[12:cut]+fresh[12:xcut]+raw[cut:]+rebase_movie_clips(fresh,xcut,xc,nf,set()))
  new_clips=copy.deepcopy(extra['clips']);new_frames=copy.deepcopy(extra['frames'])
  for row in new_frames:row['index']+=nf
  for clip in new_clips:
   name=clip['name'];clip['definition']=definitions[name];clip['movie_binding']=bindings[name];clip['provenance']=provenance[name];clip['script']=reviewed.get(name,'original movie dependency closure')
   for step in clip['source_sequence']:step['frame_indices']=[value+nf for value in step['frame_indices']]
  m['clips']+=new_clips;m['frames']+=new_frames;m['movies']+=extra['movies'];m['packet']=identity(target)
  st=m['statistics'];st['clips']=nc+xc;st['movie_frames']=nf+xf
  for key in['cropped_raw_bytes','zlib_bytes']:st[key]+=extra['statistics'][key]
  st['max_frame_rgba_bytes']=max(st['max_frame_rgba_bytes'],extra['statistics']['max_frame_rgba_bytes'])
  inputs+=extra['sources']
 m.setdefault('movie_decode_proofs',[]).append(proof)
 old={(str(Path(row['path']).resolve()),row['sha256'],row['bytes'])for row in m['sources']}
 for row in inputs+[decoder,proof['alpha_source'],proof['alpha_patch']]:
  key=(str(Path(row['path']).resolve()),row['sha256'],row['bytes'])
  if key not in old:m['sources'].append(row);old.add(key)
 return m

def append_gui(a,m):
 retained_review=copy.deepcopy(m.get('gui_review',{}))
 retained_gui_images=m['statistics'].get('gui_original_images',0)
 retained_gui_bytes=m['statistics'].get('gui_raw_rgba_bytes',0)
 action_definitions={};action_binding=None
 if getattr(a,'action_effects',None):
  selected=Path(a.action_effects);am=json.loads(selected.read_text())
  if am.get('schema')!='hades-action-effect-resources-v1':raise ValueError('Screen graph action resource schema')
  for row in [am['catalog'],am['lua']]:
   source=Path(row['path'])
   if source.stat().st_size!=row['bytes']or hashlib.file_digest(source.open('rb'),'sha256').hexdigest()!=row['sha256']:raise ValueError('Screen graph action source identity changed')
  action_definitions=json.loads(Path(am['catalog']['path']).read_text())['Definitions']
  action_binding={'manifest':identity(selected),'catalog':am['catalog'],'lua':am['lua'],'packet_sha256':am['packet']['sha256']}
 records,origin,anim_sources=load_records(a.content)
 reviewed,graphics,rejected,cov=coverage_roots(a,records,origin)
 existing={c['name']for c in m['clips']}
 names=admissible(records,origin,sorted(reviewed),rejected,action_definitions,a.content,existing)
 definitions,provenance,edges=resolve_all(records,origin,names,action_definitions)
 # Movie/static base clips keep their retained definitions.
 for n in list(definitions):
  if n in existing:rejected[n]='already in retained base screen bank';del definitions[n]
 graphics={g:p for g,p in graphics.items()if g not in existing and g not in definitions}
 movie_definitions={n:d for n,d in definitions.items()if d.get('VideoTexture')}
 clips=[]
 for name,d in definitions.items():
  if name not in movie_definitions:clips.append((name,d,image_names(d)))
 for path in graphics:clips.append((path,{'Name':path,'Type':'Dataless','FilePath':path,'NumFrames':1,'Loop':False},[(1,path)]))
 wanted={s for _,_,rows in clips for _,s in rows};matches={};variant=set();manifests=[];packages=a.content/'Win/Packages'
 for package in ['GUI','RoomManager','Dev','Fx']:
  path=packages/(package+'.pkg_manifest');manifests.append(path)
  for e in Package(path).entries():
   for s in e.get('sprites',[]):
    if s['name']not in wanted:continue
    if s['flags']&3:variant.add(s['name']);continue
    old=matches.get(s['name']);rec={'package':package,'atlas':e['name'],'sprite':s}
    if old is None or sum(abs(v-1)for v in s['scale_ratio'])<sum(abs(v-1)for v in old['sprite']['scale_ratio']):matches[s['name']]=rec
 missing=wanted-set(matches)
 for name,_,rows in list(clips):
  if any(r in variant and r in missing for _,r in rows):rejected[name]='multipart/variant original graphic';continue
  if any(r in missing for _,r in rows):rejected[name]='original image absent from GUI packages: '+next(r for _,r in rows if r in missing);continue
  ratios={tuple(matches[r]['sprite']['scale_ratio'])for _,r in rows}
  if len(ratios)!=1 or len(set(next(iter(ratios))))!=1:rejected[name]='nonuniform/anisotropic original atlas ratio'
 clips=[c for c in clips if c[0]not in rejected]
 # Pixel admission must retain the complete child AND completion-chain graph,
 # including movie parents. A dropped atlas dependency cannot leave a shell
 # whose failure is deferred until its final source frame.
 while True:
  kept={c[0]for c in clips}|set(movie_definitions)
  bad=[e for e in edges if e['parent']in kept and e.get('backend')!='action'and e['child']not in kept|existing]
  if not bad:break
  for e in bad:rejected[e['parent']]=e['field']+' not representable: '+e['child']
  clips=[c for c in clips if c[0]not in rejected]
  movie_definitions={n:d for n,d in movie_definitions.items()if n not in rejected}
 definitions={**{c[0]:c[1]for c in clips if c[0]in definitions},**movie_definitions};graphics={g:p for g,p in graphics.items()if g in{c[0]for c in clips}}
 edges=[e for e in edges if e['parent']in definitions]
 used={r for _,_,rows in clips for _,r in rows};matches={k:v for k,v in matches.items()if k in used}
 atlases={};payloads=[]
 for package in sorted({v['package']for v in matches.values()}):
  need={v['atlas']for v in matches.values()if v['package']==package}
  for e in Package(packages/(package+'.pkg')).entries():
   if e.get('name')not in need:continue
   atlases[(package,e['name'])],info=xnb_image(e['data']);payloads.append({'package':package,'atlas':e['name'],'location':e['location'],'xnb_sha256':hashlib.sha256(e['data']).hexdigest(),'decoded':info})
  if any((package,n)not in atlases for n in need):raise ValueError('Original GUI atlas payload missing')
 raw=Path(m['packet']['path']).read_bytes()
 if hashlib.sha256(raw).hexdigest()!=m['packet']['sha256']:raise ValueError('Retained screen bank identity')
 nf,nc,cut=split(raw);frame_bytes=bytearray();frame_rows=[];ids={};clip_bytes=bytearray();clip_rows=[]
 for name,d,rows in clips:
  ratios={tuple(matches[s]['sprite']['scale_ratio'])for _,s in rows}
  if len(ratios)!=1:raise ValueError('Nonuniform original atlas ratio within '+name)
  rx,ry=next(iter(ratios))
  if rx!=ry:raise ValueError('Anisotropic original atlas ratio '+name)
  sequence=[]
  for original_frame,sprite_name in rows:
   mt=matches[sprite_name];s=mt['sprite'];x,y,w,h=s['rect']
   # Animation.InitData 0x292340..0x2923bc: (OriginX,OriginY) from data;
   # only when BOTH are exactly 0 does it fall back to half the original
   # frame. AtlasMap::GetOrigin then maps by atlas ratio minus trim
   # (.local/reports/hud-draw-semantics.json).
   ox,oy=float(d.get('OriginX',0)),float(d.get('OriginY',0))
   if ox==0 and oy==0:ox,oy=s['original'][0]/2,s['original'][1]/2
   origin_xy=[ox/rx-s['trim'][0],oy/ry-s['trim'][1]]
   key=(sprite_name,origin_xy[0],origin_xy[1])
   if key not in ids:
    im=atlases[(mt['package'],mt['atlas'])]
    if min(x,y)<0 or min(w,h)<=0 or max(w,h)>4096 or x+w>im.width or y+h>im.height:raise ValueError('Original GUI atlas bounds '+sprite_name)
    pixels=im.crop((x,y,x+w,y+h)).tobytes();packed=zlib.compress(pixels,6);ids[key]=nf+len(frame_rows)
    frame_bytes+=struct.pack('<8I2f',w,h,0,0,w,h,original_frame-1,0,*origin_xy)+struct.pack('<III',len(pixels),len(packed),zlib.crc32(pixels))+packed
    frame_rows.append({'index':ids[key],'name':sprite_name,'package':mt['package'],'atlas':mt['atlas'],'sprite':s,'origin':origin_xy,'raw_bytes':len(pixels),'compressed_bytes':len(packed),'crc32':zlib.crc32(pixels),'rgba_sha256':hashlib.sha256(pixels).hexdigest()})
   sequence.append({'original_frame':original_frame,'ticks':1,'events':{},'frame_indices':[ids[key]],'image':sprite_name})
  book=d.get('Type')=='Book';flags=(1 if d.get('Loop')else 0)|(2 if d.get('HoldLastFrame')else 0)|(512 if book else 0)
  speed=d.get('PlaySpeed',60.)if book else 60.
  if not 0<speed<=1000:raise ValueError('Unreviewed GUI play speed '+name)
  # Slide records keep original DurationFrames as ticks at 1/60 s
  # (SlideStart 0x1802b4126 divides by 60.0); PlaySpeed is not a Slide rate.
  ticks=dict(enumerate(slide_durations(d),1))if d.get('Type')=='Slide'else{}
  for row in sequence:row['ticks']=ticks.get(row['original_frame'],1)
  f=BytesIO()
  for text in [name,'','']:string(f,text)
  f.write(struct.pack('<4I4f',1,1,flags,len(sequence),rx,0.,0.,1/speed));f.write(struct.pack('<I',0))
  for row in sequence:f.write(struct.pack('<II',row['original_frame'],row['ticks']));f.write(struct.pack('<I',row['frame_indices'][0]));string(f,'{}')
  clip_bytes+=f.getvalue()
  clip_rows.append({'name':name,'definition':d,'source_sequence':[{k:v for k,v in r.items()if k!='image'}for r in sequence],'images':[r['image']for r in sequence],'provenance':provenance.get(name,{'graphic':graphics.get(name)}),'script':reviewed.get(name,graphics.get(name,'ChildAnimation closure')),'atlas_ratio':[rx,ry]})
 if nf+len(frame_rows)>65536 or nc+len(clip_rows)>4096:raise ValueError('Screen bank counts outside native loader bound')
 target=a.output/'actor-animations.bin'
 target.write_bytes(b'HAA1'+struct.pack('<II',nf+len(frame_rows),nc+len(clip_rows))+raw[12:cut]+bytes(frame_bytes)+raw[cut:]+bytes(clip_bytes))
 m['packet']=identity(target)
 m['sources']=m['sources']+[identity(p)for p in anim_sources]+[identity(p)for p in manifests]+[identity(packages/(p+'.pkg'))for p in sorted({v['package']for v in matches.values()})]
 m['clips']=m['clips']+clip_rows;m['frames']=m['frames']+frame_rows
 st=m['statistics'];st['clips']=nc+len(clip_rows);st['movie_frames']=nf+len(frame_rows);st['gui_original_images']=retained_gui_images+len(frame_rows);st['gui_raw_rgba_bytes']=retained_gui_bytes+sum(r['raw_bytes']for r in frame_rows)
 m['gui_review']={'coverage':str(Path(a.coverage).resolve()),'reviewed_animations':{n:reviewed.get(n,'original dependency closure')for n in definitions},'reviewed_graphics':graphics,'rejected':dict(sorted(rejected.items())),'closure_edges':edges,'admitted_keys':sorted(KEYS),'original_payloads':payloads,'rule':'InheritFrom resolved by native Read order (ClearSlides/ClearCreateAnimations, additive CreateAnimations); OriginX/Y baked into frame origin; Offset/Scale/ScaleX/ScaleY/Alpha/Color/AddColor and Constant duration alpha transitions applied at runtime from the retained definition. AttachedTextScalar uses actual attached text layout dimensions; finite primary ChainTo retains the screen Thing owner.'}
 if action_binding:
  m['action_binding']=action_binding
  m['sources'] += [action_binding[k]for k in ['manifest','catalog','lua']]
 m['gui_review']['renderer_approximations']={n:[k for k in ['Ambient','Material','SortMode']if k in d]for n,d in definitions.items()if any(k in d for k in ['Ambient','Material','SortMode'])}
 # A retained export already owns real frames and admitted definitions. Keep
 # their review/provenance when appending the newly reached resource closure;
 # replacing this metadata makes the unchanged packet unverifiable at stage.
 review=m['gui_review']
 for field in ['reviewed_animations','reviewed_graphics','renderer_approximations']:
  review[field]={**retained_review.get(field,{}),**review.get(field,{})}
 for field in ['closure_edges','original_payloads']:
  combined=[];seen=set()
  for row in retained_review.get(field,[])+review.get(field,[]):
   key=json.dumps(row,sort_keys=True)
   if key not in seen:seen.add(key);combined.append(row)
  review[field]=combined
 review['coverage_history']=list(dict.fromkeys(retained_review.get('coverage_history',[])+([retained_review['coverage']]if retained_review.get('coverage')else[])+[review['coverage']]))
 admitted=set(review['reviewed_animations'])|set(review['reviewed_graphics'])|{e['child']for e in review['closure_edges']if e.get('backend')!='action'}
 review['rejected']={n:reason for n,reason in {**retained_review.get('rejected',{}),**review['rejected']}.items()if n not in admitted}
 m=append_movie_definitions(a,m,movie_definitions,provenance,reviewed)
 return m
