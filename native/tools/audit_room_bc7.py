#!/usr/bin/env python3.13
"""Independently decode each original atlas used by stable v5; no asset edits."""
import ctypes,hashlib,json,sys
from pathlib import Path
import PIL
from hades_assets import Package,Reader,xnb_image
ROOT=Path(__file__).resolve().parents[2]
def identity(p):
 b=p.read_bytes();return {'path':str(p.relative_to(ROOT)),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
def main():
 out=ROOT/'.local/reports/native-room-bc7-independent-audit.json'
 if out.exists():raise ValueError('Output exists')
 scene=ROOT/'.local/native-assets/opening-fx-v5/scene-manifest.json';m=json.loads(scene.read_text());expected=dict(m['decoded_atlases']);expected.update({x['atlas']:x for x in m['decoded_fx_atlases']});results=[]
 tools=ROOT/'.local/tools/bcdec-80859ed3';lib=ctypes.CDLL(str((tools/'libbcdec.dylib').resolve()));lib.decode_bc7_image.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_int,ctypes.c_int];lib.decode_bc7_image.restype=None
 for package in ['Tartarus','Fx']:
  for e in Package(ROOT/'.local/game/base/Content/Win/Packages'/f'{package}.pkg').entries():
   if e['type']!='texture'or e['name']not in expected:continue
   data=e['data'];x=expected[e['name']];assert hashlib.sha256(data).hexdigest()==x['xnb_sha256'];p=Reader(data);assert p.read(4)==b'XNBw';version=p.u8();assert p.u8()==0;assert p.uint('<')==len(data)
   if version==5:
    for _ in range(p.varint()):p.xnbstring();p.integer('<')
    p.varint();p.varint()
   else:assert version==6
   fmt,w,h,mips,n=[p.integer('<')for _ in range(5)];assert fmt==28 and w%4==h%4==0;compressed=p.read(n);assert n==(w//4)*(h//4)*16
   output=ctypes.create_string_buffer(w*h*4);lib.decode_bc7_image(compressed,output,w,h);independent=output.raw;original=xnb_image(data)[0].tobytes();same=original==independent
   entry={'package':package,'atlas':e['name'],'source_xnb_sha256':x['xnb_sha256'],'dimensions':[w,h],'mips':mips,'RGBA_identical':same,'Pillow_RGBA_sha256':hashlib.sha256(original).hexdigest(),'bcdec_RGBA_sha256':hashlib.sha256(independent).hexdigest()}
   if not same:entry.update({'max_channel_difference':max(abs(a-b)for a,b in zip(original,independent)),'different_channels':sum(a!=b for a,b in zip(original,independent))})
   results.append(entry)
 assert len(results)==len(expected),(len(results),len(expected))
 report={'schema':'hades-independent-bc7-audit-v1','scene_manifest':identity(scene),'Pillow_version':PIL.__version__,'independent_decoder':{'url':'https://github.com/iOrange/bcdec','commit':'80859ed3b7afb1c527a2a99d70c61457bea72d0c','header':identity(tools/'bcdec.h'),'wrapper':identity(tools/'wrapper.c'),'build':'clang -O2 -dynamiclib wrapper.c -o libbcdec.dylib','license':'MIT or Unlicense, preserved in pinned header'},'atlases':results,'all_identical':all(x['RGBA_identical']for x in results),'limits':['Checks original first-mip BC7 bytes to portable RGBA; no original game rendering/execution.','Does not prove original UV/sort/shader state.','All scene/animation originals remain unchanged.']}
 out.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'atlases':len(results),'all_identical':report['all_identical'],'report':str(out)}))
if __name__=='__main__':main()
