#!/usr/bin/env python3
"""Recover constant WeaponData ctor stores, without executing original machine code.

This is a bounded constant-store reader for this exact matching binary, not an
x86 emulator. Unknown bytes/types stay unknown; it never assumes zeroed stack.
"""
from pathlib import Path
import hashlib,json,re,struct
ROOT=Path(__file__).resolve().parents[2];REPORTS=ROOT/'.local/reports'
DLL=ROOT/'.local/game/base/x64/EngineWin64s.dll'
EXPECTED='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'
assert hashlib.sha256(DLL.read_bytes()).hexdigest()==EXPECTED
pdb_path=REPORTS/'native-engine-pdb-weapon-defaults-next.json'
if not pdb_path.exists():pdb_path=REPORTS/'native-engine-pdb-weapon-defaults.json'
if not pdb_path.exists():pdb_path=REPORTS/'native-engine-pdb.json'
pdb=json.loads(pdb_path.read_text());by={x['index']:x for x in pdb['selected_types']}
ctor=json.loads((REPORTS/'native-weapon-data-ctor.json').read_text())
move=json.loads((REPORTS/'native-weapon-def-move-assignment.json').read_text())
assert ctor['dll_sha256']==EXPECTED and move['dll_sha256']==EXPECTED
klass=next(x for x in by.values()if x.get('name')=='sgg::WeaponDataDef'and'fields: Some'in x['debug'])
field_index=int(re.search(r'fields: Some\(TypeIndex\((0x[\da-f]+)\)',klass['debug'])[1],16)
size=int(re.search(r'size: (\d+)',klass['debug'])[1]);assert size==0x960
members=[]
for m in re.finditer(r'Member\(MemberType \{ attributes: FieldAttributes\(\d+\), field_type: TypeIndex\((0x[\da-f]+)\), offset: (\d+), name: RawString\("([^\"]+)"\)',by[field_index]['debug']):
    members.append({'member':m[3],'name':m[3][1:],'type_index':int(m[1],16),'offset':int(m[2])})
def instructions(assembly):
    out=[]
    for line in assembly.splitlines():
        match=re.match(r'^([\da-f]+):.*?\t([a-z][a-z0-9]*)\s*(.*?)\s*(?:#.*)?$',line)
        if match:out.append((int(match[1],16),match[2],match[3].split('#')[0].strip(),line))
    return out
ops=instructions(ctor['assembly']);copy=instructions(move['assembly'])
temp=[None]*size;temp_prov=[None]*size
patches=[];constants={'%rax':None,'%r14':None,'%xmm0':None}
# After the constructor prologue RBP=RSP+0x100. Temporary WeaponDataDef is
# [RSP+0x20]. Assert the exact frame and assignment argument instructions.
assert any(a==0x1800f753e and'leaq'==op and'-0x8a0(%rsp)'in args for a,op,args,_ in ops)
assert any(a==0x1800f7546 and'subq'==op and'$0x9a0, %rsp'in args for a,op,args,_ in ops)
assert any(a==0x1800f7d1c and'leaq'==op and'0x20(%rsp), %rdx'in args for a,op,args,_ in ops)
assert any(a==0x1800f7d28 and'leaq'==op and'0x50(%rdi), %rcx'in args for a,op,args,_ in ops)
assert any(a==0x1800f7e76 and'0x1800f87b0'in args for a,op,args,_ in ops)
def put(target,provenance,offset,data,origin):
    if offset<0 or offset+len(data)>size:return
    for i,byte in enumerate(data):target[offset+i]=byte;provenance[offset+i]=origin
for va,op,args,line in ops:
    if va<0x1800f7598:continue
    if va>=0x1800f7e76:break
    if op=='xorl'and args=='%eax, %eax':constants['%rax']=(0,va)
    if op=='xorl'and args=='%r14d, %r14d':constants['%r14']=(0,va)
    if op=='xorps'and args=='%xmm0, %xmm0':constants['%xmm0']=(0,va)
    match=re.match(r'(.+), (-?0x[\da-f]+)?\(%(rsp|rbp)\)$',args)
    if match and op in ['movq','movl','movw','movb','movdqu']:
        width={'movq':8,'movl':4,'movw':2,'movb':1,'movdqu':16}[op]
        operand,disp,base=match.groups();offset=int(disp or'0',0)+(0x100 if base=='rbp'else 0)-0x20
        constant=None;origin={'store_va':hex(va),'instruction':line}
        if operand.startswith('$'):constant=int(operand[1:],0)
        else:
            canonical={'%al':'%rax','%eax':'%rax','%r14b':'%r14','%r14d':'%r14'}.get(operand,operand)
            value=constants.get(canonical)
            if value is not None:constant,zero_va=value;origin['constant_definition_va']=hex(zero_va)
        if constant is not None:put(temp,temp_prov,offset,(constant% (1<<(width*8))).to_bytes(width,'little'),origin)
    if op=='callq':
        # Exactly two constant zero-fill calls, with checked register setup.
        if va in [0x1800f79b8,0x1800f7b8a]:
            offset,length=(0x3f8,0x78)if va==0x1800f79b8 else(0x608,0x98)
            assert '0x18049589a'in args
            put(temp,temp_prov,offset,bytes(length),{'store_va':hex(va),'kind':'memset_zero','offset':hex(offset),'bytes':length,'argument_setup':'native-weapon-data-ctor.json; RDX0,RCX=temporary+offset,R8=length'})
        constants['%rax']=None
