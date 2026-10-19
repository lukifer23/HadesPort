#!/usr/bin/env python3
"""Export original Laurel atlas frames to a fresh compact HAA1 image bank.
Timing remains original source data for the live effect initializer, not baked
random durations or simulated particles.
"""
import argparse,hashlib,json,struct,zlib
from pathlib import Path
from build_room_scene import identity
from hades_assets import Package,xnb_image
from export_actor_animations import string

def export(a):
 if a.output.exists():raise ValueError('Fresh cinder output required')
 deps=json.loads(a.dependencies.read_text());source=Path(deps['source']['path'])
 if identity(source)['sha256']!=deps['source']['sha256']:raise ValueError('Original FX source changed')
 definitions=[r for r in deps['records']if r['name'].startswith('LaurelBurn')]
 packages=a.content/'Win/Packages';manifest=packages/'Fx.pkg_manifest';sprites={};atlases={}
 for e in Package(manifest).entries():
  for s in e.get('sprites',[]):
   if any(s['name'].startswith(d['resolved']['FilePath'])for d in definitions):
    if s['name']in sprites:raise ValueError('Ambiguous original sprite')
    sprites[s['name']]={**s,'atlas':e['name']};atlases[e['name']]=None
 for d in definitions:
  f=d['resolved']
  if f['NumFrames']!=28 or f['Type']not in ('Slide','Book'):raise ValueError('Unexpected original cinder shape')
  for i in range(1,29):
   if f['FilePath']+f'{i:04}'not in sprites:raise ValueError('Missing exact cinder sprite')
 used={s['atlas']for s in sprites.values()};atlas_sources=[]
 for e in Package(packages/'Fx.pkg').entries():
  if e['type']=='texture'and e['name']in used:
   if atlases[e['name']]is not None:raise ValueError('Ambiguous original texture')
   im,info=xnb_image(e['data']);atlases[e['name']]=im;atlas_sources.append({'name':e['name'],'xnb_sha256':hashlib.sha256(e['data']).hexdigest(),'location':e['location'],'image':info})
 if any(im is None for im in atlases.values()):raise ValueError('Missing original atlas payload')
 a.output.mkdir(parents=True);frames=[];lookup={};clips=[];raw_total=0;packed_total=0
 for name,s in sorted(sprites.items()):
  x,y,w,h=s['rect'];im=atlases[s['atlas']];ow,oh=s['original'];tx,ty=s['trim'];ratio=s['scale_ratio']
  if ratio!=[1.,1.]or min(x,y,tx,ty)<0 or x+w>im.width or y+h>im.height:raise ValueError('Unreviewed original cinder transform')
  rgba=im.crop((x,y,x+w,y+h)).tobytes();packed=zlib.compress(rgba,6);idx=len(frames);lookup[name]=idx;frames.append({'index':idx,'sprite':s,'rgba':rgba,'packed':packed,'crc':zlib.crc32(rgba),'origin':[ow*.5-tx,oh*.5-ty],'raw_sha256':hashlib.sha256(rgba).hexdigest()});raw_total+=len(rgba);packed_total+=len(packed)
 for row in definitions:
  d=row['resolved'];clips.append({'name':row['name'],'source_definition':d,'frame_ids':[lookup[d['FilePath']+f'{i:04}']for i in range(1,29)],'type':d['Type']})
 packet=a.output/'cinder-frames.bin'
 with packet.open('xb')as out:
  out.write(b'HAA1'+struct.pack('<II',len(frames),len(clips)))
  for f in frames:
   s=f['sprite'];w,h=s['rect'][2:];frame=int(s['name'][-4:])-1
   out.write(struct.pack('<8I2f',w,h,0,0,w,h,frame,0,*f['origin']));out.write(struct.pack('<III',len(f['rgba']),len(f['packed']),f['crc']));out.write(f['packed'])
  for c in clips:
   for value in [c['name'],'','']:string(out,value)
   out.write(struct.pack('<4I4f',1,1,512 if c['type']=='Book'else 0,28,1.,0.,0.,1/60));out.write(struct.pack('<I',0))
   for i,idx in enumerate(c['frame_ids'],1):out.write(struct.pack('<III',i,1,idx));string(out,'{}')
 report={'schema':'hades-original-cinder-frame-bank-v1','packet':identity(packet),'sources':[identity(source),identity(manifest),identity(packages/'Fx.pkg')],'dependencies':identity(a.dependencies),'atlas_sources':atlas_sources,'clips':clips,'frames':[{k:v for k,v in f.items()if k not in('rgba','packed')}for f in frames],'raw_bytes':raw_total,'compressed_bytes':packed_total,'timing':'Bank nominal ticks1/60 only; original randomized Slide duration and Book play-speed must come from source live initializer. Do not use deterministic ActorAnimationPlayback as cinder execution.','limits':['Original pixel frames/pivots only; no particle motion, RNG or lifetime synthesized.','Original selected atlas scale ratios exactly1, ten source definitions share140 images.','Live original scale/angle/Z/offset/easing/visibility/sort are separate native state.']}
 (a.output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'packet':report['packet'],'frames':len(frames),'clips':len(clips),'compressed_bytes':packed_total}))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--content',type=Path,required=True);p.add_argument('--dependencies',type=Path,required=True);p.add_argument('--output',type=Path,required=True);export(p.parse_args())
