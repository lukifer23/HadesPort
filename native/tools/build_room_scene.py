#!/usr/bin/env python3.13
"""Resolve original Hades room placements to original PKG texture crops.

Produces portable RGBA/PNG assets and HDS1 scene for the native renderer.
This is static original scene reconstruction, not gameplay emulation.
"""
import argparse,collections,hashlib,json,math,re,struct
from pathlib import Path
from hades_assets import Package,map_objects,xnb_image
class Sjson:
 def __init__(self,text):self.text=text;self.pos=0
 def skip(self):
  while self.pos<len(self.text):
   if self.text[self.pos].isspace()or self.text[self.pos]in ',;':self.pos+=1
   elif self.text.startswith('//',self.pos):
    p=self.text.find('\n',self.pos);self.pos=len(self.text)if p<0 else p+1
   elif self.text.startswith('/*',self.pos):
    p=self.text.find('*/',self.pos+2)
    if p<0:raise ValueError('Unterminated comment')
    self.pos=p+2
   else:break
 def token(self):
  self.skip()
  if self.pos>=len(self.text):raise ValueError('Unexpected SJSON EOF')
  c=self.text[self.pos];self.pos+=1
  if c in '{}[]=:\\':return c
  if c=='"':
   out=[]
   while self.pos<len(self.text):
    c=self.text[self.pos];self.pos+=1
    if c=='"':return ('s',''.join(out))
    if c=='\\'and self.pos<len(self.text)and self.text[self.pos]in ('"','\\'):c=self.text[self.pos];self.pos+=1
    out.append(c)
   raise ValueError('Unterminated SJSON string')
  start=self.pos-1
  while self.pos<len(self.text)and not self.text[self.pos].isspace()and self.text[self.pos]not in '{}[]=,:;':self.pos+=1
  return ('t',self.text[start:self.pos])
 def value(self,t=None):
  t=self.token()if t is None else t
  if t=='{':
   obj={}
   while True:
    key=self.token()
    if key=='}':return obj
    if not isinstance(key,tuple):raise ValueError('Invalid SJSON key')
    if self.token()not in ('=',':'):raise ValueError('Missing SJSON assignment')
    obj[key[1]]=self.value()
  if t=='[':
   arr=[]
   while True:
    v=self.token()
    if v==']':return arr
    arr.append(self.value(v))
  if not isinstance(t,tuple):raise ValueError('Unexpected token '+str(t))
  if t[0]=='s':return t[1]
  if t[1]=='true':return True
  if t[1]=='false':return False
  if t[1]=='null':return None
  try:return float(t[1])if any(c in t[1]for c in '.eE')else int(t[1])
  except ValueError:return t[1]
 def parse(self):
  obj=self.value();self.skip()
  if self.pos!=len(self.text):raise ValueError('Unparsed SJSON')
  return obj
def merge(a,b):
 r=dict(a)
 for k,v in b.items():r[k]=merge(r[k],v)if isinstance(v,dict)and isinstance(r.get(k),dict)else v
 return r
def map_packages(content,room):
 """Original MapGroups.sjson: the map group whose Maps glob names this map.

 The group name is the biome package (Tartarus.pkg, DeathArea.pkg, ...);
 the engine reads MapGroups.sjson in GameDataManager (string "MapGroups.sjson").
 Fx/GUI global residency is supplied by the caller and labeled there.
 """
 import fnmatch
 groups=Sjson((content/'Game/MapGroups.sjson').read_text(encoding='utf-8-sig')).parse()['MapGroups']
 found=[g['Name']for g in groups if any(fnmatch.fnmatchcase(room,m['Name'])for m in g.get('Maps',[]))]
 if len(found)!=1:raise ValueError(f'Map {room} matches {len(found)} original MapGroups')
 if not (content/'Win/Packages'/f'{found[0]}.pkg_manifest').is_file():raise ValueError('Map group package absent: '+found[0])
 return found
