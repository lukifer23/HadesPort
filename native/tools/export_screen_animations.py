#!/usr/bin/env python3.13
"""Original Remembrance UI Bink data, independent streamed HAA1; no synthetic UI art."""
import argparse,json,struct,zlib,hashlib,shutil
from io import BytesIO
from hades_assets import Package,xnb_image
from export_actor_animations import string
from pathlib import Path
from export_actor_animations import export
from export_effect_initialization import lua
from build_room_scene import identity
p=argparse.ArgumentParser(description=__doc__)
for key in ['content','decoder','output']:p.add_argument('--'+key,type=Path,required=True)
p.add_argument('--base-manifest',type=Path,help='Reuse a retained original movie export in a fresh directory, adding reviewed static screen resources.')
p.add_argument('--gui-review',action='store_true',help='Also admit the coverage-derived original GUIAnimations/Thing.Graphic closure (screen_gui_review.py).')
p.add_argument('--coverage',type=Path,help='Static animation coverage report (animation_coverage.py) for --gui-review.')
a=p.parse_args();a.source=a.content/'Game/Animations/ObstacleAnimations.sjson';a.names=['RemBGIntro','RemBG'];a.unit_scale=1.;a.directions=1;a.allow_unused_movie_tail=True;a.movie_directory=a.content/'Movies/720p'
if a.base_manifest:
 if a.output.exists():raise ValueError('Fresh output required')
 base=json.loads(a.base_manifest.read_text());packet=Path(base['packet']['path'])
 if hashlib.sha256(packet.read_bytes()).hexdigest()!=base['packet']['sha256']:raise ValueError('Retained source screen bank identity')
 a.output.mkdir(parents=True);shutil.copyfile(packet,a.output/'actor-animations.bin');(a.output/'manifest.json').write_text(json.dumps(base))
else:export(a)
m=json.loads((a.output/'manifest.json').read_text())
# Dataless rectangle and actual ConstantNoUpdate Blank source image.
from build_room_scene import Sjson
blank_source=a.content/'Game/Animations/1_DevAnimations.sjson'
blank=next(r for r in Sjson(blank_source.read_text()).parse()['Animations']if r['Name']=='Blank')
m['sources'].append(identity(blank_source))
gui_source=a.content/'Game/Animations/GUIAnimations.sjson'
vignette=next(r for r in Sjson(gui_source.read_text()).parse()['Animations']if r['Name']=='VignetteOverlay')
m['sources'].append(identity(gui_source))
static=[('Backgrounds\\rectangle_01','Backgrounds\\rectangle_01',{'Name':'Backgrounds\\rectangle_01','Type':'Dataless','FilePath':'Backgrounds\\rectangle_01','NumFrames':1,'Loop':False}),('Blank',blank['FilePath'],blank),('VignetteOverlay',vignette['FilePath'],vignette)]
for animation_name,wanted,definition in static:
 if any(c['name']==animation_name for c in m['clips']):continue
 found=None
 for pkg in ['RoomManager','Dev','Fx','GUI']:
  manifest=a.content/'Win/Packages'/(pkg+'.pkg_manifest')
  for entry in Package(manifest).entries():
   for sprite in entry.get('sprites',[]):
    if sprite['name']==wanted:found=(pkg,entry['name'],sprite,manifest);break
   if found:break
  if found:break
 if not found:raise ValueError('Actual screen rectangle texture unavailable')
 pkg,atlas,sprite,manifest=found
 entry=next(e for e in Package(a.content/'Win/Packages'/(pkg+'.pkg')).entries()if e.get('name')==atlas)
 image,info=xnb_image(entry['data']);x,y,w,h=sprite['rect'];rx,ry=sprite['scale_ratio']
 if sprite['flags']&3 or min(x,y)<0 or min(w,h,rx,ry)<=0 or x+w>image.width or y+h>image.height:raise ValueError('Screen rectangle source bounds')
 raw=image.crop((x,y,x+w,y+h)).tobytes();packed=zlib.compress(raw,6);origin=[sprite['original'][0]/2/rx-sprite['trim'][0],sprite['original'][1]/2/ry-sprite['trim'][1]]
 packet=a.output/'actor-animations.bin';old=packet.read_bytes();nf,nc=struct.unpack_from('<II',old,4);cursor=12
 for _ in range(nf):cursor+=52+struct.unpack_from('<I',old,cursor+44)[0]
 f=BytesIO();f.write(b'HAA1'+struct.pack('<II',nf+1,nc+1));f.write(old[12:cursor]);f.write(struct.pack('<8I2f',w,h,0,0,w,h,0,0,*origin));f.write(struct.pack('<III',len(raw),len(packed),zlib.crc32(raw)));f.write(packed);f.write(old[cursor:])
 for name in [animation_name,'','']:string(f,name)
 f.write(struct.pack('<4I4f',1,1,512,1,rx,0.,0.,1/60));f.write(struct.pack('<IIII',0,1,1,nf));string(f,'{}');packet.write_bytes(f.getvalue())
 m['packet']=identity(packet);m['sources'].append(identity(manifest));m.setdefault('screen_static_graphics',[]).append({'name':animation_name,'package':pkg,'atlas':atlas,'sprite':sprite,'rgba_sha256':hashlib.sha256(raw).hexdigest(),'raw_rgba_bytes':len(raw)})
 m['clips'].append({'name':animation_name,'definition':definition,'source_sequence':[]})
 m['statistics']['clips']+=1;m['statistics']['movie_frames']+=1
if a.gui_review:
 if not a.coverage:raise ValueError('--gui-review requires --coverage')
 from screen_gui_review import append_gui
 m=append_gui(a,m)
catalog={'schema':'hades-screen-animations-v1','bank_sha256':m['packet']['sha256'],'target_sha256':'d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254','clips':{c['name']:{'definition':c['definition'],'source_sequence':c['source_sequence']}for c in m['clips']}}
(a.output/'screen-animations.lua').write_text('return '+lua(catalog)+'\n')
(a.output/'manifest.json').write_text(json.dumps({**m,'screen_catalog':identity(a.output/'screen-animations.lua'),'screen_coordinates':'Original script ScreenCenterX/Y from 1920x1080 reference. UnitScale1; no world Z projection or 0.7 hero scale.'},indent=2)+'\n')
