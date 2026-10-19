#!/usr/bin/env python3
"""Matching-PDB joined primitive Projectile constructor state, never target execution."""
from pathlib import Path
import hashlib,json,re,struct
ROOT=Path(__file__).resolve().parents[2];R=ROOT/'.local/reports'
ctor=json.loads((R/'native-player-projectile-base-ctor.json').read_text())
pdb=(R/'native-player-projectile-classes-pdb.txt').read_text()
line=next(x for x in pdb.splitlines()if 'FieldList' in x and 'RawString("mClipShotsRemaining")'not in x and 'RawString("mHitMaxRange")'in x)
fields=re.findall(r'field_type: TypeIndex\((0x[0-9a-f]+)\), offset: (\d+), name: RawString\("([^"]+)"\)',line)
mem={};proof={}
for instruction in ctor['assembly'].splitlines():
 m=re.match(r'^([0-9a-f]+):.*?\t(mov[bwlq])\s+(\$-?0x[0-9a-f]+|%[re]?cx|%cl), (0x[0-9a-f]+)\(%rbx\)',instruction)
 if not m:continue
 va,op,source,offset=m.groups();n={'movb':1,'movw':2,'movl':4,'movq':8}[op];offset=int(offset,16)
 value=int(source[1:],0)if source.startswith('$')else 0
 for i,v in enumerate((value%(1<<(n*8))).to_bytes(n,'little')):mem[offset+i]=v;proof[offset+i]={'va':hex(int(va,16)),'instruction':instruction}
known=[];unknown=[]
for typ,off,name in fields:
 off=int(off);typ=int(typ,16);kind={0x30:('bool',1,'?'),0x74:('int32',4,'i'),0x40:('float32',4,'f'),0x1031:('vector2f',8,'ff')}.get(typ)
 if not kind:unknown.append({'name':name,'offset':off,'type_index':typ,'reason':'complex pointer/container state not represented as invented bytes'});continue
 t,n,fmt=kind
 if not all(off+i in mem for i in range(n)):unknown.append({'name':name,'offset':off,'type_index':typ,'reason':'constructor write coverage incomplete'});continue
 values=struct.unpack('<'+fmt,bytes(mem[off+i]for i in range(n)))
 known.append({'name':name,'offset':off,'type':t,'value':list(values)if len(values)>1 else values[0],'proof':[json.loads(s)for s in sorted({json.dumps(proof[off+i],sort_keys=True)for i in range(n)})]})
out={'schema':'hades-projectile-live-constructor-defaults-v1','dll_sha256':ctor['dll_sha256'],'pdb_identity':ctor['pdb_identity'],'constructor_rva':'0x696a0','source_report':'.local/reports/native-player-projectile-base-ctor.json','layout_report':'.local/reports/native-player-projectile-classes-pdb.txt','fields':known,'unrepresented_fields':unknown,'phase':'constructor only; Projectile::Init overwrites many values before queue registration','limits':['Complete retained constructor instruction coverage, matching PDB primitive offsets; no target execution.','This is not initialized or active projectile state. No lifetime/Thing ID allocated.','Parent must call actual Thing.Init and source-backed Projectile.Init operations; speed/range/physics/fuse and callbacks cannot be inferred from constructor zeroes.']}
p=R/'native-player-projectile-live-defaults.json';p.write_text(json.dumps(out,indent=2,allow_nan=False)+'\n');print('known',len(known),'unrepresented',len(unknown),p)
