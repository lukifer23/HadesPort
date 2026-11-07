"""Validate immutable original font/localization inputs before private staging."""
import hashlib,json,math,os,shutil,struct,tempfile
from pathlib import Path
TARGET='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'

def read_text_catalog(path):
 from lua_static import tokenize,_DataReader
 tokens=tokenize(Path(path).read_text())
 if tokens[0].kind!='kw'or tokens[0].value!='return':raise ValueError('Original text catalog requires one literal return')
 catalog,end=_DataReader(tokens).value(1)
 if tokens[end].kind!='eof'or not isinstance(catalog,dict)or catalog.get('Schema')!='HadesOriginalText1':raise ValueError('Original text catalog schema/suffix mismatch')
 def literal(node):
  if isinstance(node,dict):
   if any(not isinstance(k,str)or k.startswith('__')for k in node):raise ValueError('Nonliteral original text catalog')
   for value in node.values():literal(value)
  elif isinstance(node,list):
   for value in node:literal(value)
  elif not isinstance(node,(str,int,float,bool,type(None)))or isinstance(node,(int,float))and not math.isfinite(node):raise ValueError('Nonliteral original text catalog value')
 literal(catalog);return catalog

def read_native_menu_tooltip_templates(menu_path,ingame_path):
 from build_room_scene import Sjson,merge
 menu=Sjson(Path(menu_path).read_text()).parse()['MenuScreen']
 ingame=Sjson(Path(ingame_path).read_text()).parse()['InGameUI']
 def scalars(node):return {k:v for k,v in node.items()if not isinstance(v,(dict,list))}
 result={'Schema':'HadesNativeMenuTooltips1','SlotCount':6,'ScreenDataName':'InGameUI',
         'MenuScreenDefaults':scalars(menu),'InGameUIOverrides':scalars(ingame)}
 # MenuScreen.Load19d709..19d7d2 allocates six slots. The original screen
 # loader merges named component templates before GUIComponent::Load.
 for name in ('Backing','Header','Body'):
  key='Tooltip'+name;base=menu.get(key);override=ingame.get(key,{})
  if not isinstance(base,dict)or not isinstance(override,dict):raise ValueError('Original native tooltip template unavailable: '+key)
  result[name]=merge(base,override)
 return result

def validate_text_resources(manifest):
 manifest=Path(manifest);data=json.loads(manifest.read_text())
 if data['schema']!='hades-original-text-v1' or data['target_sha256']!='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254':raise ValueError('Original text source target mismatch')
 checked={}
 for item in [data['catalog'],*data['files'],*data['sources']]:
  p=Path(item['path'])
  key=str(p.resolve());value=(item['bytes'],item['sha256'])
  if key in checked:
   if checked[key]!=value:raise ValueError('Conflicting original text source identities')
   continue
  if not p.is_file()or p.is_symlink():raise ValueError('Original text input must be a regular file: '+str(p))
  if p.stat().st_size!=item['bytes'] or hashlib.file_digest(p.open('rb'),'sha256').hexdigest()!=item['sha256']:raise ValueError('Original text source changed: '+str(p))
  checked[key]=value
 catalog=read_text_catalog(data['catalog']['path'])
 files={}
 for row in data['files']:
  p=Path(row['path']).resolve()
  if not p.is_relative_to((manifest.parent/'text-fonts').resolve()):raise ValueError('Original text payload outside pack')
  relative=p.relative_to(manifest.parent.resolve()).as_posix()
  if relative in files:raise ValueError('Duplicate original text payload')
  files[relative]=row
 if set(files)!={p.relative_to(manifest.parent).as_posix()for p in (manifest.parent/'text-fonts').rglob('*')if p.is_file()}:raise ValueError('Original text payload inventory differs from manifest')
 expected=set()
 for font in catalog['Fonts'].values():expected.update((font['Image'],font['Metrics']))
 for symbol in catalog['Symbols'].values():expected.add(symbol['Image'])
 if expected!=set(files):raise ValueError('Original font/symbol payload closure mismatch')
 if 'NativeMenuTooltips'in catalog:
  proof=data.get('native_menu_tooltips')
  if not isinstance(proof,dict)or set(proof)!= {'menu_source','ingame_source','contract','graphic','atlas','sprite','xnb_sha256'}:raise ValueError('Native tooltip source binding missing')
  for key in ('menu_source','ingame_source','contract'):
   if proof[key]not in data['sources']:raise ValueError('Native tooltip source not retained')
  contract=json.loads(Path(proof['contract']['path']).read_text())
  if contract.get('schema')!='hades-screen-tooltip-v1'or contract.get('target_sha256')!=TARGET:raise ValueError('Native tooltip recovery contract mismatch')
  templates=read_native_menu_tooltip_templates(proof['menu_source']['path'],proof['ingame_source']['path'])
  if catalog['NativeMenuTooltips']!=templates:raise ValueError('Native tooltip templates differ from original source')
  graphic=templates['Backing'].get('Graphic');symbol=catalog['Symbols'].get(graphic)
  if proof['graphic']!=graphic or not symbol:raise ValueError('Native tooltip original backing unavailable')
  from hades_assets import Package
  sources=[Path(row['path'])for row in data['sources']if Path(row['path']).name=='GUI.pkg_manifest']
  if len(sources)!=1:raise ValueError('Native tooltip GUI manifest identity')
  matches=[(entry['name'],sprite)for entry in Package(sources[0]).entries()for sprite in entry.get('sprites',[])if sprite['name']==graphic]
  if matches!=[(proof['atlas'],proof['sprite'])]:raise ValueError('Native tooltip original sprite differs/ambiguous')
  packages=[Path(row['path'])for row in data['sources']if Path(row['path']).name=='GUI.pkg']
  if len(packages)!=1:raise ValueError('Native tooltip GUI package identity')
  carriers=[hashlib.sha256(entry['data']).hexdigest()for entry in Package(packages[0]).entries()if entry.get('name')==proof['atlas']and entry.get('type')=='texture']
  if carriers!=[proof['xnb_sha256']]:raise ValueError('Native tooltip original atlas carrier differs/ambiguous')
  sprite=proof['sprite'];x,y,w,h=sprite['rect']
  if sprite['flags']&3 or min(x,y)<0 or min(w,h)<=0:raise ValueError('Unsupported native tooltip sprite transform/bounds')
  expected_symbol={'Image':symbol['Image'],'Width':w,'Height':h,'OriginalWidth':sprite['original'][0],'OriginalHeight':sprite['original'][1],'TrimX':sprite['trim'][0],'TrimY':sprite['trim'][1],'ScaleRatioX':sprite['scale_ratio'][0],'ScaleRatioY':sprite['scale_ratio'][1]}
  if symbol!=expected_symbol:raise ValueError('Native tooltip symbol geometry differs from original sprite')
  image=Path(files[symbol['Image']]['path'])
  with image.open('rb')as stream:header=stream.read(12)
  if header!=b'UIR1'+struct.pack('<II',w,h)or image.stat().st_size!=12+w*h*4:raise ValueError('Native tooltip RGBA payload geometry mismatch')
  for name in ('Header','Body'):
   if templates[name].get('Font')not in catalog['Fonts']:raise ValueError('Native tooltip original font unavailable')
 elif 'native_menu_tooltips'in data:raise ValueError('Native tooltip proof lacks runtime catalog')
 return data,catalog

