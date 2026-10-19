#!/usr/bin/env python3.13
"""Host-only original scene affine preview; unsupported materials reported.

Uses original shader equations and the recovered blend enum. Shader selection,
native skew and the render-layer hierarchy remain provisional.
"""
import argparse,collections,json,math
from pathlib import Path
from PIL import Image,ImageChops
from export_scene_animation import frame_quad

def main():
 p=argparse.ArgumentParser();p.add_argument('directory',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--frame',type=int,default=0);a=p.parse_args();m=json.loads((a.directory/'scene-manifest.json').read_text());clips=json.loads((a.directory/'animation-clips.json').read_text())
 clamps=[o['object']['Location']for o in m['all_original_objects']if o['object']['Name']=='CameraClamp'];x0=min(p['X']for p in clamps)-200;y0=min(p['Y']for p in clamps)-200;x1=max(p['X']for p in clamps)+200;y1=max(p['Y']for p in clamps)+200;scale=min(1500/(x1-x0),1000/(y1-y0));canvas=Image.new('RGBA',(math.ceil((x1-x0)*scale),math.ceil((y1-y0)*scale)),(0,0,0,255));cache={};unsupported=collections.Counter();coverage=json.loads((a.directory/'coverage.json').read_text());unsupported['non_axis_squash_stretch']=coverage['squash_stretch_not_yet_applied']
 original={r['object']['Id']:r for r in m['all_original_objects']}
 for s in m['sprites']:
  tex=m['textures'][s['draw'][0]];path=tex['png']
  world_quad=s['draw_quad']
  if s.get('animation'):
   clip=clips[s['animation']];frames=clip['frames'];fr=frames[a.frame%len(frames)];path=fr['png'];world_quad=s['draw_quad']if a.frame%len(frames)==0 else frame_quad(s,original[s['object_id']],clip,fr)
  if path not in cache:cache[path]=Image.open(a.directory/path).convert('RGBA')
  im=cache[path];u0,v0,u1,v1=s['draw'][5:9]
  if u0>u1:im=im.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
  if v0>v1:im=im.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
  tint=tuple(s['color_rgba']);alpha=im.getchannel('A');hsv=s['material']['hsv']
  if any(hsv):
   hh,sat,val=im.convert('RGB').convert('HSV').split()
   hh=hh.point([round(((v/255+hsv[0]+1)%1)*255)for v in range(256)])
   sat=sat.point([round(max(0,min(1,v/255+hsv[1]))*255)for v in range(256)])
   val=val.point([round(max(0,min(1,v/255+hsv[2]))*255)for v in range(256)])
   im=Image.merge('HSV',(hh,sat,val)).convert('RGB').convert('RGBA');im.putalpha(alpha)
  if s['material']['add_color']:
   rgb=ImageChops.add(im.convert('RGB'),Image.new('RGB',im.size,tint[:3]));im=rgb.convert('RGBA');im.putalpha(alpha.point([round(v*tint[3]/255)for v in range(256)]))
  else:im=ImageChops.multiply(im,Image.new('RGBA',im.size,tint))
  quad=[((x-x0)*scale,(y-y0)*scale)for x,y in world_quad];ax,ay=quad[0];bx,by=quad[1][0]-ax,quad[1][1]-ay;cx,cy=quad[2][0]-ax,quad[2][1]-ay;det=bx*cy-by*cx
  if abs(det)<1e-8:continue
  left=max(0,math.floor(min(x for x,y in quad)));top=max(0,math.floor(min(y for x,y in quad)));right=min(canvas.width,math.ceil(max(x for x,y in quad)));bottom=min(canvas.height,math.ceil(max(y for x,y in quad)))
  if right<=left or bottom<=top:continue
  iw,ih=im.size;dx,dy=left-ax,top-ay;matrix=(iw*cy/det,-iw*cx/det,iw*(cy*dx-cx*dy)/det,-ih*by/det,ih*bx/det,ih*(-by*dx+bx*dy)/det)
  patch=im.transform((right-left,bottom-top),Image.Transform.AFFINE,matrix,resample=Image.Resampling.BICUBIC)
  mode=s['layer_blend_mode']
  if mode in(2,9,11):
   bg=canvas.crop((left,top,right,bottom));fg=patch.convert('RGB');rgb=bg.convert('RGB')
   if mode==2:rgb=ImageChops.add(rgb,ImageChops.multiply(fg,Image.merge('RGB',(patch.getchannel('A'),)*3)));result=rgb.convert('RGBA')
   elif mode==9:result=Image.composite(ImageChops.overlay(rgb,fg),rgb,patch.getchannel('A')).convert('RGBA')
   else:
    bl=rgb.convert('L',matrix=(.3,.59,.11,0));fl=fg.convert('L',matrix=(.3,.59,.11,0));mask=ImageChops.subtract(fl,bl).point([0]+[255]*255);mask=ImageChops.multiply(mask,patch.getchannel('A'));result=Image.composite(fg,rgb,mask).convert('RGBA')
   canvas.paste(result,(left,top))
  else:
   if mode:unsupported['raw_layer_blend_'+str(mode)]+=1
   canvas.alpha_composite(patch,(left,top))
 canvas.convert('RGB').save(a.output);report={'preview':str(a.output),'frame':a.frame,'bounds':[x0,y0,x1,y1],'scale':scale,'instances':len(m['sprites']),'unsupported_materials':dict(unsupported),'limits':'Source HLSL HSV/add-color/overlay/luminosity equations approximated at Pillow 8-bit precision; shader selection/default origins remain provisional; non-axis squash/stretch and native offscreen group compositing remain incomplete.'};a.output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
