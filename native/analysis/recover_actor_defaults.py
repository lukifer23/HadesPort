#!/usr/bin/env python3
"""Read proven constructor constants for the exact original actor definitions.

This decodes bounded constant stores, not target execution or an x86 emulator.
Unknown stack bytes, pointers and complex types remain explicit.
"""
from pathlib import Path
import argparse,hashlib,json,re,struct
ROOT=Path(__file__).resolve().parents[2];REPORTS=ROOT/'.local/reports'
TARGET='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'
assert hashlib.file_digest((ROOT/'.local/game/base/x64/EngineWin64s.dll').open('rb'),'sha256').hexdigest()==TARGET
pdb=json.loads((REPORTS/'native-engine-pdb-actor-next-2.json').read_text());by={t['index']:t for t in pdb['selected_types']}

def instructions(assembly):
    for line in assembly.splitlines():
        m=re.match(r'^([\da-f]+):.*?\t([a-z][a-z0-9]*)\s*(.*?)\s*(?:#.*)?$',line)
        if m:yield int(m[1],16),m[2],m[3].split('#')[0].strip(),line

def register(name):
    aliases={'eax':'rax','ax':'rax','al':'rax','ebx':'rbx','bl':'rbx','ecx':'rcx','cl':'rcx','edx':'rdx','dl':'rdx','esi':'rsi','sil':'rsi','edi':'rdi','dil':'rdi'}
    n=name.lstrip('%');n=aliases.get(n,n)
    if re.fullmatch(r'r\d+[dwb]',n):n=n[:-1]
    return n

