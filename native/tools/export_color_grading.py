#!/usr/bin/env python3
"""Export original SGG type170/XNB6 color volumes without modifying originals."""
import argparse,hashlib,json,struct,zlib
from pathlib import Path
from hades_assets import Package,Reader
from export_room_creation import RangedSjson
from export_actor_catalog import identity

def volume(data):
 r=Reader(data)
 if r.read(4)!=b'XNBw' or r.u8()!=6 or r.u8()!=0 or r.uint('<')!=len(data):raise ValueError('Expected uncompressed Windows XNB6 volume')
 fmt,w,h,d,n=(r.uint('<')for _ in range(5))
 if fmt!=0 or not 2<=w<=64 or w!=h or w!=d or n!=w*h*d*4:raise ValueError('Unsupported original color volume format/dimensions')
 bgra=r.read(n)
 if r.pos!=len(data):raise ValueError('Trailing original color-volume bytes')
 rgba=bytearray(bgra);rgba[0::4]=bgra[2::4];rgba[2::4]=bgra[0::4]
 return w,bytes(rgba)

def export(content,output):
 if output.exists()or output.is_symlink():raise ValueError('Fresh output required')
 if output.resolve().is_relative_to(content.resolve().parent):raise ValueError('Output must be outside originals')
 source=content/'Game/ColorGrading.sjson';rows=RangedSjson(source.read_text(encoding='utf-8-sig')).parse()['ColorGradings']
 names={r['Name']:Path(r['FilePath'].replace('\\','/')).stem for r in rows}
 if len(names)!=len(rows)or len({n.lower()for n in names})!=len(names):raise ValueError('Duplicate source grading names')
 package=content/'Win/Packages/Dev.pkg';wanted=set(names.values());found={}
 for e in Package(package).entries():
  if e['type']!='texture3d'or e['name']not in wanted:continue
  if e['name']in found:raise ValueError('Ambiguous source LUT')
  size,pixels=volume(e['data']);found[e['name']]={'size':size,'rgba':pixels,'source':{'name':e['name'],'location':e['location'],'xnb_sha256':hashlib.sha256(e['data']).hexdigest(),'xnb_bytes':len(e['data'])}}
 if wanted-set(found):raise ValueError('Missing original LUTs '+str(wanted-set(found)))
 output.mkdir(parents=True);packet=output/'color-grading.bin';records=[]
 with packet.open('xb')as out:
  out.write(b'HCG1'+struct.pack('<I',len(names)))
  for name,image in names.items():
   lut=found[image];rgba=lut['rgba'];packed=zlib.compress(rgba,6);encoded=name.encode('utf-8')
   out.write(struct.pack('<I',len(encoded))+encoded+struct.pack('<4I',lut['size'],len(rgba),len(packed),zlib.crc32(rgba))+packed)
   records.append({'name':name,'image':image,'size':lut['size'],'rgba_sha256':hashlib.sha256(rgba).hexdigest(),'source':lut['source']})
 manifest={'schema':'hades-original-color-grading-v1','target_sha256':'d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254','source':identity(source),'package':identity(package),'packet':identity(packet),'records':records,'storage':'x-fastest rows and depth slices; original BGRA bytes reordered losslessly to RGBA; zlib per volume','limits':['Resource extraction does not itself establish runtime transition or shader execution.']}
 (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');return {'packet':manifest['packet'],'original_luts':len(records)}
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--content',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();print(json.dumps(export(a.content,a.output),indent=2))
