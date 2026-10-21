#!/usr/bin/env python3.13
"""Extend the stable original room with original FX books and diagnostics.

New output only. Preserves source pivots/material fields independently of the
provisional projection. HDS3 is four corner vertices plus original tint/blend.
"""
import argparse,collections,hashlib,json,math,shutil,struct
from pathlib import Path
from PIL import Image
from hades_assets import Package,xnb_image,map_objects
from build_room_scene import Sjson,merge,identity,map_packages
CAMERA_Z_SCALE=math.sqrt(3)/2 # Original gCamRotation/CamRotation X rotation -60deg.

def native_axis_stretch(ob):
 # Native SquashAndStretch scale-1 formula reduces to local X*scale when
 # sin(skewAngle)==0, for both Flat and Vertical meshes, independent of YOffset.
 angle=math.radians(ob['SkewAngle'])
 return ob['SkewScale']if abs(math.sin(angle))<1e-6 else 1.

def project_quad(draw,ob,post_x=1,post_y=1,*,anchor=None,mesh_type='Flat',y_offset=0.,full_stretch=False):
 _,cx,cy,w,h,_,_,_,_,angle,_=draw;c,ss=math.cos(angle),math.sin(angle);anchor=anchor or(ob['Location']['X'],ob['Location']['Y']-ob['OffsetZ']*CAMERA_Z_SCALE);stretch=native_axis_stretch(ob);result=[]
 for x,y in((-w/2,-h/2),(w/2,-h/2),(-w/2,h/2),(w/2,h/2)):
  dx=cx-anchor[0]+x*c-y*ss;dy=cy-anchor[1]+x*ss+y*c
  if full_stretch:
   # Native SquashAndStretch occurs in unrotated mesh x/z, before source
   # rotation and postRotateScale. Flat mesh feeds nativeY/2; Vertical feeds
   # nativeZ (its image-Y displacement /sin60). Then project native Z back.
   lx,ly=dx*c+dy*ss,-dx*ss+dy*c;sk=math.radians(ob['SkewAngle']);sc,sn=math.cos(sk),math.sin(sk);z=ly/CAMERA_Z_SCALE if mesh_type=='Vertical'else ly
   delta=(ob['SkewScale']-1)*(sc*lx-sn*z+sn*y_offset);lx+=sc*delta;ly-=CAMERA_Z_SCALE*sn*delta;dx=lx*c-ly*ss;dy=lx*ss+ly*c
  else:
   delta=(stretch-1)*(dx*c+dy*ss);dx+=delta*c;dy+=delta*ss
  result.append([anchor[0]+dx*post_x,anchor[1]+dy*post_y])
 return result

def atlas_center(sprite,origin,flip_x,flip_y):
 """Native AtlasMap::GetOrigin arithmetic; default input origin provisional.

 Matching GetOrigin VA1801dde30 and PDB AtlasMap members establish inverse
 scale, trimmed origin and integer-truncated flip correction order.
 """
 result=[]
 for axis,flip in enumerate((flip_x,flip_y)):
  ratio=sprite['scale_ratio'][axis];trim=sprite['trim'][axis];size=sprite['rect'][2+axis]
  native_origin=origin[axis]/ratio-trim
  if flip:native_origin-=math.trunc(sprite['original'][axis]/ratio-size-2*trim)
  result.append((size/2-native_origin)*ratio)
 return result

