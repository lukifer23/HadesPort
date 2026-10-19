#!/usr/bin/env python3
"""Export all original fullscreen bloom presets with lossless float32 values."""
import argparse,json,math,struct
from pathlib import Path
from export_room_creation import RangedSjson
from export_actor_catalog import identity
FIELDS=('BloomThreshold','BlurAmount','BloomIntensity','BaseIntensity','BloomSaturation','BaseSaturation')
def export(content,output):
 if output.exists()or output.is_symlink():raise ValueError('Fresh bloom output required')
 if output.resolve().is_relative_to(content.resolve().parent):raise ValueError('Bloom output must be outside originals')
 source=content/'Game/BloomSettings.sjson';rows=RangedSjson(source.read_text(encoding='utf-8-sig')).parse()['BloomSettings'];names=set();packet=bytearray(b'HBL1'+struct.pack('<I',len(rows)))
 for r in rows:
  if set(r)!={'Name',*FIELDS}:raise ValueError('Unexpected/missing bloom fields')
  name=r['Name'];key=name.lower();raw=name.encode('utf-8');values=[r[k]for k in FIELDS]
  if not raw or len(raw)>256 or '\0'in name or key in names or any(type(v)not in [int,float]or not math.isfinite(v)for v in values):raise ValueError('Invalid bloom definition')
  names.add(key);packet+=struct.pack('<I',len(raw))+raw+struct.pack('<6f',*values)
 if 'off'not in names:raise ValueError('Missing original Off preset')
 output.mkdir(parents=True);path=output/'bloom-settings.bin';path.write_bytes(packet)
 receipt={'schema':'hades-original-bloom-presets-v1','source':identity(source),'packet':identity(path),'count':len(rows),'fields':FIELDS,'storage':'HBL1/u32count then u32 UTF8name bytes and six little-endian float32 values in fields order','limits':['Original presets only; native transition and Android rendering verified separately.']}
 (output/'manifest.json').write_text(json.dumps(receipt,indent=2)+'\n');return receipt
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--content',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();print(json.dumps(export(a.content,a.output),indent=2))
