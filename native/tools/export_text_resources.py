#!/usr/bin/env python3.13
"""Export preserved English XNB glyphs and original localized text records."""
import argparse,json,struct,csv,re
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

def load_text_records(content):
 # TextData fields inherit independently; an explicit empty string overrides
 # its parent. Keep duplicate source rows until each field is resolved, so
 # differing descriptions cannot discard an otherwise identical DisplayName.
 records={};sources=[]
 def admit(node,path):
  if isinstance(node,dict):
   if isinstance(node.get('Id'),str)and any(isinstance(node.get(k),str)for k in('DisplayName','Description','AdditionalDescription','InheritFrom')):
    records.setdefault(node['Id'],[]).append({'source':str(path.resolve()),'record':node})
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
 ambiguous=object();cache={};field_sources={}
 def field(key,name,stack=()):
  if key in stack or len(stack)>32:raise ValueError('Original text InheritFrom cycle: '+key)
  if (key,name)in cache:return cache[key,name]
  if key not in records:raise ValueError('Original text InheritFrom record unavailable: '+key)
  values=[];origins=[]
  for row in records[key]:
   own=row['record']
   if name in own:
    if not isinstance(own[name],str):raise ValueError('Original text field requires string: '+key+'.'+name)
    value=own[name];origins.append({'source':row['source'],'id':key,'field':name})
   elif own.get('InheritFrom')not in(None,'null',''):
    parent=own['InheritFrom'];value=field(parent,name,stack+(key,));origins.extend(field_sources[parent,name])
   else:value=None if name=='DisplayName' else ''
   values.append(value)
  value=values[0]if all(v==values[0]for v in values)else ambiguous
  cache[key,name]=value;field_sources[key,name]=origins;return value
 localization={};descriptions={};excluded={name:[]for name in('DisplayName','Description','AdditionalDescription')}
 for key in sorted(records):
  resolved={name:field(key,name)for name in excluded}
  for name,value in resolved.items():
   if value is ambiguous:excluded[name].append(key)
  if isinstance(resolved['DisplayName'],str):localization[key]=resolved['DisplayName']
  # GameDataManager::GetDescription 0x1800b6a30 concatenates TextData+0x70
  # (Description) and +0x88 (AdditionalDescription), without substitution.
  if all(resolved[name]is not ambiguous for name in('Description','AdditionalDescription')):
   descriptions[key]=resolved['Description']+resolved['AdditionalDescription']
 return localization,descriptions,records,excluded,field_sources,sources

