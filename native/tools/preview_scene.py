#!/usr/bin/env python3.13
"""Independent PIL preview of decoded original scene (approximate compositor)."""
import argparse,json,math
from pathlib import Path
from PIL import Image,ImageChops
p=argparse.ArgumentParser();p.add_argument('directory',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();m=json.loads((a.directory/'scene-manifest.json').read_text())
clamps=[o['object']['Location']for o in m['all_original_objects']if o['object']['Name']=='CameraClamp']
x0=min(x['X']for x in clamps)-200;y0=min(x['Y']for x in clamps)-200;x1=max(x['X']for x in clamps)+200;y1=max(x['Y']for x in clamps)+200
scale=min(1500/(x1-x0),1000/(y1-y0));canvas=Image.new('RGBA',(int((x1-x0)*scale),int((y1-y0)*scale)),(0,0,0,255));images={}
for s in m['sprites']:
 n,cx,cy,w,h,u0,v0,u1,v1,angle,alpha=s['draw'];rec=m['textures'][n]
 if n not in images:images[n]=Image.open(a.directory/rec['png']).convert('RGBA')
 if w*scale<1 or h*scale<1:continue
 im=images[n].resize((max(1,int(w*scale)),max(1,int(h*scale))),Image.Resampling.LANCZOS)
 if u0>u1:im=im.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
 if v0>v1:im=im.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
 tint=tuple(s['color_rgba']);im=ImageChops.multiply(im,Image.new('RGBA',im.size,tint))
 if angle:im=im.rotate(-math.degrees(angle),resample=Image.Resampling.BICUBIC,expand=True)
 x=round((cx-x0)*scale-im.width/2);y=round((cy-y0)*scale-im.height/2);canvas.alpha_composite(im,(x,y))
canvas.convert('RGB').save(a.output)
print({'preview':str(a.output),'camera_bounds':[x0,y0,x1,y1]})
