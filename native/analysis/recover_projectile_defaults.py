#!/usr/bin/env python3
"""Bounded constant-store recovery of matching original ProjectileData defaults."""
import json,re,struct,hashlib,argparse
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];R=ROOT/'.local/reports'
args=argparse.ArgumentParser();args.add_argument('--output',type=Path,default=R/'native-player-projectile-compiled-defaults.json');args.add_argument('--include-enums',action='store_true');args.add_argument('--enum-pdb-report',type=Path,default=R/'native-engine-pdb-actor-next.json');options=args.parse_args()
SHA='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'
assert hashlib.sha256((ROOT/'.local/game/base/x64/EngineWin64s.dll').read_bytes()).hexdigest()==SHA
pdb=json.loads((R/'native-engine-pdb-player-weapons.json').read_text());by={x['index']:x for x in pdb['selected_types']}
klass=next(x for x in by.values()if x.get('name')=='sgg::ProjectileDataDef'and'fields: Some'in x['debug']);fi=int(re.search(r'fields: Some\(TypeIndex\((0x[\da-f]+)',klass['debug'])[1],16);size=int(re.search(r'size: (\d+)',klass['debug'])[1]);assert size==0x718
ctor=json.loads((R/'native-player-projectile-default-ctor.json').read_text());assign=json.loads((R/'native-player-projectile-def-assignment.json').read_text());assert ctor['dll_sha256']==assign['dll_sha256']==SHA
ops=[]
for line in ctor['assembly'].splitlines():
 m=re.match(r'^([\da-f]+):.*?\t([a-z][a-z0-9]*)\s*(.*?)(?:\s+#.*)?$',line)
 if m:ops.append((int(m[1],16),m[2],m[3].strip(),line))
assert any(va==0x1800d842e and'-0x650(%rsp), %rbp'in args for va,op,args,line in ops)
assert any(va==0x1800d8436 and'$0x750, %rsp'in args for va,op,args,line in ops)
assert any(va==0x1800d89d5 and'0x20(%rsp), %rdx'in args for va,op,args,line in ops)
assert any(va==0x1800d89e1 and'0x50(%rdi), %rcx'in args for va,op,args,line in ops)
mem=[None]*size;prov=[None]*size;regs={};temp_origins={};assignment_va=0x1800d8a6e
aliases={'%eax':'%rax','%al':'%rax','%r14d':'%r14','%r14b':'%r14','%ecx':'%rcx','%cl':'%rcx','%edx':'%rdx','%dl':'%rdx'}
def put(off,value,n,proof):
 if off<0 or off+n>size:return
 b=(value%(1<<(n*8))).to_bytes(n,'little')
 for i,v in enumerate(b):mem[off+i]=v;prov[off+i]=proof
for va,op,args,line in ops:
 if va<0x1800d8488:continue
 if va==assignment_va:temp_origins=prov.copy();continue
 if va>assignment_va:
  m=re.match(r'(\$-?0x[\da-f]+|%r14[db]?), (0x[\da-f]+)\(%rdi\)$',args)
  if m and op in ['movb','movw','movl','movq']:
   val=int(m[1][1:],0)if m[1].startswith('$')else 0
   put(int(m[2],16)-0x50,val,{'movb':1,'movw':2,'movl':4,'movq':8}[op],{'kind':'post_assignment_override','va':hex(va),'instruction':line})
  continue
 if op in ['xorl','xorps']:
  a=args.split(', ')
  if len(a)==2 and a[0]==a[1]:regs[aliases.get(a[1],a[1])]=(0,va)
 elif re.search(r', %(?:rax|eax|xmm0|xmm1|r14|r14d)$',args):
  dest=aliases.get(args.split(', ')[-1],args.split(', ')[-1]);regs.pop(dest,None)
 m=re.match(r'(.+), (-?0x[\da-f]+)?\(%(rsp|rbp)\)$',args)
 if m and op in ['movb','movw','movl','movq','movdqu','movups']:
  src,disp,base=m.groups();off=int(disp or'0',0)+(0x100 if base=='rbp'else 0)-0x20
  if src.startswith('$'):value=int(src[1:],0);definition=va
  elif aliases.get(src,src)in regs:value,definition=regs[aliases.get(src,src)]
  else:continue
  put(off,value,{'movb':1,'movw':2,'movl':4,'movq':8,'movdqu':16,'movups':16}[op],{'kind':'temporary_constant_store','va':hex(va),'constant_va':hex(definition),'instruction':line})
 if op=='callq':
  if va in [0x1800d86f7,0x1800d8776,0x1800d8974]:
   off,n={0x1800d86f7:(0x288,0x68),0x1800d8776:(0x350,0xa8),0x1800d8974:(0x5c0,0x60)}[va];assert '0x18049589a'in args
   for i in range(n):mem[off+i]=0;prov[off+i]={'kind':'memset_zero','va':hex(va),'offset':hex(off),'bytes':n,'argument_evidence':'native-player-projectile-default-ctor.json'}
  regs.pop('%rax',None);regs.pop('%xmm0',None);regs.pop('%xmm1',None)