def export(content,out):
 if out.exists():raise ValueError('Fresh output required')
 out.mkdir(parents=True);(out/'text-fonts').mkdir()
 sources=[];fonts={};details={}
 remap=content/'Game/FontSizes.remap';sizes=json.loads(remap.read_text());sources.append(identity(remap))
 # Font class derived from the original scripts, not a name list: every
 # string literal assigned to a `...Font` field in Scripts/*.lua (CreateTextBox
 # Font, text-format tables, screen data such as TitleFont/OverheadTextFont).
 # Include one exact native emphasis-map target for each authored name;
 # SetupFont performs one lookup, never a recursive naming transformation.
 # Each admitted name must have an original remap entry and English XNB.
 from lua_static import LuaFile as _LuaFile
 font_refs={}
 for path in sorted((content/'Scripts').glob('*.lua')):
  t=_LuaFile(path).tokens;used=False
  for i in range(len(t)-2):
   if t[i].kind=='name' and t[i].value.endswith('Font') and t[i+1].kind=='op' and t[i+1].value=='=' and t[i+2].kind=='str':
    font_refs.setdefault(t[i+2].value,[]).append(f'{path.name}:{t[i].line} {t[i].value}');used=True
  if used:sources.append(identity(path))
 emphasis_path=Path(__file__).resolve().parents[1]/'analysis/font-emphasis-contract.json'
 emphasis=json.loads(emphasis_path.read_text())
 if emphasis.get('schema')!='hades-font-emphasis-contract-v1' or emphasis.get('target_sha256')!='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254':raise ValueError('Original font emphasis contract identity')
 emphasis_identity=identity(emphasis_path);sources.append(emphasis_identity)
 pairs=emphasis['map_initializer']['pairs'];mapping={}
 for pair in pairs:
  if not isinstance(pair,list)or len(pair)!=2 or not all(isinstance(n,str)and n for n in pair)or pair[0]in mapping:raise ValueError('Original font emphasis map pair')
  mapping[pair[0]]=pair[1]
 font_edges=[{'source_font':name,'target_font':mapping[name],'initializer_va':emphasis['map_initializer']['va'],'script_references':font_refs[name][:8]}for name in sorted(set(font_refs)&set(mapping))]
 font_candidates=set(font_refs)|{edge['target_font']for edge in font_edges}
 font_rejections={n:'Referenced font has no original FontSizes.remap entry/English XNB' for n in font_candidates if n not in sizes or not (content/'Win/Fonts/en'/f'{n}.xnb').exists()}
 if any(edge['target_font']in font_rejections for edge in font_edges):raise ValueError('Original emphasis target font resource unavailable')
 font_names=sorted(font_candidates-set(font_rejections))
 for name in font_names:
  path=content/'Win/Fonts/en'/f'{name}.xnb';im,meta=read_font(path);sources.append(identity(path))
  meta['font_size_remap_record']=sizes[name] # Preserve remap differences; compiled XNB nativeSize controls glyph scale.
  image=out/'text-fonts'/f'{name}.rgba';image.write_bytes(b'UIR1'+struct.pack('<II',*im.size)+im.tobytes())
  metrics=out/'text-fonts'/f'{name}.bin';raw=bytearray(b'UFN1'+struct.pack('<IifI',len(meta['glyphs']),meta['line_height'],meta['spacing'],meta['default_codepoint']))
  for g in meta['glyphs']:raw+=struct.pack('<I8i3f',g['codepoint'],*g['atlas_rect'],*g['cropping'],*g['kerning'])
  metrics.write_bytes(raw);fonts[name]={'NativeSize':meta['font_size'],'RemapSize':sizes[name],'Image':f'text-fonts/{name}.rgba','Metrics':f'text-fonts/{name}.bin'};details[name]=meta
 localization,descriptions,records,excluded,field_sources,text_sources=load_text_records(content);sources.extend(text_sources)
 # Original IconData is the bounded source class used by UIScripts to construct
 # Icons. Admit the complete literal texture class, including dynamically chosen
 # keys; collect literal markup references as evidence, not a name allowlist.
 from hades_assets import Package,xnb_image
 from lua_static import LuaFile,top_level_tables
 icon_path=content/'Scripts/UIData.lua';icon_lua=LuaFile(icon_path)
 tables=[(value,line)for name,value,line,_,_ in top_level_tables(icon_lua)if name=='IconData']
 if len(tables)!=1:raise ValueError('Original IconData table identity')
 icons,icon_line=tables[0];sources.append(identity(icon_path))
 icon_setup=content/'Scripts/UIScripts.lua';sources.append(identity(icon_setup))
 references={};token_pattern=re.compile(r'\{!Icons\.([A-Za-z_][A-Za-z0-9_]*)\}')
 for field,values in [('DisplayName',localization),('Descriptions',descriptions)]:
  for key,value in values.items():
   origins=field_sources[key,'DisplayName']if field=='DisplayName'else field_sources[key,'Description']+field_sources[key,'AdditionalDescription']
   for icon in token_pattern.findall(value):references.setdefault(icon,[]).append({'localized_id':key,'field':field,'sources':origins})
 for path in sorted((content/'Scripts').glob('*.lua')):
  script=LuaFile(path);used=False
  for token in script.tokens:
   if token.kind!='str':continue
   for icon in token_pattern.findall(token.value):
    references.setdefault(icon,[]).append({'source':str(path.resolve()),'line':token.line});used=True
  if used:sources.append(identity(path))
 texture_icons={};symbol_rejections={};symbols={};symbol_details={}
 for name,record in icons.items():
  if isinstance(record,dict)and isinstance(record.get('TexturePath'),str):texture_icons.setdefault(record['TexturePath'],[]).append(name)
  else:symbol_rejections[str(name)]='Original IconData has no literal TexturePath'
 for name in sorted(set(references)-set(icons)):symbol_rejections[name]='Literal markup references unknown original IconData key'
 manifest=content/'Win/Packages/GUI.pkg_manifest';package=manifest.with_suffix('.pkg')
 sources.extend([identity(manifest),identity(package)])
 sprites={sprite['name']:(entry['name'],sprite)for entry in Package(manifest).entries()for sprite in entry.get('sprites',[])}
 needed_atlases={sprites[texture][0]for texture in texture_icons if texture in sprites}
 # Decode each original atlas once and discard it after its icon crops.
 found_textures=set()
 for entry in Package(package).entries():
  atlas=entry.get('name')
  if atlas not in needed_atlases:continue
  image,info=xnb_image(entry['data'])
  for texture,names in sorted(texture_icons.items()):
   if texture not in sprites or sprites[texture][0]!=atlas:continue
   sprite=sprites[texture][1];x,y,w,h=sprite['rect']
   if sprite['flags']&3:
    symbol_rejections[texture]='Original atlas sprite transform flags are not admitted';continue
   if min(x,y)<0 or min(w,h)<=0 or x+w>image.width or y+h>image.height:raise ValueError('Original symbol atlas bounds: '+texture)
   crop=image.crop((x,y,x+w,y+h));stem=texture.replace('\\','_')
   (out/'text-fonts'/f'{stem}.rgba').write_bytes(b'UIR1'+struct.pack('<II',w,h)+crop.tobytes())
   symbols[texture]={'Image':f'text-fonts/{stem}.rgba','Width':w,'Height':h,'OriginalWidth':sprite['original'][0],'OriginalHeight':sprite['original'][1],'TrimX':sprite['trim'][0],'TrimY':sprite['trim'][1],'ScaleRatioX':sprite['scale_ratio'][0],'ScaleRatioY':sprite['scale_ratio'][1]}
   symbol_details[texture]={'atlas':atlas,'sprite':sprite,'xnb':info,'package':str(package),'icon_keys':names,'class_source':str(icon_path.resolve()),'class_line':icon_line,'references':{name:references.get(name,[])for name in names}}
   found_textures.add(texture)
 for texture in sorted(set(texture_icons)-found_textures-set(symbol_rejections)):
  symbol_rejections[texture]='Original IconData texture absent from GUI package manifest'
 gui=content/'Game/GUI/InGameUI.sjson';ui=Sjson(gui.read_text()).value()['InGameUI'];sources.append(identity(gui))
 style={**ui['SubtitlesA'],**ui['SubtitlesB']};style.pop('InheritFrom',None);style['X']=ui['SubtitlesBBacking']['X'];style['Y']=ui['SubtitlesBBacking']['Y'];style['AX']=ui['SubtitlesABacking']['X'];style['AY']=ui['SubtitlesABacking']['Y']
 catalog={'Schema':'HadesOriginalText1','Fonts':fonts,'Localization':localization,'Descriptions':descriptions,'SubtitleStyle':style,'Symbols':symbols}
 lua=out/'text-fonts.lua';lua.write_text('return '+literal(catalog)+'\n')
 report={'schema':'hades-original-text-v1','target_sha256':'d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254','catalog':identity(lua),'files':[identity(p)for p in sorted((out/'text-fonts').iterdir())],'sources':sources,'fonts':details,'font_class':{'policy':'Script-literal *Font names plus one exact native emphasis-map target per authored name, with original remap entries and English XNBs; no recursive remapping','references':{n:v[:8] for n,v in sorted(font_refs.items())},'emphasis_contract':emphasis_identity,'emphasis_edges':font_edges,'rejections':font_rejections},'ambiguous_localization_keys_excluded':excluded['DisplayName'],'ambiguous_text_fields_excluded':excluded,'localization_count':len(localization),'description_count':len(descriptions),'localization_records':{k:records[k]for k in localization},'description_records':{k:{'records':records[k],'fields':{name:field_sources[k,name]for name in('Description','AdditionalDescription')}}for k in descriptions},'symbols':symbol_details,'symbol_class':{'source':str(icon_path.resolve()),'record_count':len(icons),'literal_textures':len(texture_icons),'policy':'All original IconData literal TexturePath records, independent of selected room or icon key'},'symbol_rejections':symbol_rejections,'subtitle_style':style,'limits':['Original glyphs/metrics and English DisplayName/Description/AdditionalDescription strings preserved; macros and rich text substitutions require actual format consumers.','Script-referenced original fonts admitted (font_class); no system font substitution.','Text fields inherit independently, parent-then-own including explicit empty strings; ambiguous fields are excluded independently; only English.','Symbol textures derive from the complete original IconData literal texture class; absent GUI-package resources and unsupported transform flags are reported and remain unavailable.','Speech text and lifetime come from original audio SpeechPresentation events; no copied subtitle timing.']}
 (out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'fonts':len(fonts),'localized_keys':len(localization),'font_bytes':sum(x['bytes']for x in report['files']),'catalog_bytes':report['catalog']['bytes']}))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--content',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();export(a.content,a.output)
