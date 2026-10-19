"""Coverage-derived original GUI animation admission for the native screen bank.

Every admitted name is an original GUIAnimations.sjson record (InheritFrom
resolved with the native Read rules already used for action effects) or an
original obstacle Thing.Graphic sprite path (native Dataless animation). The
script provenance names the original call site that reaches it. Pixels are
lossless crops of the original GUI package atlases; no art is synthesized.
"""
import copy,hashlib,json,struct,zlib
from io import BytesIO
from pathlib import Path
from build_room_scene import Sjson,identity
from hades_assets import Package,xnb_image
from export_actor_animations import string

# Admitted names are not hand-listed: they come from the static animation
# coverage report (animation_coverage.py) - every GUIAnimations/DevAnimations
# record the original scripts reach in scope whose properties this consumer
# reconstructs, plus obstacle Thing.Graphic sprite paths named by reachable
# SpawnObstacle/CreateScreenObstacle calls. Each keeps its script provenance;
# names the screen consumer cannot represent are listed with the reason.
# Source keys whose runtime semantics are reconstructed by screen_animation.cpp.
# Any other key (VisualFx, RandomPlaySpeed, CreateAnimations, ChainTo, ...)
# rejects the export rather than shipping an unimplemented record.
# Slides: SlideAnimation per-slide DurationFrames only (SetupSlideIndex
# 0x1802b5010 copies each 0x110-byte slide record's +0x1c duration; NextSlide
# 0x1802b46d0 skips zero-duration slides; SlideStart 0x1802b4080 duration/60).
# Sound: animation-level cue played by Animation.InitData 0x180291c70
# (SoundAllowedAfterFrame gate, default 2).
KEYS={'Name','InheritFrom','FilePath','Type','StartFrame','EndFrame','NumFrames','PlaySpeed','OffsetX','OffsetY','OriginX','OriginY','Scale','ScaleX','ScaleY','Alpha','AddColor','Material','SortMode','Loop','HoldLastFrame','ChildAnimation','AngleFromOwner','ScaleFromOwner','ColorFromOwner','DieWithOwner','Red','Green','Blue','Slides','Sound'}
TYPES={'Constant','ConstantNoUpdate','Book','Slide'}
SLIDE_KEYS={'DurationFrames'}

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
 for filename in ['GUIAnimations.sjson','1_DevAnimations.sjson']:
  path=content/'Game/Animations'/filename;raw=path.read_bytes();text=raw.decode('utf-8-sig');parser=Ranged(text);sources.append(path)
  bom=len(raw)-len(text.encode('utf-8'))
  for d in parser.parse()['Animations']:
   if 'Name'not in d:continue
   if d['Name']in records:raise ValueError('Ambiguous original animation '+d['Name'])
   b,e=parser.spans[id(d)]
   records[d['Name']]=d;origin[d['Name']]=(path,raw,text,bom,b,e)
 return records,origin,sources

def byte_span(text,bom,b,e):
 head=len(text[:b].encode('utf-8'));return [bom+head,len(text[b:e].encode('utf-8'))]

def resolve_all(records,origin,roots):
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
  if d.get('Type')not in TYPES:raise ValueError('Unreviewed GUI animation type '+name)
  if 'Slides'in d and d.get('Type')!='Slide':raise ValueError('Unreviewed non-Slide Slides data '+name)
  if d.get('Type')=='Slide':slide_durations(d)
  child=d.get('ChildAnimation')
  if child and child!='null':
   edges.append({'parent':name,'field':'ChildAnimation','child':child})
   if child not in names:names.append(child)
  if len(names)>4096:raise ValueError('Bounded GUI closure exceeded')
 return {n:resolved[n]for n in names},provenance,edges

