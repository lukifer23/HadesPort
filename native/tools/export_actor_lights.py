#!/usr/bin/env python3
"""Export actual RoomOpening Thing.Light controllers and lossless source atlas images.

No original execution or edits. FX-owned lights are a different runtime owner.
"""
import argparse,copy,hashlib,json,struct,zlib
from pathlib import Path
from export_room_creation import RangedSjson
from export_actor_catalog import literal,identity
from export_actor_animations import string
from hades_assets import Package,xnb_image
TARGET='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'
def export(content,creation,output):
 if output.exists()or output.is_symlink():raise ValueError('Fresh light output required')
 if output.resolve().is_relative_to(content.resolve().parent):raise ValueError('Never write inside originals')
 source=json.loads(creation.read_bytes());actors={};names=[]
 for row in source['records']:
  if not row['raw_map']['DoCreate']or not row['raw_map']['Active']:continue
  name=source['definitions'][row['definition_key']]['value'].get('Thing',{}).get('Light','')
  if name:actors[str(row['id'])]=name
  if name and name not in names:names.append(name)
 animation_file=content/'Game/Animations/Fx.sjson';raw=animation_file.read_bytes();parser=RangedSjson(raw.decode('utf-8-sig'));records={d['Name']:d for d in parser.parse()['Animations']};resolved={};provenance={}
 def resolve(name,stack=()):
  if name in resolved:return resolved[name]
  if name in stack:raise ValueError('Light controller inheritance cycle')
  own=records[name];parent=own.get('InheritFrom');d=copy.deepcopy(resolve(parent,stack+(name,)))if parent and parent!='null'else{}
  if own.get('ClearSlides'):d['Slides']=[]
  if own.get('ClearCreateAnimations'):d['CreateAnimations']=[]
  creates=d.get('CreateAnimations',[]);d.update(copy.deepcopy(own))
  if 'CreateAnimations'in own:d['CreateAnimations']=creates+copy.deepcopy(own['CreateAnimations'])
  span=parser.ranges[id(own)];provenance[name]={'range':span,'sha256':hashlib.sha256(raw[span[0]:sum(span)]).hexdigest(),'source':own,'inherit_from':parent};resolved[name]=d;return d
 definitions={};images={};unsupported={}
 for name in names:
  named=name in records;d=resolve(name)if named else {'Name':name,'FilePath':name,'Type':'Dataless'}
  # Unreviewed shapes (multi-frame Book controllers, extra operations) are not
  # exported: they are listed in Unsupported so the runtime fails loudly for
  # exactly the owning actors instead of the whole room export failing.
  reason=None
  if d['Type']not in ['Constant','Dataless']or d.get('NumFrames',1)!=1 or d.get('NumAngles',1)!=1:reason='Unreviewed actor light controller shape Type=%s NumFrames=%s NumAngles=%s'%(d['Type'],d.get('NumFrames',1),d.get('NumAngles',1))
  for key in ['ChildAnimation','CreateAnimation','CreateAnimations','VisualFx','Light','Sound']:
   if reason is None and d.get(key):reason='Actual actor light has additional required operation '+key
  if reason:unsupported[name]=reason;continue
  definitions[name]={'Named':named,'Definition':d};images[name]=d['FilePath']
 names=[n for n in names if n not in unsupported]
 wanted=set(images.values());matches={};manifests=[];payloads=[];package_root=content/'Win/Packages'
 for package in ['Fx','Dev','RoomManager','Tartarus']:
  path=package_root/(package+'.pkg_manifest');manifests.append(identity(path))
  for entry in Package(path).entries():
   for sprite in entry.get('sprites',[]):
    if sprite['name']not in wanted:continue
    if sprite['flags']&3:raise ValueError('Multipart actor light texture unsupported')
    old=matches.get(sprite['name']);record={'package':package,'atlas':entry['name'],'sprite':sprite}
    if old is None or sum(abs(x-1)for x in sprite['scale_ratio'])<sum(abs(x-1)for x in old['sprite']['scale_ratio']):matches[sprite['name']]=record
 if wanted-set(matches):raise ValueError('Original actor light graphics missing '+repr(wanted-set(matches)))
 atlases={}
 for package in sorted({x['package']for x in matches.values()}):
  path=package_root/(package+'.pkg')
  for entry in Package(path).entries():
   if entry.get('name')not in {x['atlas']for x in matches.values()if x['package']==package}:continue
   im,metadata=xnb_image(entry['data']);atlases[(package,entry['name'])]=im;payloads.append({'package':identity(path),'atlas':entry['name'],'location':entry['location'],'xnb_sha256':hashlib.sha256(entry['data']).hexdigest(),'decoded':metadata})
 frames=[];indices={}
 for image,match in sorted(matches.items()):
  s=match['sprite'];x,y,w,h=s['rect'];rx,ry=s['scale_ratio'];im=atlases[(match['package'],match['atlas'])]
  if min(x,y)<0 or min(w,h,rx,ry)<=0 or max(w,h)>4096 or x+w>im.width or y+h>im.height:raise ValueError('Original light atlas bounds')
  pixels=im.crop((x,y,x+w,y+h)).tobytes();indices[image]=len(frames);frames.append({'name':image,**match,'origin':[s['original'][0]/2/rx-s['trim'][0],s['original'][1]/2/ry-s['trim'][1]],'rgba_sha256':hashlib.sha256(pixels).hexdigest(),'rgba':pixels,'packed':zlib.compress(pixels,6)})
 output.mkdir(parents=True);packet=output/'actor-light-frames.bin';clips=[]
 with packet.open('xb')as out:
  out.write(b'HAA1'+struct.pack('<II',len(frames),len(names)))
  for i,f in enumerate(frames):
   w,h=f['sprite']['rect'][2:];out.write(struct.pack('<8I2f',w,h,0,0,w,h,i,0,*f['origin']));out.write(struct.pack('<III',len(f['rgba']),len(f['packed']),zlib.crc32(f['rgba'])));out.write(f['packed'])
  for name in names:
   for text in [name,'','']:string(out,text)
   out.write(struct.pack('<4I4f',1,1,0,1,1.,0.,0.,1/60));out.write(struct.pack('<I',0));out.write(struct.pack('<III',1,1,indices[images[name]]));string(out,'{}')
   clips.append({'Name':name,'Index':len(clips),'Frame':indices[images[name]],'AtlasRatio':matches[images[name]]['sprite']['scale_ratio']})
 catalog={'Schema':'HadesActorLightResources1','Target':TARGET,'ActorLights':actors,'Definitions':definitions,'Unsupported':unsupported,'Clips':clips,'Frames':[{k:v for k,v in f.items()if k not in ['rgba','packed']}for f in frames],'ImageBankSha256':identity(packet)['sha256'],'Limits':['Data/resource export does not claim light execution or native Windows shadow shader equivalence.','Constant LightTorchFlicker retains all real RNG/loop/color fields; runtime must execute its native source phases.','Dataless lights are original texture fallback, not fabricated animation definitions.']}
 (output/'actor-lights.lua').write_text('return '+literal(catalog)+'\n');(output/'catalog.json').write_text(json.dumps(catalog,indent=2)+'\n')
 manifest={'schema':'hades-actor-light-resources-v1','unsupported_controllers':unsupported,'target_sha256':TARGET,'creation':identity(creation),'animation_source':identity(animation_file),'manifests':manifests,'record_provenance':provenance,'payloads':payloads,'packet':identity(packet),'catalog':identity(output/'catalog.json'),'lua':identity(output/'actor-lights.lua'),'actors':len(actors),'controllers':len(definitions),'original_images':len(frames),'raw_rgba_bytes':sum(len(f['rgba'])for f in frames)}
 (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');return {k:manifest[k]for k in ['packet','lua','actors','controllers','original_images','raw_rgba_bytes']}
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for k in ['content','creation','output']:p.add_argument('--'+k,type=Path,required=True)
 a=p.parse_args();print(json.dumps(export(a.content,a.creation,a.output),indent=2))
