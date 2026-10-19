#!/usr/bin/env python3
"""Bounded matched-PDB/LLVM constant-store recovery; never execute game code."""
import hashlib,json,re,struct
from pathlib import Path
R=Path(__file__).resolve().parents[2]/'.local/reports'
TARGET='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'
def recover():
 ctor=json.loads((R/'native-interact-data-ctor-static-v1.json').read_text());copy=json.loads((R/'native-interact-data-assignment-static-v1.json').read_text());layout=(R/'native-interaction-field-layout-v1.txt').read_text()
 assert ctor['dll_sha256']==copy['dll_sha256']==TARGET
 line=next(l for l in layout.splitlines()if l.startswith('0xdae8 '));fields=[(int(t,16),int(o),n[1:])for t,o,n in re.findall(r'field_type: TypeIndex\((0x[0-9a-f]+)\), offset: (\d+), name: RawString\("([^"\n]+)"\)',line)]
 ops=[]
 for line in ctor['assembly'].splitlines():
  m=re.match(r'^([0-9a-f]+):.*?\t([a-z][a-z0-9]*)\s*(.*?)\s*(?:#.*)?$',line)
  if m:ops.append((int(m[1],16),m[2],m[3].split('#')[0].strip(),line))
 assert any(op=='leaq'and args=='-0x210(%rsp), %rbp'for _,op,args,_ in ops)
 assert any(op=='subq'and args=='$0x310, %rsp'for _,op,args,_ in ops)
 size=752;temp=[None]*size;proof=[None]*size;final=None;origins=None;regs={}
 aliases={'eax':'rax','ax':'rax','al':'rax','ecx':'rcx','cl':'rcx','edx':'rdx','dl':'rdx'}
 def reg(n):return aliases.get(n.lstrip('%'),n.lstrip('%'))
 def put(mem,provenance,offset,width,value,where):
  if not 0<=offset<=size-width:return
  data=[None]*width if value is None else int(value%(1<<(8*width))).to_bytes(width,'little')
  for i,b in enumerate(data):mem[offset+i]=b;provenance[offset+i]=where if b is not None else None
 widths={'movq':8,'movl':4,'movw':2,'movb':1,'movdqu':16,'movups':16,'movaps':16}
 for va,op,args,line in ops:
  if va==0x1800c4ae1:final=temp.copy();origins=proof.copy()
  parts=[v.strip()for v in args.split(',')]
  if op.startswith('xor')and len(parts)==2 and parts[0]==parts[1]and parts[0].startswith('%'):regs[reg(parts[0])]=0
  if op in widths:
   m=re.fullmatch(r'(.+), (%[a-z0-9]+)',args)
   if m:
    a,b=m.groups();value=int(a[1:],0)if a.startswith('$')else regs.get(reg(a))if a.startswith('%')else None
    if value is None:regs.pop(reg(b),None)
    else:regs[reg(b)]=value
   m=re.fullmatch(r'(.+), (-?0x[0-9a-f]+)?\(%(rsp|rbp|rbx)\)',args)
   if m:
    src,disp,base=m.groups();value=int(src[1:],0)if src.startswith('$')else regs.get(reg(src))if src.startswith('%')else None
    off=int(disp or'0',0);where={'va':hex(va),'instruction':line}
    if final is None and base in ['rsp','rbp']:put(temp,proof,off+(0x100 if base=='rbp'else 0)-0x20,widths[op],value,where)
    elif final is not None and base=='rbx':put(final,origins,off,widths[op],value,where)
  if op=='callq':
   for r in ['rax','rcx','rdx','r8','r9','r10','r11']:regs.pop(r,None)
 assert final is not None
 primitive={0x30:('bool',1,'?'),0x40:('float32',4,'f'),0x74:('int32',4,'i'),0x75:('uint32',4,'I'),0x20:('uint8',1,'B')};known={};unknown=[]
 for typ,off,name in fields:
  if typ in primitive:kind,width,fmt=primitive[typ];selected=list(range(width))
  elif typ==0x10ad:kind,width,fmt='string',24,None;selected=[0,23]
  elif typ==0x56a1:kind,width,fmt='sound_cue',40,None;selected=list(range(32))+[33]
  elif name=='SpawnType':kind,width,fmt='native_spawn_type',1,'B';selected=[0]
  else:unknown.append({'name':name,'offset':off,'type_index':hex(typ),'reason':'Unrecovered complex native type'});continue
  raw=final[off:off+width]
  if any(raw[i]is None for i in selected):unknown.append({'name':name,'offset':off,'type_index':hex(typ),'reason':'Semantic bytes not completely proven'});continue
  if kind=='string':
   if raw[0]!=0 or raw[23]!=23:unknown.append({'name':name,'reason':'Not a proven empty short string'});continue
   value=''
  elif kind=='sound_cue':
   if raw[:32]!=[0]*31+[23]or raw[33]!=0:unknown.append({'name':name,'reason':'Not a proven empty invalid SoundCue'});continue
   value={'owner':None,'name':'','valid':False,'storage_padding_unknown':True}
  else:value=struct.unpack('<'+fmt,bytes(raw))[0]
  evidence=[]
  for i in selected:
   if origins[off+i]not in evidence:evidence.append(origins[off+i])
  known[name]={'type':kind,'value':value,'native_offset':off,'provenance':evidence}
 # Constructor post-assignment assigns this exact original PE string.
 annotation=next(a for a in ctor['annotations']if a['instruction'].startswith('1800c4b43:'));assert any(r.get('string')=='UnuseableInstructions'for r in annotation['references'])
 known['UnuseableInstructions']={'type':'string','value':'UnuseableInstructions','native_offset':0x2d0,'provenance':[{'va':'0x1800c4b43','instruction':'Original PE literal load followed by EASTL assign at0x1800c4b4a'}]}
 unknown=[v for v in unknown if v['name']!='UnuseableInstructions']
 known['SpawnType']['enum']='sgg::GameDataType';known['SpawnType']['enum_name']='Unknown'
 return {'schema':'hades-interact-compiled-defaults-v1','target_sha256':TARGET,'pdb_identity':ctor['pdb_identity'],'fields':known,'unknown':unknown,'source_reports':[{'path':str((R/p).relative_to(R.parents[1])),'sha256':hashlib.sha256((R/p).read_bytes()).hexdigest()}for p in ['native-interact-data-ctor-static-v1.json','native-interact-data-assignment-static-v1.json','native-interaction-field-layout-v1.txt','native-interact-spawn-type-enum-v1.txt']],'limits':['Source overrides and actual Read/PostRead are separate from compiled defaults.','SoundCue padding/hash-functor bytes remain unknown.','No target execution.']}
if __name__=='__main__':
 p=recover();path=R/'native-interact-compiled-defaults-v1.json';path.write_text(json.dumps(p,indent=2)+'\n');print(len(p['fields']),'known fields;',p['unknown']);print(path)