fields=[];unknown=[]
primitive={0x30:('bool',1,'?'),0x40:('float32',4,'f'),0x74:('int32',4,'i'),0x75:('uint32',4,'I'),0x20:('uint8',1,'B'),0x70:('char',1,'b')}
enums={}
if options.include_enums:
 enum_path=options.enum_pdb_report.resolve();enum_data=json.loads(enum_path.read_text());enum_by={x['index']:x for x in enum_data['selected_types']}
 for idx in (0xdc54,0xd780,*((0xdc56,) if 0xdc56 in enum_by else ())):
  t=enum_by[idx];underlying=int(re.search(r'underlying_type: TypeIndex\((0x[\da-f]+)',t['debug'])[1],16);fi_enum=int(re.search(r'fields: TypeIndex\((0x[\da-f]+)',t['debug'])[1],16)
  values={name:int(value)for value,name in re.findall(r'value: [UI]\d+\((\d+)\), name: RawString\("([^"]+)"\)',enum_by[fi_enum]['debug'])}
  assert values and underlying in primitive
  enums[idx]={'type_index':hex(idx),'name':t['name'],'values':values,'pdb_report':str(enum_path.relative_to(ROOT)),'pdb_report_sha256':hashlib.sha256(enum_path.read_bytes()).hexdigest(),'primitive':primitive[underlying]}
for typ,off,name in re.findall(r'field_type: TypeIndex\((0x[\da-f]+)\), offset: (\d+), name: RawString\("([^"]+)"\)',by[fi]['debug']):
 typ=int(typ,16);off=int(off);key=name[1:];p=primitive.get(typ);enum=None
 if key=='SpawnType':p=('uint8',1,'B');enum={'type_index':hex(typ),'meaning':'Underlying byte proven by native init cmpbyte/constructor post-store; enum names not inferred.'}
 if typ in enums:p=enums[typ]['primitive'];enum={k:v for k,v in enums[typ].items()if k!='primitive'}
 if p:t,n,fmt=p
 elif typ==0x10ad:t,n,fmt='string',24,None
 else:unknown.append({'name':key,'offset':off,'type_index':typ,'reason':'Complex/enum type outside bounded scalar reader'});continue
 chunk=mem[off:off+n]
 if any(x is None for x in chunk):unknown.append({'name':key,'offset':off,'type_index':typ,'reason':'Missing proven byte stores'});continue
 data=bytes(chunk)
 if fmt:value=struct.unpack('<'+fmt,data)[0]
 elif data[0]==0 and data[-1]==0x17:value=''
 else:unknown.append({'name':key,'offset':off,'type_index':typ,'reason':'Not proven empty EASTL string'});continue
 proof=[json.loads(s)for s in sorted({json.dumps(x,sort_keys=True)for x in prov[off:off+n]})]
 fields.append({'name':key,'type':t,'offset':off,'type_index':typ,'enum':enum,'value':value,'provenance':{'kind':'compiled_native_default','stores':proof,'assignment_call_va':hex(assignment_va),'assignment_rva':'0xd9730','temporary_base':'post-prologueRSP+0x20;RBP=RSP+0x100'}})
out={'schema':'hades-projectile-compiled-defaults-v1','target_sha256':SHA,'constructor_rva':'0xd8420','fields':fields,'unknown_fields':unknown,'limits':['Bounded static constant stores; unknown bytes/complex types remain explicit. No target execution.','Includes original compiled defaults before source inheritance/read/postprocessing overrides. Nested ThingData pointer/initialization is not serialized as a fake pointer.']}
options.output.write_text(json.dumps(out,indent=2)+'\n');print('recovered',len(fields),'unknown',len(unknown))
for f in fields:
 if f['name']in ['SpawnType','SpawnOnDeath','SpawnOnImpact','SpawnOnDetonate','Damage','DamageLow','DamageHigh','Speed','Range','Fuse']:print(f['name'],f['value'])
