#!/usr/bin/env python3.13
"""Export original menu sprites and original XNB font, without substitute art.

HUI1 packet described in native/tools/UI_FORMAT.md. Source XNB font is decoded
as shipped (no new font rasterizer); unknown engine baseline is not fabricated.
"""
import argparse,hashlib,json,math,struct
from pathlib import Path
from PIL import Image
from hades_assets import Package,Reader,xnb_image
from build_room_scene import Sjson,identity

def read_font(path):
 raw=path.read_bytes();r=Reader(raw)
 if r.read(6)!=b'XNBw\x06\x00' or r.uint('<')!=len(raw):raise ValueError('Unsupported font header')
 fmt,w,h,mips,n=[r.integer('<')for _ in range(5)]
 if fmt!=12 or mips!=1 or not 0<w<=4096 or not 0<h<=4096 or n!=w*h:raise ValueError('Expected bounded alpha8 font')
 alpha=r.read(n)
 def rects():
  n=r.varint()
  if n>65536:raise ValueError('Excessive glyph count')
  return [[r.integer('<')for _ in range(4)]for _ in range(n)]
 rect=rects();crop=rects();count=r.varint();chars=[]
 if count!=len(rect) or count!=len(crop):raise ValueError('Unequal font lists')
 def char():
  lead=r.read(1);b=lead[0];n=1 if b<128 else 2 if 0xc2<=b<0xe0 else 3 if b<0xf0 else 4 if b<0xf5 else 0
  if not n:raise ValueError('Invalid UTF8 leading byte')
  text=(lead+r.read(n-1)).decode('utf-8')
  if len(text)!=1:raise ValueError('Expected one codepoint')
  return ord(text)
 for _ in range(count):chars.append(char())
 line_spacing=r.integer('<');spacing=r.floating('<');font_size=r.integer('<');kern_count=r.varint()
 if kern_count!=count or not 0<font_size<1024 or not 0<line_spacing<2048:raise ValueError('Unsupported font metrics')
 kern=[[r.floating('<')for _ in range(3)]for _ in range(count)]
 default_present=r.u8()
 if default_present not in (0,1):raise ValueError('Invalid default char bool')
 default=char()if default_present else None
 if r.pos!=len(raw):raise ValueError('Trailing font data')
 im=Image.new('RGBA',(w,h),(255,255,255,0));im.putalpha(Image.frombytes('L',(w,h),alpha))
 glyphs=[]
 # Original cropping.Y is measured from line top. The packet uses an explicit
 # transport baseline at font_size, so cropY-ascent preserves original top
 # placement exactly when caller's baseline is lineTop+ascent. No recovered
 # typographic engine ascent is claimed.
 ascent=float(font_size)
 for code,box,c,k in zip(chars,rect,crop,kern):
  x,y,gw,gh=box
  if min(x,y,gw,gh)<0 or x+gw>w or y+gh>h:raise ValueError('Glyph outside font atlas')
  glyphs.append({'codepoint':code,'atlas_rect':box,'cropping':c,'kerning':k,'bearing_x':c[0]+k[0],'bearing_y':c[1]-ascent,'advance':sum(k)+spacing})
 return im,{'source':identity(path),'texture_format':'Alpha8 original -> white straightRGBA plus alpha','font_size':font_size,'ascent':ascent,'line_height':line_spacing,'spacing':spacing,'default_codepoint':default,'glyphs':glyphs,'metric_policy':'Transport baseline=font_size; original cropping.Y preserved. advance=A+B+C+spacing; leading negative A clamping is caller layout policy. Native typographic ascent not recovered.'}

def straight_alpha(im):
 # Preserve decoded source channels. Alpha8 font expansion is already straight;
 # BC7 sprite storage may be premultiplied. Without a recovered content-build
 # transform it is unsafe to unpremultiply source values here. Return both
 # physical pixel diagnostics and explicit source convention limitation.
 b=im.tobytes();semi=0;gt=0
 for i in range(0,len(b),4):
  a=b[i+3]
  if 0<a<255:
   semi+=1
   if max(b[i:i+3])>a:gt+=1
 return im,{'source_alpha':'Decoded channels preserved; no unproven unpremultiplication.','semitransparent_pixels':semi,'semitransparent_rgb_exceeds_alpha_pixels':gt}