def slide_durations(d):
 """Per-slide original DurationFrames of a Slide record (all slides kept)."""
 slides=d.get('Slides')
 if not isinstance(slides,list)or not slides:raise ValueError('Slide record without original Slides '+d['Name'])
 if any(not isinstance(x,dict)or set(x)-SLIDE_KEYS or 'DurationFrames'not in x for x in slides):raise ValueError('Unreviewed per-slide property '+d['Name'])
 out=[x['DurationFrames']for x in slides]
 if any(not isinstance(v,int)or not 0<=v<=100000 for v in out):raise ValueError('Slide duration outside bounds '+d['Name'])
 # SetupSlideIndex enters at StartFrame-1 then NextSlide; NextSlide's bound is
 # the slide-record count, so playback covers slides StartFrame..len(Slides).
 # Only StartFrame 1 and EndFrame absent/== len(Slides) are reviewed.
 if d.get('StartFrame',1)!=1 or d.get('EndFrame',len(out))!=len(out) or d.get('NumFrames',len(out))!=len(out):raise ValueError('Unreviewed Slide frame range '+d['Name'])
 if not any(out):raise ValueError('Slide record without positive duration '+d['Name'])
 # NextSlide Loop (0x1802af290) / PingPong (+0x77) wrap is not reviewed for GUI.
 if d.get('Loop'):raise ValueError('Unreviewed GUI Slide Loop '+d['Name'])
 if d.get('OffsetX',0)or d.get('OffsetY',0):raise ValueError('Unreviewed GUI Slide draw offset '+d['Name'])
 return out

def image_names(d):
 if d.get('Type')=='Slide':
  # Approximation boundary: zero-duration slides are never started (NextSlide
  # skips them), so only positive-duration slide images are exported. The
  # HoldLastFrame end state (NextSlide sets index=EndFrame) is enforced at
  # runtime (screen_animation.cpp fails loudly if it needs an unexported image).
  return [(i,d['FilePath']+f'{i:04}')for i,v in enumerate(slide_durations(d),1)if v]
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
  reviewed[name]='; '.join(prov[:3]) or 'coverage closure'
 graphics={g:'; '.join(p[:3]) for g,p in cov.get('obstacle_graphics',{}).items()}
 return reviewed,graphics,rejected,cov

def admissible(records,origin,names,rejected):
 """Drop (with reason) names whose own or ChildAnimation-closure record this consumer cannot represent."""
 ok=[]
 for name in names:
  try:
   d,_,_=resolve_all(records,origin,[name]);[image_names(x)for x in d.values()];ok.append(name)
  except ValueError as e:rejected[name]=str(e)
 return ok

def append_gui(a,m):
 records,origin,anim_sources=load_records(a.content)
 reviewed,graphics,rejected,cov=coverage_roots(a,records,origin)
 names=admissible(records,origin,sorted(reviewed),rejected)
 definitions,provenance,edges=resolve_all(records,origin,names)
 existing={c['name']for c in m['clips']}
 # Movie/static base clips keep their retained definitions.
 for n in list(definitions):
  if n in existing:rejected[n]='already in retained base screen bank';del definitions[n]
 graphics={g:p for g,p in graphics.items()if g not in existing and g not in definitions}
 clips=[]
 for name,d in definitions.items():clips.append((name,d,image_names(d)))
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
 if any(c[0]in rejected for c in clips):
  clips=[c for c in clips if c[0]not in rejected]
  # A parent whose ChildAnimation was dropped cannot be represented either.
  while True:
   kept={c[0]for c in clips};bad=[c for c in clips if c[1].get('ChildAnimation')not in (None,'null')and c[1].get('ChildAnimation')not in kept and c[1].get('ChildAnimation')not in existing]
   if not bad:break
   for c in bad:rejected[c[0]]='ChildAnimation not representable: '+c[1]['ChildAnimation']
   clips=[c for c in clips if c[0]not in rejected]
  definitions={c[0]:c[1]for c in clips if c[0]in definitions};graphics={g:p for g,p in graphics.items()if g in{c[0]for c in clips}}
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
 st=m['statistics'];st['clips']=nc+len(clip_rows);st['movie_frames']=nf+len(frame_rows);st['gui_original_images']=len(frame_rows);st['gui_raw_rgba_bytes']=sum(r['raw_bytes']for r in frame_rows)
 m['gui_review']={'coverage':str(Path(a.coverage).resolve()),'reviewed_animations':{n:reviewed[n]for n in definitions},'reviewed_graphics':graphics,'rejected':dict(sorted(rejected.items())),'closure_edges':edges,'admitted_keys':sorted(KEYS),'original_payloads':payloads,'rule':'InheritFrom resolved by native Read order (ClearSlides/ClearCreateAnimations, additive CreateAnimations); OriginX/Y baked into frame origin; Offset/Scale/ScaleX/ScaleY/Alpha/Red/Green/Blue/AddColor applied at runtime from the retained definition.'}
 return m