def publish_text_resource(item,target):
 """Replace directory entries only; existing payloads can share read-only inodes."""
 target=Path(target)
 if target.is_symlink():raise ValueError('Original text stage target cannot be a symlink: '+str(target))
 def matches(path):
  return path.is_file()and path.stat().st_size==item['bytes']and hashlib.file_digest(path.open('rb'),'sha256').hexdigest()==item['sha256']
 if matches(target):return False
 target.parent.mkdir(parents=True,exist_ok=True)
 temporary=None
 try:
  # No copy/truncate/chmod of the destination: replacing a directory entry
  # leaves all older exports and other aliases to its inode untouched.
  with tempfile.NamedTemporaryFile(prefix='.'+target.name+'-',suffix='.tmp',dir=target.parent,delete=False)as output:
   temporary=Path(output.name)
   with Path(item['path']).open('rb')as source:shutil.copyfileobj(source,output,1024*1024)
   output.flush();os.fsync(output.fileno())
  if not matches(temporary):raise ValueError('Original text copy changed/truncated before publication: '+str(target))
  os.replace(temporary,target);temporary=None
  return True
 finally:
  if temporary is not None:temporary.unlink(missing_ok=True)

def stage_text_resources(manifest,assets):
 manifest=Path(manifest);assets=Path(assets);data,_=validate_text_resources(manifest)
 root=Path(__file__).resolve().parents[2]
 protected=[manifest.parent.resolve(),(root/'.local/game').resolve(),(root/'.local/native-assets').resolve()]
 if any(assets.resolve().is_relative_to(path)for path in protected):raise ValueError('Original text stage cannot overwrite original files or immutable exports')
 if (assets/'text-fonts').is_symlink():raise ValueError('Original text stage payload directory cannot be a symlink')
 published=0
 for row in data['files']:
  relative=Path(row['path']).resolve().relative_to(manifest.parent.resolve());target=assets/relative
  published+=publish_text_resource(row,target)
 # Readers discover the new resource table only after every referenced payload
 # is verified and published. Identical old hardlinks remain entirely untouched.
 catalog_published=publish_text_resource(data['catalog'],assets/'text-fonts.lua')
 return {'fonts':len(data['fonts']),'localization_count':data['localization_count'],'published_payloads':published,'catalog_published':catalog_published}