def build(content,base,out):
 if out.exists():raise ValueError('Output must be new')
 out.mkdir(parents=True);(out/'textures').mkdir();(out/'animations').mkdir()
 m=json.loads((base/'scene-manifest.json').read_text());sources=list(m['sources']);defs={}
 # Stable v2 predates native proof of BGRA-on-disk. Reparse the same original
 # records rather than treating that old manifest's label as authenticated.
 actual={o['Id']:o for o in map_objects(content/'Win/Maps'/f'{m["room"]}.thing_bin')}
 for rec in m['all_original_objects']:
  old=rec['object'];new=actual[old['Id']]
  if old['source_range']!=new['source_range']:raise ValueError('Stable map/source ranges differ')
  rec['object']=new
 for s in m['sprites']:s['color_rgba']=actual[s['object_id']]['Color']
 for p in sorted((content/'Game/Animations').glob('*.sjson')):
  for d in Sjson(p.read_text(encoding='utf-8-sig')).parse().get('Animations',[]):defs[d['Name']]=d
  sources.append(identity(p))
 def resolve(n,seen=()):
  if n in seen:raise ValueError('Animation inheritance cycle')
  d=defs.get(n,{})
  return merge(resolve(d['InheritFrom'],seen+(n,)),d)if isinstance(d.get('InheritFrom'),str)else d
 meta=json.loads((content/'Maps'/f'{m["room"]}.map_text').read_text());bygroup={g['Id']['Name']:g for g in meta['ThingGroups']};flattened=[];visited=set()
 def append_group(g):
  name=g['Id']['Name']
  if name in visited:raise ValueError('Cyclic or repeated group')
  visited.add(name)
  for child in reversed(g['ChildGroups']):
   actual_child=bygroup[child['Name']]
   if actual_child['Id']!=child or actual_child['ParentGroup']!=g['Id']:raise ValueError('Child/parent identity mismatch')
   append_group(actual_child)
  flattened.append(g)
 for g in meta['ThingGroups']:
  if g['ParentGroup']['Id']==0:append_group(g)
 if len(visited)!=len(bygroup):raise ValueError('Orphan group')
 groups={g['Id']['Name']:(i,g)for i,g in enumerate(flattened)}
 index=collections.defaultdict(list);paths={}
 for package in (*map_packages(content,m['room']),'Fx','GUI'):
  man=content/'Win/Packages'/f'{package}.pkg_manifest';paths[package]=content/'Win/Packages'/f'{package}.pkg'
  sources.extend([identity(man),identity(paths[package])])
  for a in Package(man).entries():
   for s in a.get('sprites',[]):
    if not s['flags']&2:index[s['name']].append({'package':package,'atlas':a['name'],'sprite':s,'atlas_location':a['location']})
 # Reconstruct flipped trim pivots with native GetOrigin arithmetic. Legacy
 # scene v2 reversed UVs but retained the unflipped crop center.
 for s in m['sprites']:
  ob=actual[s['object_id']];thing=next(r['obstacle_thing']for r in m['all_original_objects']if r['object']['Id']==ob['Id']);texture=m['textures'][s['draw'][0]]
  candidates=[r for r in index[s['graphic']]if r['atlas']==texture['atlas']and r['sprite']['rect']==texture['atlas_rect']]
  if not candidates:raise ValueError('Stable crop no longer resolves')
  atlas=candidates[0]['sprite'];origin=[v/2 for v in atlas['original']];ox,oy=atlas_center(atlas,origin,ob['FlipHorizontal'],ob['FlipVertical']);scale=ob['Scale']*thing.get('Scale',1);offset=thing.get('Offset',{});ox*=scale;oy*=scale;a=math.radians(ob['Angle']);c,ss=math.cos(a),math.sin(a);anchor=[ob['Location']['X']+offset.get('X',0)*scale,ob['Location']['Y']+offset.get('Y',0)*scale-ob['OffsetZ']*CAMERA_Z_SCALE]
  d=list(s['draw']);d[1]=anchor[0]+ox*c-oy*ss;d[2]=anchor[1]+ox*ss+oy*c;d[3]=atlas['rect'][2]*atlas['scale_ratio'][0]*scale;d[4]=atlas['rect'][3]*atlas['scale_ratio'][1]*scale;s['draw']=d;s['draw_anchor']=anchor;s['native_transform_profile']='v5';s['source_pivot']={'trim':atlas['trim'],'original':atlas['original'],'origin':origin,'scale_ratio':atlas['scale_ratio'],'frame_rect':atlas['rect'],'flags':atlas['flags'],'native_flip_origin':True}
 textures=list(m['textures']);sprites=list(m['sprites']);clips={};coverage=[];needed=collections.defaultdict(list)
 for t in textures:
  for key in ('png','rgba_path'):shutil.copyfile(base/t[key],out/t[key])
 for rec in m['all_original_objects']:
  ob=rec['object'];thing=rec['obstacle_thing'];graphic=rec['graphic'];anim=resolve(graphic);status=''
  if not ob['DoCreate']:status='not_created'
  elif not ob['Active']:status='inactive'
  elif thing.get('Invisible',False):status='definition_invisible'
  elif anim.get('Alpha')==0:status='animation_transparent_fx_spawner'if anim.get('VisualFx')else'animation_transparent'
  elif rec['static_sprite_matches']:status='static_resolved'
  elif anim.get('Type')in('Book','Constant'):
   if graphic not in clips:
    clip={'name':graphic,'definition':anim,'frames':[],'missing_frames':[]}
    for f in range(int(anim.get('StartFrame',1)),int(anim.get('EndFrame',anim.get('NumFrames',1)))+1):
     name=anim['FilePath']+(f'{f:04d}'if anim['Type']=='Book'else'');matches=index.get(name,[])
     if not matches:clip['missing_frames'].append(name);continue
     if any(a['sprite']['flags']&1 for a in matches):clip['missing_frames'].append(name+' [multipart unsupported]');continue
     chosen=max(matches,key=lambda a:a['sprite']['rect'][2]*a['sprite']['rect'][3]);fr={'frame':f,'graphic':name,**chosen};clip['frames'].append(fr);needed[(chosen['package'],chosen['atlas'])].append((graphic,fr))
    clips[graphic]=clip
   status=anim['Type'].lower()+('_resolved'if clips[graphic]['frames']and not clips[graphic]['missing_frames']else'_incomplete')
  elif anim:status='unsupported_animation_'+str(anim.get('Type','unspecified'))
  else:status='missing_graphic_definition_or_package'
  coverage.append({'id':ob['Id'],'name':ob['Name'],'graphic':graphic,'status':status,'map_record':ob['source_range'],'animation_type':anim.get('Type'),'groups':ob['GroupNames']})
 decoded=[];unsupported=[]
 for package,pkg in paths.items():
  for e in Package(pkg).entries():
   key=(package,e.get('name'))
   if key not in needed or e['type']!='texture':continue
   try:im,info=xnb_image(e['data'])
   except ValueError as error:
    unsupported.append({'package':package,'atlas':e['name'],'error':str(error),'location':e['location'],'xnb_sha256':hashlib.sha256(e['data']).hexdigest()});continue
   decoded.append({'package':package,'atlas':e['name'],'location':e['location'],'xnb_sha256':hashlib.sha256(e['data']).hexdigest(),**info})
   for graphic,fr in needed[key]:
    s=fr['sprite'];x,y,w,h=s['rect']
    if min(x,y,w,h)<0 or x+w>im.width or y+h>im.height:raise ValueError('FX crop out of bounds')
    crop=im.crop((x,y,x+w,y+h));rgba=crop.tobytes();dest=out/'animations'/graphic;dest.mkdir(exist_ok=True)
    name=f'{fr["frame"]:04d}';crop.save(dest/f'{name}.png');(dest/f'{name}.rgba').write_bytes(rgba)
    fr.update({'width':w,'height':h,'png':f'animations/{graphic}/{name}.png','rgba_path':f'animations/{graphic}/{name}.rgba','rgba_bytes':len(rgba),'sha256':hashlib.sha256(rgba).hexdigest()})
 # First original frame is a deterministic preview, not a recovered RNG state.
 object_index={r['object']['Id']:r for r in m['all_original_objects']}
 for cov in coverage:
  if cov['status']not in('book_resolved','constant_resolved'):continue
  rec=object_index[cov['id']];ob=rec['object'];clip=clips[rec['graphic']];fr=clip['frames'][0]
  if 'rgba_path'not in fr:continue
  key=clip['name'];t=next((t for t in textures if t.get('clip')==key),None)
  if t is None:
   t={'index':len(textures),'width':fr['width'],'height':fr['height'],'rgba_path':fr['rgba_path'],'png':fr['png'],'sha256':fr['sha256'],'graphic':fr['graphic'],'atlas':fr['atlas'],'atlas_rect':fr['sprite']['rect'],'clip':key};textures.append(t)
  s=fr['sprite'];ratio=s['scale_ratio'];scale=ob['Scale']*rec['obstacle_thing'].get('Scale',1)*clip['definition'].get('Scale',1);w,h=fr['width'],fr['height'];trim=s['trim'];original=s['original'];a=clip['definition'];origin=[a.get('OriginX',original[0]/2),a.get('OriginY',original[1]/2)]
  ox,oy=atlas_center(s,origin,ob['FlipHorizontal'],ob['FlipVertical'])
  offset=rec['obstacle_thing'].get('Offset',{});body_scale=ob['Scale']*rec['obstacle_thing'].get('Scale',1)
  # Animation.GetDrawLocation 0x180297d60: source draw OffsetX/Y (Animation+90)
  # is added unrotated, scaled by the attached Body scale only (not the
  # animation's own Scale), the same contract action_effect.cpp consumes.
  draw_offset=[a.get('OffsetX',0)*body_scale,a.get('OffsetY',0)*body_scale]
  anchor=[ob['Location']['X']+offset.get('X',0)*body_scale+draw_offset[0],ob['Location']['Y']+offset.get('Y',0)*body_scale+draw_offset[1]-ob['OffsetZ']*CAMERA_Z_SCALE];ox*=scale;oy*=scale
  angle=math.radians(ob['Angle']);c,ss=math.cos(angle),math.sin(angle);cx=anchor[0]+ox*c-oy*ss;cy=anchor[1]+ox*ss+oy*c;sw,sh=w*ratio[0]*scale,h*ratio[1]*scale
  row=[t['index'],cx,cy,sw,sh,1. if ob['FlipHorizontal']else 0.,1. if ob['FlipVertical']else 0.,0. if ob['FlipHorizontal']else 1.,0. if ob['FlipVertical']else 1.,angle,ob['Color'][3]/255]
  sprites.append({'object_id':ob['Id'],'name':ob['Name'],'graphic':rec['graphic'],'group_names':ob['GroupNames'],'source_layer':min((groups[g][0]for g in ob['GroupNames']if g in groups),default=999),'draw':row,'draw_anchor':anchor,'native_transform_profile':'v5','color_rgba':ob['Color'],'angle_degrees':ob['Angle'],'skew_angle':ob['SkewAngle'],'skew_scale':ob['SkewScale'],'animation':key,'source_pivot':{'trim':trim,'original':original,'origin':origin,'scale_ratio':ratio,'frame_rect':s['rect'],'flags':s['flags']},**({'animation_draw_offset':draw_offset}if draw_offset[0]or draw_offset[1]else{})})
 for s in sprites:
  ob=object_index[s['object_id']]['object'];anim=clips.get(s.get('animation'),{}).get('definition',{});n,cx,cy,w,h,u0,v0,u1,v1,angle,alpha=s['draw'];c,ss=math.cos(angle),math.sin(angle);sx,sy=anim.get('PostRotateScaleX',1),anim.get('PostRotateScaleY',1)
  # World anchor is preserved for post-rotation scale, including trimmed pivot.
  required=0<ob['SkewAngle']<90 or ob['SkewScale']!=1
  s['draw_quad']=project_quad(s['draw'],ob,sx,sy,anchor=s['draw_anchor'],mesh_type=object_index[s['object_id']]['obstacle_thing'].get('MeshType','Flat'),full_stretch=required)
  s['source_layer']=min((groups[g][0]for g in s['group_names']if g in groups),default=999)
  s['layer_blend_mode']=next((groups[g][1].get('BlendMode',0)for g in s['group_names']if g in groups),0)
  s['material']={'add_color':bool(anim.get('AddColor',False)),'hsv':[ob['Hue'],ob['Saturation'],ob['Value']],'ambient':ob['Ambient'],'post_rotate_scale':[sx,sy],'parallax':ob['ParallaxAmount'],'mesh_type':object_index[s['object_id']]['obstacle_thing'].get('MeshType'),'offset_z':ob['OffsetZ'],'native_z_projection':CAMERA_Z_SCALE,'skew_applied':required,'squash_stretch_required':required,'native_y_offset':0}
 sprites.sort(key=lambda s:(s['source_layer'],object_index[s['object_id']]['object']['SortIndex'],object_index[s['object_id']]['object']['Location']['Y']))
 with(out/'scene-affine.bin').open('xb')as f:
  f.write(b'HDS3'+struct.pack('<II',len(textures),len(sprites)))
  for t in textures:
   rgba=(out/t['rgba_path']).read_bytes()
   if len(rgba)!=t['width']*t['height']*4:raise ValueError('RGBA length mismatch')
   f.write(struct.pack('<II',t['width'],t['height'])+rgba)
  for s in sprites:
   d=s['draw'];f.write(struct.pack('<I8f4f4fII',d[0],*(v for p in s['draw_quad']for v in p),*d[5:9],*(v/255 for v in s['color_rgba']),s['layer_blend_mode'],int(s['material']['add_color'])))
 counts=collections.Counter(r['status']for r in coverage);diag={'room':m['room'],'objects':len(coverage),'status_counts':dict(counts),'records':coverage,'unsupported_textures':unsupported,'missing_frames':{n:c['missing_frames']for n,c in clips.items()if c['missing_frames']},'rendered_instances':len(sprites),'by_blend_mode':dict(collections.Counter(s['layer_blend_mode']for s in sprites)),'native_axis_stretch_applied':sum(s['material']['skew_applied']and native_axis_stretch(object_index[s['object_id']]['object'])!=1 for s in sprites),'native_non_axis_stretch_applied':sum(s['material']['skew_applied']and native_axis_stretch(object_index[s['object_id']]['object'])==1 for s in sprites),'squash_stretch_not_yet_applied':sum(s['material']['squash_stretch_required']and not s['material']['skew_applied']for s in sprites),'sources':sources,'native_group_order':[g['Id']['Name']for g in flattened],'native_group_order_source':'GroupManager::Init/AddGroup recovery, native-init-groups.json/native-addgroup-groups.json'}
 (out/'coverage.json').write_text(json.dumps(diag,indent=2)+'\n');(out/'animation-clips.json').write_text(json.dumps(clips,indent=2)+'\n')
 m.update({'sources':sources,'textures':textures,'sprites':sprites,'rendered_instances':len(sprites),'decoded_fx_atlases':decoded,'graphics_revision':'native-full-stretch-world-thing-offset-v5','color_format':{'disk':'BGRA8','native':'RGBA8','native_proof':'MapThingsList::LoadVersion12 VA0x1800d0cfb..0x1800d0d45; matching PDB Color.r0,g1,b2,a3','raw_bytes':'ColorStoredBGRA'},'scene':identity(out/'scene-affine.bin'),'limitations':['Original FX books and source frame counts recovered. Deterministic first-frame preview; original random phase not recovered.','PostRotateScale applied after rotation around original object anchor, provisional call-order interpretation.','Native flat/vertical squash/stretch, world-axis Thing.Offset ordering, BGRA conversion, height projection and flip pivots recovered; default input origin and shader selection remain under investigation.','HDS3 has original-derived four corners, straight RGBA tint, raw original group BlendMode and AddColor; compositor must support these fields.']})
 (out/'scene-manifest.json').write_text(json.dumps(m,indent=2)+'\n');print(json.dumps({'output':str(out),'counts':dict(counts),'sprites':len(sprites),'textures':len(textures),'clips':len(clips),'unsupported':unsupported}))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--content',type=Path,required=True);p.add_argument('--base',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();build(a.content,a.base,a.output)