# Move assignment transfers the initialized temporary into this.mDef (offset50),
# then explicit constructor stores override selected compiled defaults.
values=temp.copy();provenance=temp_prov.copy()
for va,op,args,line in ops:
    if va<0x1800f7e85:continue
    m=re.match(r'\$(-?0x[\da-f]+), (0x[\da-f]+)\(%rdi\)$',args)
    if m and op in ['movq','movl','movw','movb']:
        width={'movq':8,'movl':4,'movw':2,'movb':1}[op];offset=int(m[2],16)-0x50;number=int(m[1],0)
        put(values,provenance,offset,(number%(1<<(width*8))).to_bytes(width,'little'),{'store_va':hex(va),'kind':'post_assignment_constructor_override','instruction':line})
        patches.append({'offset':hex(offset),'bytes':width,'value_bits':hex(number%(1<<(width*8))),'va':hex(va)})
primitives={0x30:('bool',1,'?'),0x40:('float32',4,'f'),0x74:('int32',4,'i'),0x75:('uint32',4,'I'),0x20:('uint8',1,'B'),0x70:('char',1,'b')}
def transfer_proof(offset,width):
    pairs=[]
    for i,(va,op,args,line)in enumerate(copy):
        for j in range(i+1,min(len(copy),i+4)):
            va2,op2,args2,line2=copy[j]
            token=f'0x{offset:x}'if offset else''
            if re.search(r'(?<![0-9a-fx])'+re.escape(token)+r'\(%(?:rdx|r8)\)',args) and re.search(r'(?<![0-9a-fx])'+re.escape(token)+r'\(%(?:rcx|rbx)\)',args2):
                if args.split(',')[-1].strip() in args2.split(',')[0]:pairs.append({'read_va':hex(va),'write_va':hex(va2),'read':line,'write':line2})
    return pairs
fields=[];unknown=[]
for field in members:
    off=field['offset'];typ=field['type_index'];enum=None
    if typ in by and by[typ]['debug'].startswith('Enumeration'):
        debug=by[typ]['debug'];underlying=int(re.search(r'underlying_type: TypeIndex\((0x[\da-f]+)\)',debug)[1],16)
        enum={'name':by[typ].get('name'),'underlying':underlying};typ=underlying
    primitive=primitives.get(typ)
    if primitive:type_name,width,fmt=primitive
    elif typ==0x10ad:type_name,width,fmt='string',24,None
    elif field['name']=='Effects':type_name,width,fmt='empty_effect_vector',24,None
    else:unknown.append({**field,'reason':'PDB field type not in bounded scalar/string scope'});continue
    chunk=values[off:off+width]
    if any(x is None for x in chunk):unknown.append({**field,'reason':'Not every byte has a proven constant store','missing_byte_offsets':[hex(off+i)for i,v in enumerate(chunk)if v is None]});continue
    data=bytes(chunk)
    if fmt:value=struct.unpack('<'+fmt,data)[0]
    elif type_name=='string':
        if data[0]!=0 or data[23]!=0x17:unknown.append({**field,'reason':'Not proven empty EASTL short string'});continue
        value=''
    else:
        if data!=bytes(24):unknown.append({**field,'reason':'Effect vector is not proven empty'});continue
        value=[]
    origins={json.dumps(x,sort_keys=True)for x in provenance[off:off+width]};proof=transfer_proof(off,width)
    fields.append({**field,'type':type_name,'enum':enum,'value':value,'storage_bytes_hex':data.hex(),'provenance':{'kind':'compiled_native_default','temporary_stores':[json.loads(x)for x in sorted(origins)],'temporary_base':'post_prologueRSP+0x20; RBP=RSP+0x100','assignment_call_va':'0x1800f7e76','assignment_rva':'0xf87b0','copy_instructions':proof,'later_store_override':any(x.get('kind')=='post_assignment_constructor_override'for x in map(json.loads,origins))}})
report={'schema':'hades-weapon-compiled-defaults-v1','target_sha256':EXPECTED,'pdb_guid':'15f83bcc-e740-4d0b-bf67-6ac938c8bdfa','pdb_age':1,'constructor_rva':'0xf7530','definition_size':size,'temporary_known_bytes':sum(x is not None for x in temp),'fields':fields,'unknown_fields':unknown,'post_assignment_patches':patches,'limits':['Static constant-store recovery, not execution or full x86 emulation. Unknown bytes/types remain unknown.','The reported default applies before original source definition/deserialization/inheritance overrides.','Complex SoundCue/vector/color/enum types outside selected inventory are not silently normalized.']}
(REPORTS/'native-weapon-compiled-defaults.json').write_text(json.dumps(report,indent=2)+'\n')
print('Recovered',len(fields),'typed fields;',len(unknown),'unknown;',sum(x is not None for x in temp),'/',size,'temporary bytes proven')
for field in fields:
    if field['name']in['Control','DefaultControl','ReloadOnCreation','ReloadOnEquip','StartingAmmo','MaxAmmo','ClipSize','ChargeTime','Cooldown','CooldownMin','CooldownMax','ReloadTime','Effects']:print(field['name'],field['type'],repr(field['value']))
