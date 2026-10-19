#!/usr/bin/env python3
"""Decode bounded constant stores for the matched original EffectData ctor.

Read-only original DLL/PDB reports; this is not an x86 interpreter and does not
claim unknown pointer/padding bytes are zero. Source overrides apply afterward.
"""
import hashlib,json,re,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]; R=ROOT/'.local/reports'
TARGET='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'
CTOR=R/'native-weapon-effect-default-ctor.json'; COPY=R/'native-effect-def-assignment.json'
PDB=R/'native-engine-pdb-weapon-defaults-next.json'
CHANGE=R/'native-effect-change-type-pdb.json'
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def recover():
    dll=ROOT/'.local/game/base/x64/EngineWin64s.dll'; data=dll.read_bytes();assert hashlib.sha256(data).hexdigest()==TARGET
    ctor=json.loads(CTOR.read_text());copy=json.loads(COPY.read_text());pdb=json.loads(PDB.read_text())
    assert ctor['dll_sha256']==copy['dll_sha256']==TARGET
    assert pdb['identity']['guid']=='15f83bcc-e740-4d0b-bf67-6ac938c8bdfa' and pdb['identity']['age']==1
    change=json.loads(CHANGE.read_text());assert change['pdb_sha256']=='a75edd9c15ebbd4caa7a89ce2e3197cfb96cecb1e5a750f62a950caff48fcace'
    assert 'sgg::EffectDataDef::ChangeType' in change['stdout'] and 'name: RawString("ADD")' in change['stdout']
    assert ctor['procedure']['rva']==0xae2a0 and copy['procedure']['rva']==0xaed90
    by={t['index']:t for t in pdb['selected_types']}
    cls=next(t for t in by.values()if t.get('name')=='sgg::EffectDataDef'and'fields: Some'in t['debug'])
    size=int(re.search(r'size: (\d+)',cls['debug'])[1]);assert size==904
    index=int(re.search(r'fields: Some\(TypeIndex\((0x[\da-f]+)',cls['debug'])[1],16)
    fields=[{'name':m[3].removeprefix('m'),'member':m[3],'type_index':int(m[1],16),'offset':int(m[2])}for m in re.finditer(r'Member\(MemberType \{ attributes: FieldAttributes\(\d+\), field_type: TypeIndex\((0x[\da-f]+)\), offset: (\d+), name: RawString\("([^\"]+)"\)',by[index]['debug'])]
    pe=struct.unpack_from('<I',data,0x3c)[0];n,optional=struct.unpack_from('<H',data,pe+6)[0],struct.unpack_from('<H',data,pe+20)[0];sections=[]
    for i in range(n):
        vaoff=pe+24+optional+40*i;vsize,rva,rawsize,raw=struct.unpack_from('<IIII',data,vaoff+8);sections.append((rva,vsize,raw,rawsize))
    def readva(va,width):
        rva=va-0x180000000
        for start,_,raw,extent in sections:
            if start<=rva and rva+width<=start+extent:return data[raw+rva-start:raw+rva-start+width]
        raise ValueError('Constant outside original raw PE section')
    ops=[]
    for line in ctor['assembly'].splitlines():
        m=re.match(r'^([\da-f]+):.*?\t([a-z][a-z0-9]*)\s*(.*?)\s*(?:#.*)?$',line)
        if m:ops.append((int(m[1],16),m[2],m[3].split('#')[0].strip(),line))
    assert any(va==0x1800ae64b and op=='callq'and'0x1800aed90'in args for va,op,args,_ in ops)
    # RAX=entryRSP, pushRBP, RBP=entryRSP-0x2b8, subRSP0x3b0:
    # postRSP=entryRSP-0x3b8, hence RBP=postRSP+0x100. Temporary+0x20.
    assert any(op=='leaq'and args=='-0x2b8(%rax), %rbp'for _,op,args,_ in ops)
    assert any(op=='subq'and args=='$0x3b0, %rsp'for _,op,args,_ in ops)
    temp=[None]*size;proof=[None]*size;final=None;finalproof=None;regs={};addresses={};vectors={}
    aliases={'eax':'rax','ax':'rax','al':'rax','esi':'rsi','sil':'rsi','edx':'rdx','ecx':'rcx','edi':'rdi','dil':'rdi'}
    def reg(x):
        x=x.lstrip('%');return aliases.get(x,x[:-1]if re.fullmatch(r'r\d+[dwb]',x)else x)
    def put(dst,offset,width,value,where):
        memory,provenance=(temp,proof)if dst=='temp'else(final,finalproof)
        if not 0<=offset<=size-width:return
        raw=value if isinstance(value,bytes)else int(value%(1<<(width*8))).to_bytes(width,'little')if value is not None else[None]*width
        for i,b in enumerate(raw):memory[offset+i]=b;provenance[offset+i]=where if b is not None else None
    widths={'movq':8,'movl':4,'movw':2,'movb':1,'movdqa':16,'movdqu':16,'movaps':16,'movups':16}
    for va,op,args,line in ops:
        if va==0x1800ae64b:final=temp.copy();finalproof=proof.copy()
        items=[x.strip()for x in args.split(',')]
        if op.startswith('xor')and len(items)==2 and items[0]==items[1]and items[0].startswith('%'):regs[reg(items[0])]=0
        if op in widths or op in ('leaq','leal'):
            m=re.fullmatch(r'(.+), (%[a-z\d]+)',args)
            if m:
                src,dest=m.groups();key=reg(dest);value=int(src[1:],0)if src.startswith('$')else regs.get(reg(src))if src.startswith('%')else None
                if op in ('leaq','leal'):
                    a=re.fullmatch(r'(-?0x[\da-f]+)?\(%([a-z\d]+)\)',src)
                    if a and reg(a[2])in regs:value=regs[reg(a[2])]+int(a[1]or'0',0)
                if value is None:regs.pop(key,None)
                else:regs[key]=value
                addresses.pop(key,None)
                a=re.fullmatch(r'(-?0x[\da-f]+)?\(%(rsp|rbp)\)',src)
                if op=='leaq'and a:addresses[key]=int(a[1]or'0',0)+(0x100 if a[2]=='rbp'else 0)-0x20
                if dest.startswith('%xmm'):
                    match=re.search(r'# (0x[\da-f]+)',line)
                    if op=='movdqa'and match:vectors[key]=readva(int(match[1],16),16)
                    else:vectors.pop(key,None)
            m=re.fullmatch(r'(.+), (-?0x[\da-f]+)?\(%(rsp|rbp|rdi)\)',args)
            if m and op in widths:
                src,disp,base=m.groups();value=int(src[1:],0)if src.startswith('$')else vectors.get(reg(src),regs.get(reg(src)))if src.startswith('%')else None
                where={'va':hex(va),'instruction':line};offset=int(disp or'0',0)
                if final is None and base in ('rsp','rbp'):put('temp',offset+(0x100 if base=='rbp'else 0)-0x20,widths[op],value,where)
                elif final is not None and base=='rdi':put('final',offset-0x68,widths[op],value,where)
        if op=='callq':
            if final is None and'0x18049589a'in args and regs.get('rdx')==0 and addresses.get('rcx')is not None and regs.get('r8')is not None:put('temp',addresses['rcx'],regs['r8'],0,{'va':hex(va),'kind':'exact_zero_fill'})
            for key in ['rax','rcx','rdx','r8','r9','r10','r11']:regs.pop(key,None);addresses.pop(key,None)
            vectors.clear()
    assert final is not None
    primitive={0x30:('bool',1,'?'),0x40:('float32',4,'f'),0x74:('int32',4,'i'),0x75:('uint32',4,'I'),0x20:('uint8',1,'B')};known={};unknown=[]
    for field in fields:
        typ=field['type_index'];enum=None
        if typ==0xe5b7:enum='sgg::EffectDataDef::ChangeType';typ=0x74
        if by.get(typ,{}).get('debug','').startswith('Enumeration'):enum=by[typ]['name'];typ=int(re.search(r'underlying_type: TypeIndex\((0x[\da-f]+)',by[typ]['debug'])[1],16)
        kind,width,fmt=primitive.get(typ,('string',24,None)if typ==0x10ad else('unknown',0,None))
        projection=None
        if typ==0x1065:kind,width,projection='color_rgba8',4,'white_color'
        if typ==0x56a1:kind,width,projection='sound_cue',40,'empty_invalid_cue'
        off=field['offset'];raw=final[off:off+width]
        if projection=='empty_invalid_cue':
            selected=list(range(32))+[33]
            if raw[:32]!=[0]*31+[23] or raw[33]!=0:raise ValueError('SoundCue semantic bytes not proven empty/invalid')
            value={'owner':None,'name':'','valid':False}
        elif projection=='white_color':
            if raw!=[255]*4:raise ValueError('Color projection only proven for native opaque white')
            selected=list(range(4));value={'r':255,'g':255,'b':255,'a':255}
        else:selected=list(range(width))
        if not width or any(raw[i] is None for i in selected):unknown.append({**field,'reason':'Unproven bytes or complex native type'});continue
        if not projection:
            raw=bytes(raw)
            if fmt:value=struct.unpack('<'+fmt,raw)[0]
            elif raw[0]==0 and raw[23]==0x17:value=''
            else:unknown.append({**field,'reason':'String is not proven empty EASTL short string'});continue
        origins=[]
        for p in [finalproof[off+i] for i in selected]:
            if p not in origins:origins.append(p)
        known[field['name']]={'type':kind,'value':value,'offset':off,'full_data_offset':off+0x68,'enum':enum,'provenance':origins}
        if projection=='empty_invalid_cue':known[field['name']]['unproven_storage_byte_offsets']=[i for i in range(width) if raw[i] is None]
    return {'schema':'hades-effect-compiled-defaults-v1','target_sha256':TARGET,'pdb_guid':pdb['identity']['guid'],'pdb_age':pdb['identity']['age'],'fields':known,'unknown':unknown,'known_definition_bytes':sum(x is not None for x in final),'definition_bytes':size,'source_reports':[{'path':str(p.relative_to(ROOT)),'sha256':digest(p)}for p in [CTOR,COPY,PDB,CHANGE]],'limits':['Ctor constant stores only; actual source overrides/read/postread follow these defaults.','SoundCue semantic owner/name/valid projected; functor/padding remain unknown.','No original binary execution.']}
if __name__=='__main__':
    result=recover();output=R/'native-effect-compiled-defaults.json';output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n');print(output);print('known',len(result['fields']),'unknown',len(result['unknown']))
