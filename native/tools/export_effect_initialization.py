#!/usr/bin/env python3.13
"""Export source-bound Laurel Constant initialization and original invisible texel."""
import argparse,hashlib,json
from pathlib import Path
from hades_assets import Package,xnb_image
from build_room_scene import identity

def lua(v):
 if v is None:return'nil'
 if isinstance(v,bool):return'true'if v else'false'
 if isinstance(v,(int,float)):return repr(v)
 if isinstance(v,str):return json.dumps(v,ensure_ascii=True)
 if isinstance(v,list):return'{'+','.join(lua(x)for x in v)+'}'
 return'{'+','.join('['+lua(k)+']='+lua(x)for k,x in v.items())+'}'
def export(a):
 if a.output.exists():raise ValueError('New output required')
 deps=json.loads(a.dependencies.read_text());source=Path(deps['source']['path'])
 if identity(source)['sha256']!=deps['source']['sha256']:raise ValueError('Source changed')
 packages=a.content/'Win/Packages';meta=None;atlas=None
 for e in Package(packages/'Dev.pkg_manifest').entries():
  for s in e.get('sprites',[]):
   if s['name']=='Dev\\blank_invisible':
    if meta:raise ValueError('Ambiguous original texel')
    meta=s;atlas=e['name']
 if not meta:raise ValueError('Missing original blank graphic')
 rgba=None;xnb_sha=None
 for e in Package(packages/'Dev.pkg').entries():
  if e['type']=='texture'and e['name']==atlas:
   im,info=xnb_image(e['data']);x,y,w,h=meta['rect'];rgba=list(im.crop((x,y,x+w,y+h)).tobytes());xnb_sha=hashlib.sha256(e['data']).hexdigest();break
 if rgba is None or len(rgba)!=4 or rgba[3]!=0:raise ValueError('Original invisible graphic outside selected scope')
 defaults={'VisualFxIntervalMin':0.,'VisualFxIntervalMax':0.,'VisualFxDistanceMin':0.,'VisualFxDistanceMax':0.,'VisualFxDistanceMinChange':0.,'VisualFxAngleOffset':0.,'VisualFxOffsetX':0.,'VisualFxOffsetY':0.,'VisualFxFrameCap':0,'VisualFxManagerCap':0,'ActiveVisualFxCap':0,'VisualFxCap':0,'VisualFxCapMin':0,'VisualFxCapMax':0,'Scale':1.,'OffsetX':0.,'OffsetY':0.,'OffsetZ':0.,'Angle':0.,'GroupName':'','ChildAnimation':'','CreateAnimation':'','VisualFx':'','UseAttachedMarkerLocation':'','Loop':False}
 definitions={r['name']:r['resolved']for r in deps['records']}
 catalog={'Schema':'HadesEffectInitialization1','Definitions':definitions,'NativeDefaults':defaults,'CompiledConfig':{'VfxFrameCap':60,'VfxManagerCapMultiplier':4.},'Texture':{'Name':'Dev\\blank_invisible','Atlas':atlas,'Width':1,'Height':1,'RGBA':rgba,'AtlasRect':meta['rect'],'Trim':meta['trim'],'OriginalSize':meta['original'],'ScaleRatio':meta['scale_ratio']}}
 a.output.mkdir(parents=True);(a.output/'effects.lua').write_text('return '+lua(catalog)+'\n');(a.output/'catalog.json').write_text(json.dumps(catalog,indent=2)+'\n');(a.output/'blank-original.rgba').write_bytes(bytes(rgba))
 report={'schema':'hades-laurel-initialization-export-v1','catalog':identity(a.output/'catalog.json'),'lua':identity(a.output/'effects.lua'),'dependencies':identity(a.dependencies),'sources':[identity(source),identity(packages/'Dev.pkg_manifest'),identity(packages/'Dev.pkg')],'original_graphic':{'atlas':atlas,'sprite':meta,'source_xnb_sha256':xnb_sha,'decoded_rgba':rgba},'defaults':'Only reviewed selected scalar/string defaults for Constant-spawner init, from exact AnimationData ctor zero template/direct overrides. Full leaf motion defaults remain outside this export.','compiled_config_reports':['.local/reports/native-effect-config-VfxFrameCap.json','.local/reports/native-effect-config-VfxManagerCapMultiplier.json']};(a.output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'lua':report['lua'],'definitions':len(definitions)}))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--content',type=Path,required=True);p.add_argument('--dependencies',type=Path,required=True);p.add_argument('--output',type=Path,required=True);export(p.parse_args())