def identity(p):return {'path':str(p.resolve()),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
def build(content,out,room):
 if out.exists():raise ValueError('Output must be new')
 out.mkdir(parents=True);(out/'textures').mkdir()
 sources=[];defs={};failures=[]
 for p in sorted((content/'Game/Obstacles').glob('*.sjson')):
  try:d=Sjson(p.read_text(encoding='utf-8-sig')).parse()
  except Exception as e:failures.append({'source':str(p),'error':str(e)});continue
  sources.append(identity(p))
  for ob in d.get('Obstacles',[]):defs[ob['Name']]=ob
 def resolve(name,seen=()):
  if name in seen:raise ValueError('Obstacle inheritance cycle')
  own=defs.get(name,{})
  return merge(resolve(own['InheritFrom'],seen+(name,)),own)if isinstance(own.get('InheritFrom'),str)else own
 mappath=content/'Win/Maps'/f'{room}.thing_bin';metapath=content/'Maps'/f'{room}.map_text'
 objs=map_objects(mappath);meta=json.loads(metapath.read_text());sources.extend([identity(mappath),identity(metapath)])
 package=map_packages(content,room)[0];manifest=content/'Win/Packages'/f'{package}.pkg_manifest';pkgpath=content/'Win/Packages'/f'{package}.pkg';sources.extend([identity(manifest),identity(pkgpath),identity(content/'Game/MapGroups.sjson')])
 manifests=list(Package(manifest).entries());index=collections.defaultdict(list)
 for atlas in manifests:
  for s in atlas.get('sprites',[]):
   if not s['flags']&2:index[s['name']].append((atlas['name'],s))
 # map_text order supplies an explicit, source-bound layer order. Standing
 # receives map Y sort; exact native engine occlusion/parallax is not recovered.
 group_order={g['Id']['Name']:i for i,g in enumerate(meta['ThingGroups'])}
 placements=[];unresolved=collections.Counter();allobjects=[]
 for obj in objs:
  definition=resolve(obj['Name']);thing=definition.get('Thing',{});graphic=thing.get('Graphic');groups=[x for x in obj['GroupNames']if x]
  matches=index.get(graphic,[])
  record={'object':obj,'graphic':graphic,'obstacle_thing':thing,'static_sprite_matches':len(matches)};allobjects.append(record)
  if not obj['DoCreate'] or not obj['Active'] or thing.get('Invisible',False):continue
  if not matches:unresolved[obj['Name']]+=1;continue
  # Non-multi duplicate records are alternate variants; use largest stored crop.
  if len(matches)>1 and not any(s['flags']&1 for _,s in matches):matches=[max(matches,key=lambda a:a[1]['rect'][2]*a[1]['rect'][3])]
  for tex,s in matches:
   placements.append({'object':obj,'thing':thing,'graphic':graphic,'texture':tex,'sprite':s,'layer':min((group_order.get(g,999)for g in groups),default=999)})
 needed={p['texture']for p in placements};atlasimages={};atlasmeta={}
 for entry in Package(pkgpath).entries():
  if entry.get('type')=='texture'and entry['name']in needed:
   im,info=xnb_image(entry['data']);atlasimages[entry['name']]=im;atlasmeta[entry['name']]={**info,'xnb_sha256':hashlib.sha256(entry['data']).hexdigest()}
 textures=[];texindex={};sprites=[];sprite_manifest=[]
 placements.sort(key=lambda p:(p['layer'],p['object']['SortIndex'],p['object']['Location']['Y']))
 for p in placements:
  ob=p['object'];s=p['sprite'];atlas=atlasimages.get(p['texture'])
  if atlas is None:unresolved[ob['Name']]+=1;continue
  x,y,w,h=s['rect']
  if min(x,y,w,h)<0 or x+w>atlas.width or y+h>atlas.height:raise ValueError('Atlas crop out of bounds')
  key=(p['texture'],x,y,w,h)
  if key not in texindex:
   n=len(textures);name=f'{n:03d}-{hashlib.sha256(s["name"].encode()).hexdigest()[:10]}'
   crop=atlas.crop((x,y,x+w,y+h));rgba=crop.tobytes();(out/'textures'/f'{name}.rgba').write_bytes(rgba);crop.save(out/'textures'/f'{name}.png')
   texindex[key]=n;textures.append({'index':n,'width':w,'height':h,'rgba':rgba,'png':f'textures/{name}.png','rgba_path':f'textures/{name}.rgba','sha256':hashlib.sha256(rgba).hexdigest(),'graphic':s['name'],'atlas':p['texture'],'atlas_rect':s['rect']})
  scale=ob['Scale'];ratio=s['scale_ratio'];sw=w*ratio[0]*scale;sh=h*ratio[1]*scale
  # Standard sprite origin interpretation, flagged as provisional below.
  trim=s['trim'];original=s['original'];ox=(trim[0]*ratio[0]+w*ratio[0]/2-original[0]/2)*scale;oy=(trim[1]*ratio[1]+h*ratio[1]/2-original[1]/2)*scale
  offset=p['thing'].get('Offset',{});ox+=offset.get('X',0)*scale;oy+=offset.get('Y',0)*scale
  angle=math.radians(ob['Angle']);c=math.cos(angle);ss=math.sin(angle)
  cx=ob['Location']['X']+ox*c-oy*ss;cy=ob['Location']['Y']+ox*ss+oy*c-ob['OffsetZ']
  u0,u1=(1.,0.)if ob['FlipHorizontal']else(0.,1.);v0,v1=(1.,0.)if ob['FlipVertical']else(0.,1.)
  row=(texindex[key],cx,cy,sw,sh,u0,v0,u1,v1,angle,ob['Color'][3]/255.)
  sprites.append(row);sprite_manifest.append({'object_id':ob['Id'],'name':ob['Name'],'graphic':p['graphic'],'group_names':ob['GroupNames'],'source_layer':p['layer'],'draw':row,'color_rgba':ob['Color'],'angle_degrees':ob['Angle'],'skew_angle':ob['SkewAngle'],'skew_scale':ob['SkewScale']})
 for filename,magic in [('scene.bin',b'HDS1'),('scene-tinted.bin',b'HDS2')]:
  with(out/filename).open('xb')as f:
   f.write(magic+struct.pack('<II',len(textures),len(sprites)))
   for t in textures:f.write(struct.pack('<II',t['width'],t['height']));f.write(t['rgba'])
   for row,record in zip(sprites,sprite_manifest):
    f.write(struct.pack('<I10f',*row))
    if magic==b'HDS2':f.write(struct.pack('<3f',*(v/255 for v in record['color_rgba'][:3])))
 result={'room':room,'format':'HDS1 little-endian; textureCount,spriteCount; eachtexture w,h,RGBA8; eachsprite uint32textureIndex +10floats centerX,centerY,width,height,u0,v0,u1,v1,rotationRadians,alpha','sources':sources,'room_objects':len(objs),'rendered_static_instances':len(sprites),'textures':[{k:v for k,v in t.items()if k!='rgba'}for t in textures],'sprites':sprite_manifest,'unresolved_objects':dict(unresolved),'definition_parse_failures':failures,'all_original_objects':allobjects,'decoded_atlases':atlasmeta,'limitations':['Static scene only; no original game logic executed.','Draw order uses map_text group order,SortIndex and Y; engine render order not yet established.','Object pivot/trim,rotation and OffsetZ projection are provisional interpretations of original fields.','HDS1 lacks RGB tint; HDS2 preserves original per-object RGB. Blend modes, skew/parallax, animated effects and custom shaders are preserved in metadata but not rendered.','Animated and absent-package sprites are unresolved, not replaced with synthetic assets.'],'scene':identity(out/'scene.bin'),'tinted_scene':identity(out/'scene-tinted.bin'),'tinted_format':'HDS2 adds RGB float multipliers after each original HDS1 sprite record.'}
 (out/'scene-manifest.json').write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps({k:result[k]for k in('room','room_objects','rendered_static_instances','unresolved_objects','definition_parse_failures','scene')}))
 return result
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--content',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--room',default='RoomOpening');a=p.parse_args();build(a.content,a.output,a.room)
