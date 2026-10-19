#!/usr/bin/env python3
"""Retain exact original inverse-isometric lookup and source angle metadata."""
import argparse,hashlib,json,math,struct
from pathlib import Path
DLL_SHA='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'
def identity(p):return {'path':str(p),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
def export(a):
 if a.output.exists():raise ValueError('New output required')
 dll=a.dll.read_bytes()
 if hashlib.sha256(dll).hexdigest()!=DLL_SHA:raise ValueError('Exact source DLL required')
 pe=struct.unpack_from('<I',dll,60)[0];n=struct.unpack_from('<H',dll,pe+6)[0];opt=struct.unpack_from('<H',dll,pe+20)[0];rva=0x4cf140;fo=None
 for i in range(n):
  _,vr,rs,at=struct.unpack_from('<IIII',dll,pe+24+opt+40*i+8)
  if vr<=rva and rva+2880<=vr+rs:fo=at+rva-vr
 if fo is None:raise ValueError('LUT outside file bounds')
 raw=dll[fo:fo+2880];lut=struct.unpack('<720f',raw)
 if not all(math.isfinite(x)and 0<=x<=2*math.pi for x in lut):raise ValueError('Unexpected native angle LUT')
 m=json.loads((a.bank/'manifest.json').read_text());packet=a.bank/'actor-animations.bin';bank_sha=hashlib.sha256(packet.read_bytes()).digest()
 records=[]
 for c in m['clips']:
  d=c['definition']
  if d.get('AngleNumAngleOffset',0)!=0 or d.get('AngleNumOffset',0)!=0 or d.get('Angle',0)!=0:raise ValueError('Selected nonzero angle offsets require further native loader verification')
  records.append({'name':c['name'],'use_isometric_angle':d.get('UseIsometricAngle',True),'own_angle_radians':0.,'angle_offset_radians':0.,'angle_num_offset':0,'mirror':d.get('Mirror',False),'ignore_owner_angle':d.get('AngleFromOwner')=='Ignore'})
 a.output.mkdir(parents=True)
 out=a.output/'actor-facing.bin'
 with out.open('xb')as f:
  f.write(b'HAF1'+bank_sha+struct.pack('<II',len(records),720)+raw)
  for r in records:f.write(struct.pack('<IffiII',r['use_isometric_angle'],r['own_angle_radians'],r['angle_offset_radians'],r['angle_num_offset'],r['mirror'],r['ignore_owner_angle']))
 report={'schema':'hades-native-actor-facing-v1','packet':identity(out),'bank':identity(packet),'source':identity(a.dll),'source_range':{'rva':hex(rva),'va':'0x1804cf140','file_offset':fo,'bytes':2880,'sha256':hashlib.sha256(raw).hexdigest()},'records':records,'format':'HAF1+32raw bankSHA+u32clipCount+u32LUTCount720+720float; perclip u32useIso,floatownRadians,floatangleOffsetRadians,int32angleNumOffset,u32mirror,u32ignoreOwner','native_index':'trunc(float32(float32(angleRadians*float32bits42e52ee6)+0.5)) modulo720; fetch exact native LUT. GetAngleNum rounds by half native angle interval, adds native AngleNumOffset, wraps then mirrors when configured.','selected_export_angle':'After exact native full-angle index, nearest circular selected original angle; tie retains first exported angle. Eight-direction selection is an explicit asset-coverage reduction, not the full native32/64directions.','evidence':['.local/reports/native-actor-animation-angle-num.json','.local/reports/native-actor-animation-inverse-isometric-angle.json','.local/reports/native-lighting-AnimationCtor-static.json'],'defaults':'UseIsometricAngle=true atVA180094170; Mirror=false atVA180094214; source absent angle/offset zero from native constructor cleared template. No lookup values invented.'}
 (a.output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report['packet']))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--dll',type=Path,required=True);p.add_argument('--bank',type=Path,required=True);p.add_argument('--output',type=Path,required=True);export(p.parse_args())
