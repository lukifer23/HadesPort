#!/usr/bin/env python3.13
"""Export preserved English XNB glyphs and actual localized DisplayName records."""
import argparse,json,struct,csv
from pathlib import Path
from export_native_ui import read_font
from build_room_scene import Sjson,identity
from export_actor_catalog import literal

class TextSjson(Sjson):
 def token(self):
  self.skip()
  if self.text.startswith('"""',self.pos):
   begin=self.pos+3;end=self.text.find('"""',begin)
   if end<0:raise ValueError('Unterminated source multiline text')
   self.pos=end+3;return ('s',self.text[begin:end])
  return super().token()

def export(content,out):
 if out.exists():raise ValueError('Fresh output required')
 out.mkdir(parents=True);(out/'text-fonts').mkdir()
 sources=[];fonts={};details={}
 remap=content/'Game/FontSizes.remap';sizes=json.loads(remap.read_text());sources.append(identity(remap))
 # Actual nonempty startup + original English paragraph styles. Other fonts
 # fail explicitly until their original source is admitted.
 # AlegreyaSansSCBold/SCRegular: actual combat HUD CreateTextBox Font values
 # (UIScripts.lua ShowHealthUI/ShowMoneyUI/ShowAmmoUI/ResourceGainPresentation).
 for name in ['SpectralSCLightTitling','AlegreyaSansExtraBold','AlegreyaSansExtraBoldItalic','AlegreyaSansRegular','AlegreyaSansItalic','AlegreyaSansSCExtraBold','AlegreyaSansSCBold','AlegreyaSansSCRegular']:
  path=content/'Win/Fonts/en'/f'{name}.xnb';im,meta=read_font(path);sources.append(identity(path))
  meta['font_size_remap_record']=sizes[name] # Preserve remap differences; compiled XNB nativeSize controls glyph scale.
  image=out/'text-fonts'/f'{name}.rgba';image.write_bytes(b'UIR1'+struct.pack('<II',*im.size)+im.tobytes())
  metrics=out/'text-fonts'/f'{name}.bin';raw=bytearray(b'UFN1'+struct.pack('<IifI',len(meta['glyphs']),meta['line_height'],meta['spacing'],meta['default_codepoint']))
  for g in meta['glyphs']:raw+=struct.pack('<I8i3f',g['codepoint'],*g['atlas_rect'],*g['cropping'],*g['kerning'])
  metrics.write_bytes(raw);fonts[name]={'NativeSize':meta['font_size'],'RemapSize':sizes[name],'Image':f'text-fonts/{name}.rgba','Metrics':f'text-fonts/{name}.bin'};details[name]=meta
 # Text records: every en *.sjson Texts record (Id, DisplayName, InheritFrom)
 # plus the en block of Game/Text/Aliases.sjson (UI_AmmoText/UI_GunText are
 # InheritFrom aliases of UI_SuperText). InheritFrom is resolved with the
 # engine-wide SJSON parent-then-own rule used by every other exporter here.
 records={};ranges={};ambiguous=set()
 def admit(node,path):
  if isinstance(node,dict):
   if isinstance(node.get('Id'),str)and(isinstance(node.get('DisplayName'),str)or isinstance(node.get('InheritFrom'),str)):
    key=node['Id'];own={k:node[k]for k in('DisplayName','InheritFrom')if isinstance(node.get(k),str)}
    if key in records and records[key]!=own:ambiguous.add(key)
    records[key]=own;ranges[key]={'source':str(path.resolve()),'record':node}
   for value in node.values():admit(value,path)
  elif isinstance(node,list):
   for value in node:admit(value,path)
 for path in sorted((content/'Game/Text/en').glob('*.sjson')):
  if path.name=='MacroText.sjson':continue
  admit(TextSjson(path.read_text()).value(),path);sources.append(identity(path))
 aliases=content/'Game/Text/Aliases.sjson';alias_data=TextSjson(aliases.read_text()).value();sources.append(identity(aliases))
 en=[block for block in alias_data['HelpTexts'] if block.get('lang')=='en']
 if len(en)!=1:raise ValueError('Original English alias block identity')
 admit(en[0]['Texts'],aliases)
 def display(key,stack=()):
  if key in stack or len(stack)>32:raise ValueError('Original text InheritFrom cycle: '+key)
  own=records.get(key)
  if own is None:return None
  if 'DisplayName' in own:return own['DisplayName']
  return display(own['InheritFrom'],stack+(key,))if own.get('InheritFrom')not in(None,'null')else None
 localization={}
 for key in sorted(records):
  if key in ambiguous:continue
  value=display(key)
  if isinstance(value,str):localization[key]=value
 # Inline symbol textures named by the admitted HUD strings' {!Icons.X} tokens.
 # Icons[X]=IconData[X].TexturePath (UIScripts.lua:79-82, UIData.lua IconData).
 from hades_assets import Package,xnb_image
 symbols={};symbol_details={}
 for texture in ['GUI\\HUD\\Slash','GUI\\HUD\\InfiniteIcon']:
  found=None
  for manifest in sorted((content/'Win/Packages').glob('GUI.pkg_manifest')):
   for entry in Package(manifest).entries():
    for sprite in entry.get('sprites',[]):
     if sprite['name']==texture:found=(entry['name'],sprite,manifest)
  if not found:raise ValueError('Original HUD symbol texture unavailable: '+texture)
  atlas,sprite,manifest=found;sources.append(identity(manifest))
  package=manifest.with_suffix('.pkg');entry=next(e for e in Package(package).entries()if e.get('name')==atlas);sources.append(identity(package))
  image,info=xnb_image(entry['data']);x,y,w,h=sprite['rect']
  if sprite['flags']&3 or min(x,y)<0 or min(w,h)<=0 or x+w>image.width or y+h>image.height:raise ValueError('Original symbol atlas bounds')
  crop=image.crop((x,y,x+w,y+h));stem=texture.replace('\\','_')
  (out/'text-fonts'/f'{stem}.rgba').write_bytes(b'UIR1'+struct.pack('<II',w,h)+crop.tobytes())
  symbols[texture]={'Image':f'text-fonts/{stem}.rgba','Width':w,'Height':h,'OriginalWidth':sprite['original'][0],'OriginalHeight':sprite['original'][1],'TrimX':sprite['trim'][0],'TrimY':sprite['trim'][1],'ScaleRatioX':sprite['scale_ratio'][0],'ScaleRatioY':sprite['scale_ratio'][1]}
  symbol_details[texture]={'atlas':atlas,'sprite':sprite,'xnb':info,'package':str(package)}
 gui=content/'Game/GUI/InGameUI.sjson';ui=Sjson(gui.read_text()).value()['InGameUI'];sources.append(identity(gui))
 style={**ui['SubtitlesA'],**ui['SubtitlesB']};style.pop('InheritFrom',None);style['X']=ui['SubtitlesBBacking']['X'];style['Y']=ui['SubtitlesBBacking']['Y'];style['AX']=ui['SubtitlesABacking']['X'];style['AY']=ui['SubtitlesABacking']['Y']
 catalog={'Schema':'HadesOriginalText1','Fonts':fonts,'Localization':localization,'SubtitleStyle':style,'Symbols':symbols}
 lua=out/'text-fonts.lua';lua.write_text('return '+literal(catalog)+'\n')
 report={'schema':'hades-original-text-v1','target_sha256':'d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254','catalog':identity(lua),'files':[identity(p)for p in sorted((out/'text-fonts').iterdir())],'sources':sources,'fonts':details,'ambiguous_localization_keys_excluded':sorted(ambiguous),'localization_count':len(localization),'localization_records':{k:ranges[k]for k in localization},'symbols':symbol_details,'subtitle_style':style,'limits':['Original glyphs/metrics preserved. English DisplayName only; macros, descriptions and rich text substitutions require actual format consumers.','Eight source fonts admitted; no system font substitution.','InheritFrom text aliases resolved parent-then-own; only English.','Symbol textures: reviewed HUD icons only; other {!Icons.X} paths fail at runtime.','Speech text and lifetime come from original audio SpeechPresentation events; no copied subtitle timing.']}
 (out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'fonts':len(fonts),'localized_keys':len(localization),'font_bytes':sum(x['bytes']for x in report['files']),'catalog_bytes':report['catalog']['bytes']}))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--content',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();export(a.content,a.output)