def recover(category,ctor_file,copy_file,assignment,base):
    cls=next(t for t in by.values()if t.get('name')=='sgg::'+category+'DataDef'and'fields: Some'in t['debug'])
    field_index=int(re.search(r'fields: Some\(TypeIndex\((0x[\da-f]+)',cls['debug'])[1],16);size=int(re.search(r'size: (\d+)',cls['debug'])[1])
    members=[{'name':m[3].removeprefix('m'),'member':m[3],'type_index':int(m[1],16),'offset':int(m[2])}for m in re.finditer(r'Member\(MemberType \{ attributes: FieldAttributes\(\d+\), field_type: TypeIndex\((0x[\da-f]+)\), offset: (\d+), name: RawString\("([^\"]+)"\)',by[field_index]['debug'])]
    ctor=json.loads((REPORTS/ctor_file).read_text());copy=json.loads((REPORTS/copy_file).read_text());assert ctor['dll_sha256']==copy['dll_sha256']==TARGET
    ops=list(instructions(ctor['assembly']));assert any(a==assignment and op=='callq'for a,op,_,_ in ops)
    # All three exact prologues establish RBP=post-prologueRSP+0x100 and pass
    # temporary[RSP+0x20] as RDX to the matching definition assignment.
    lea=next(args for _,op,args,_ in ops if op=='leaq'and args.endswith('(%rsp), %rbp'))
    frame=next(int(args.split(',')[0][1:],0)for _,op,args,_ in ops if op=='subq'and args.endswith(', %rsp'))
    assert int(re.search(r'(-?0x[\da-f]+)',lea)[1],0)+frame==0x100
    assert any(a<assignment and op=='leaq'and args=='0x20(%rsp), %rdx'for a,op,args,_ in ops)
    # The constructor's live `this` register (rbx for Thing/Unit/Life; rdi for
    # ObstacleData::ObstacleData VA1800d414f). Post-assignment constant stores
    # through it (e.g. NeedsLife=1 at VA1800d4314) override the copied def.
    this=next(register(args.split(',')[1].strip())for _,op,args,_ in ops if op=='movq'and args.startswith('%rcx, %r'))
    temp=[None]*size;provenance=[None]*size;direct=[];constants={};stack_addresses={}
    def put(offset,width,value,proof):
        if offset<0 or offset+width>size:return
        data=(value%(1<<(width*8))).to_bytes(width,'little')if value is not None else[None]*width
        for i,v in enumerate(data):temp[offset+i]=v;provenance[offset+i]=proof if value is not None else None
    widths={'movq':8,'movl':4,'movw':2,'movb':1,'movdqu':16,'movaps':16,'movups':16}
    for va,op,args,line in ops:
        if va==assignment:direct=temp.copy();direct_prov=provenance.copy();continue
        if op.startswith('xor')and len(args.split(','))==2:
            a,b=[s.strip()for s in args.split(',')]
            if a==b and a.startswith('%'):constants[register(a)]=0
        if op in widths or op in ('leaq','leal'):
            m=re.fullmatch(r'(.+), (%[a-z\d]+)',args)
            if m:
                src,dest=m.groups();key=register(dest)
                known=int(src[1:],0)if src.startswith('$')else constants.get(register(src))if src.startswith('%')else None
                computed=re.fullmatch(r'(-?0x[\da-f]+)?\(%([a-z\d]+)\)',src)
                if op in ('leaq','leal')and computed and register(computed[2])in constants:
                    known=constants[register(computed[2])]+int(computed[1]or'0',0)
                if known is None:constants.pop(key,None)
                else:constants[key]=known
                stack_addresses.pop(key,None)
                address=re.fullmatch(r'(-?0x[\da-f]+)?\(%(rsp|rbp)\)',src)
                if op=='leaq'and address:stack_addresses[key]=int(address[1]or'0',0)+(0x100 if address[2]=='rbp'else 0)-0x20
        m=re.fullmatch(r'(.+), (-?0x[\da-f]+)?\(%(rsp|rbp|rbx|rdi)\)',args)
        if m and op in widths:
            source,disp,dest=m.groups();value=int(source[1:],0)if source.startswith('$')else constants.get(register(source))if source.startswith('%')else None
            offset=int(disp or'0',0)+(0x100 if dest=='rbp'else 0)-0x20
            proof={'va':hex(va),'instruction':line}
            if va<assignment and dest in ('rsp','rbp'):put(offset,widths[op],value,proof)
            elif va>assignment and dest==this:
                # Apply only constant stores after the completed native move.
                old,old_prov=temp,provenance;temp,provenance=direct,direct_prov;put(int(disp or'0',0)-base,widths[op],value,proof);direct,direct_prov=temp,provenance;temp,provenance=old,old_prov
        if op=='callq':
            if va<assignment and'0x18049589a'in args:
                offset=stack_addresses.get('rcx');length=constants.get('r8');zero=constants.get('rdx')
                if offset is not None and length is not None and zero==0:put(offset,length,0,{'va':hex(va),'kind':'verified_zero_fill','offset':offset,'bytes':length})
            for n in ('rax','rcx','rdx','r8','r9','r10','r11','xmm0','xmm1','xmm2','xmm3','xmm4','xmm5'):
                constants.pop(n,None);stack_addresses.pop(n,None)
    fields={};unknown=[];primitives={0x30:('bool',1,'?'),0x40:('float32',4,'f'),0x74:('int32',4,'i'),0x75:('uint32',4,'I'),0x20:('uint8',1,'B'),0x70:('char',1,'b')}
    for item in members:
        off=item['offset'];typ=item['type_index'];enum=None
        if typ in by and by[typ]['debug'].startswith('Enumeration'):
            enum=by[typ].get('name');typ=int(re.search(r'underlying_type: TypeIndex\((0x[\da-f]+)',by[typ]['debug'])[1],16)
        primitive=primitives.get(typ)
        if primitive:kind,width,fmt=primitive
        elif typ==0x10ad:kind,width,fmt='string',24,None
        elif by.get(typ,{}).get('debug','').startswith('Pointer'):kind,width,fmt='null_pointer',8,None
        elif item['name']=='Color':kind,width,fmt='packed_color_rgba',4,None
        elif category=='Thing' and item['name']=='Offset' and typ==0x1035:kind,width,fmt='Point2',8,None
        elif category=='Thing' and (item['name'],typ) in [('Interact',0xe3aa),('Metering',0xe3ac)]:kind,width,fmt='null_pointer',8,None
        else:unknown.append({**item,'reason':'Complex type outside proven scalar scope'});continue
        chunk=direct[off:off+width]
        if len(chunk)!=width or any(v is None for v in chunk):unknown.append({**item,'reason':'Some native bytes remain unproven'});continue
        raw=bytes(chunk)
        if fmt:value=struct.unpack('<'+fmt,raw)[0]
        elif kind=='Point2':
            xy=struct.unpack('<2f',raw);value={'X':xy[0],'Y':xy[1]}
        elif kind=='string':
            if raw[0]!=0 or raw[23]!=0x17:unknown.append({**item,'reason':'Not a proven empty EASTL short string'});continue
            value=''
        elif kind=='null_pointer':
            if raw!=bytes(width):unknown.append({**item,'reason':'Non-null native pointer'});continue
            value=None
        else:value={'Red':raw[0]/255,'Green':raw[1]/255,'Blue':raw[2]/255,'Alpha':raw[3]/255}
        proof=[]
        for prov in direct_prov[off:off+width]:
            if prov not in proof:proof.append(prov)
        fields[item['name']]={'type':kind,'value':value,'native_member':item['member'],'offset':off,'full_data_offset':off+base,'enum':enum,'provenance':proof}
    return {'fields':fields,'unknown':unknown,'size':size,'constructor':ctor_file,'assignment':copy_file,'copy_call_va':hex(assignment),'known_definition_bytes':sum(v is not None for v in direct),'limits':['Constant-store evidence only; unsupported complex values remain absent. Source inheritance/overrides apply after these compiled defaults.','Frame/constant transfers are specific to the matched DLL and are not a general machine-code evaluator.']}

result={'schema':'hades-actor-compiled-defaults-v1','target_sha256':TARGET,'pdb_guid':pdb['identity']['guid'],'pdb_age':pdb['identity']['age'],'definitions':{
    'Thing':recover('Thing','native-collision-thing-data-defaults.json','native-collision-thing-def-assignment.json',0x1800eb719,8),
    'Unit':recover('Unit','native-active-unit-ctor-static.json','native-active-unit-def-assignment.json',0x1800f1631,0x50),
    'Obstacle':recover('Obstacle','native-active-obstacle-ctor-static.json','native-active-obstacle-def-assignment.json',0x1800d42bb,0x50),
    'Life':recover('Life','native-active-life-ctor-static.json','native-active-life-def-assignment.json',0x1800c86b6,0)}}
parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,default=REPORTS/'native-actor-compiled-defaults.json');args=parser.parse_args()
path=args.output;path.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
for name,value in result['definitions'].items():print(name,'known fields',len(value['fields']),'unknown',len(value['unknown']),'bytes',value['known_definition_bytes'],'/',value['size'])
