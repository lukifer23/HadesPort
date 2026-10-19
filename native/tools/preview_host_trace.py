#!/usr/bin/env python3.13
"""Overlay actual host player trace and original HDH1 pose for diagnostics."""
import argparse,hashlib,json,math,struct
from pathlib import Path
from PIL import Image,ImageDraw

def main():
 p=argparse.ArgumentParser();p.add_argument('--scene-preview',type=Path,required=True);p.add_argument('--trace',type=Path,required=True);p.add_argument('--hero-dir',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--tick',type=int);a=p.parse_args()
 source=json.loads(a.scene_preview.with_suffix('.json').read_text());trace=json.loads(a.trace.read_text());samples=trace['samples']
 if not samples:raise ValueError('Trace empty')
 chosen=samples[-1]if a.tick is None else min(samples,key=lambda x:abs(x['tick']-a.tick));scale=source['scale'];x0,y0,_,_=source['bounds'];canvas=Image.open(a.scene_preview).convert('RGBA');draw=ImageDraw.Draw(canvas)
 points=[((s['x']-x0)*scale,(s['y']-y0)*scale)for s in samples if s['tick']<=chosen['tick']]
 if len(points)>1:draw.line(points,fill=(255,255,255,220),width=2)
 moving=math.hypot(chosen['input_x'],chosen['input_y'])>0.01;clip=a.hero_dir/('ZagreusRun.bin'if moving else'ZagreusIdle.bin');b=clip.read_bytes()
 if b[:4]!=b'HDH1':raise ValueError('Not HDH1')
 frames,w,h,angles,fps,display_scale,offset_y=struct.unpack_from('<4I3f',b,4)
 if not 0<frames*angles<=8192 or w*h>1024*1024 or len(b)!=32+frames*angles*w*h*4:raise ValueError('HDH1 bounds mismatch')
 # Current host trace frame is the actual angle-major HDH1 frame index.
 index=chosen['frame']%(frames*angles)if moving else 0;offset=32+index*w*h*4;im=Image.frombytes('RGBA',(w,h),b[offset:offset+w*h*4]);im=im.resize((max(1,round(w*display_scale*scale)),max(1,round(h*display_scale*scale))),Image.Resampling.LANCZOS);x=(chosen['x']-x0)*scale;y=(chosen['y']+offset_y-y0)*scale;canvas.alpha_composite(im,(round(x-im.width/2),round(y-im.height/2)));canvas.convert('RGB').save(a.output)
 result={'scene_preview':str(a.scene_preview),'trace':str(a.trace),'trace_sha256':hashlib.sha256(a.trace.read_bytes()).hexdigest(),'sample':chosen,'hero_clip':str(clip),'hero_sha256':hashlib.sha256(b).hexdigest(),'hero_frame':index,'display_scale':display_scale,'offset_y':offset_y,'output':str(a.output),'limits':'Diagnostic trace/pose overlay drawn over foreground; pose display offset is provisional HDH1 conversion field. No invented character or simulated trace samples.'};a.output.with_suffix('.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