def export(content,out):
 if out.exists():raise ValueError('Output must be new')
 out.mkdir(parents=True);sources=[];sprites={};images={};details=[]
 requested={'logo':'GUI\\MainMenuLogo','background':'Launch\\load_bg_blank','zagreus':'GUI\\Screens\\MainMenuZagStatic','selector':'GUI\\Shell\\selector','selector_backing':'GUI\\Shell\\MainMenuSelectorBacking\\MainMenuSelectorBacking0011'}
 for pkgname in ['GUI','Launch']:
  manifest=content/'Win/Packages'/(pkgname+'.pkg_manifest');pkgpath=content/'Win/Packages'/(pkgname+'.pkg');sources.extend([identity(manifest),identity(pkgpath)])
  index={}
  for entry in Package(manifest).entries():
   if entry['type']=='atlas':
    for sprite in entry['sprites']:
     for key,name in requested.items():
      if sprite['name']==name:index.setdefault(entry['name'],[]).append((key,sprite))
  for entry in Package(pkgpath).entries():
   if entry['type']!='texture' or entry['name']not in index:continue
   atlas,texture_meta=xnb_image(entry['data'])
   for key,sprite in index[entry['name']]:
    x,y,w,h=sprite['rect']
    if sprite['flags']!=0 or min(x,y,w,h)<0 or x+w>atlas.width or y+h>atlas.height:raise ValueError('Unsupported sprite crop')
    im=atlas.crop((x,y,x+w,y+h));im,alpha_meta=straight_alpha(im)
    # Restore original transparent canvas; source trim is integer exact.
    canvas=Image.new('RGBA',tuple(sprite['original']));canvas.paste(im,tuple(sprite['trim']))
    if key in images:raise ValueError('Duplicate menu image')
    images[key]=canvas
    details.append({'name':key,'source_graphic':sprite['name'],'atlas':entry['name'],'package_location':entry['location'],'sprite':sprite,'texture_metadata':texture_meta,'alpha':alpha_meta,'export_canvas_size':list(canvas.size)})
 if set(images)!=set(requested):raise ValueError('Missing menu images '+str(set(requested)-set(images)))
 fontpath=content/'Win/Fonts/en/AlegreyaSansSCExtraBold.xnb';font,fontmeta=read_font(fontpath);images={'font':font,**images};sources.append(identity(fontpath))
 menu=content/'Game/GUI/MainMenuScreen.sjson';animations=content/'Game/Animations/ObstacleAnimations.sjson';launch=content/'Game/Animations/LaunchAnimations.sjson';remap=content/'Game/FontSizes.remap'
 for path in [menu,animations,launch,remap]:sources.append(identity(path))
 menu_def=Sjson(menu.read_text()).parse()['MainMenuScreen'];anims={}
 for path in [animations,launch]:
  for d in Sjson(path.read_text()).parse()['Animations']:
   if d['Name'].startswith('MainMenu'):anims[d['Name']]=d
 packet=out/'ui.bin'
 with packet.open('xb')as f:
  f.write(b'HUI1'+struct.pack('<I',len(images)))
  for name,im in images.items():
   encoded=name.encode();f.write(struct.pack('<I',len(encoded))+encoded+struct.pack('<II',*im.size)+im.tobytes());im.save(out/(name+'.png'))
  selected=[g for g in fontmeta['glyphs']if 32<=g['codepoint']<=126]
  if len(selected)!=95:raise ValueError('Missing required ASCII')
  f.write(struct.pack('<3fI',fontmeta['font_size'],fontmeta['ascent'],fontmeta['line_height'],len(selected)))
  for glyph in selected:f.write(struct.pack('<I7f',glyph['codepoint'],*glyph['atlas_rect'],glyph['bearing_x'],glyph['bearing_y'],glyph['advance']))
 # Independent companions for the portable renderer; same original pixels.
 companions=out/'ui';companions.mkdir()
 for name,im in images.items():
  (companions/(name+'.rgba')).write_bytes(b'UIR1'+struct.pack('<II',*im.size)+im.tobytes())
 with (companions/'font.bin').open('xb')as f:
  f.write(b'UFN1'+struct.pack('<IifI',len(fontmeta['glyphs']),fontmeta['line_height'],fontmeta['spacing'],fontmeta['default_codepoint']or 0))
  for glyph in fontmeta['glyphs']:
   f.write(struct.pack('<I8i3f',glyph['codepoint'],*glyph['atlas_rect'],*glyph['cropping'],*glyph['kerning']))
 result={'schema':'hades-native-ui-v1','packet':identity(packet),'packet_format':'HUI1,u32 textureCount; pertexture u32UTF8nameBytes,name,u32w,h,RGBA8; then f32fontSize,ascent,lineHeight,u32glyphCount; each u32codepoint,7f32 atlasX,Y,w,h,bearingX,Y,advance; little-endian','texture_names':list(images),'companion_format':{'textures':'UIR1,u32LEwidth,height,RGBA8','font':'UFN1,u32LEglyphCount,i32LElineSpacing,f32LEspacing,u32LEfallback; eachglyphu32LEcodepoint,i32LErect[4],i32LEcrop[4],f32LEkerning[3]'},'companion_files':[identity(p)for p in sorted(companions.iterdir())],'textures':details,'font':fontmeta,'source_integrity':sources,'original_main_menu_definition':menu_def,'original_menu_animations':anims,'limitations':['Original assets only; native shell adaptation is not original menu script execution.','Original movie backgrounds/intro not exported; source static background/logo/Zagreus are exported.','Sprite alpha channels physically preserved; straight vs premultiplied source convention not yet proven. Font texture is definitely straight white RGBA with original Alpha8 coverage.','Packet baseline is explicit transport policy; source font cropping/ABC kerning, native size35 and lineheight47 retained unmodified.']}
 (out/'ui-manifest.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'packet':str(packet),'sha256':result['packet']['sha256'],'textures':{k:list(v.size)for k,v in images.items()},'fontSize':fontmeta['font_size'],'glyphs':len(selected)}))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--content',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();export(a.content,a.output)
