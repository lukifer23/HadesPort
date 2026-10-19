#!/usr/bin/env python3
"""Bounded static semantic SoundCue recovery for matching ProjectileData ctor."""
from pathlib import Path
import json,re,hashlib
ROOT=Path(__file__).resolve().parents[2];R=ROOT/'.local/reports'
TARGET='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(ROOT/'.local/game/base/x64/EngineWin64s.dll')==TARGET
ctor=R/'native-player-projectile-default-ctor.json';p=json.loads(ctor.read_text());assert p['dll_sha256']==TARGET
values=[None]*0x800;origins=[None]*len(values);zero={'%rax':None,'%r14':None,'%xmm0':None};canonical={'%al':'%rax','%eax':'%rax','%r14b':'%r14','%r14d':'%r14'}
for line in p['assembly'].splitlines():
 m=re.match(r'^([\da-f]+):.*?\t([a-z][a-z0-9]*)\s*(.*?)\s*(?:#.*)?$',line)
 if not m:continue
 address,op,operands=int(m[1],16),m[2],m[3].split('#')[0].strip()
 if not 0x1800d848c<=address<0x1800d8a6e:continue
 parts=operands.split(',')
 if len(parts)==2:
  src,dst=[x.strip()for x in parts];reg=canonical.get(dst,dst)
  if reg in zero:zero[reg]=address if op in ('xorl','xorps')and src==dst else None
 store=re.match(r'(.+), (-?0x[\da-f]+)?\(%(rsp|rbp)\)$',operands)
 if store and op in ('movq','movl','movw','movb','movdqu'):
  operand,disp,base=store.groups();width={'movq':8,'movl':4,'movw':2,'movb':1,'movdqu':16}[op];offset=int(disp or'0',0)+(0x100 if base=='rbp'else 0)-0x20;number=None;provenance={'store_va':hex(address),'instruction':line}
  if operand.startswith('$'):number=int(operand[1:],0)
  elif zero.get(canonical.get(operand,operand))is not None:number=0;provenance['constant_definition_va']=hex(zero[canonical.get(operand,operand)])
  if number is not None and 0<=offset<=len(values)-width:values[offset:offset+width]=(number%(1<<(8*width))).to_bytes(width,'little');origins[offset:offset+width]=[provenance]*width
 if op=='callq':zero['%rax']=zero['%xmm0']=None
fields=[]
for name,offset in [('ImpactSound',0x178),('InvincibleImpactSound',0x1a0),('InflictedDamageSound',0x1c8),('DetonateSound',0x260)]:
 assert values[offset:offset+8]==[0]*8,(name,'owner')
 assert values[offset+8:offset+32]==[0]*23+[23],(name,'name')
 assert values[offset+33]==0,(name,'valid')
 stores={json.dumps(origins[i],sort_keys=True)for i in list(range(offset,offset+32))+[offset+33]}
 fields.append({'name':name,'offset':offset,'type':'sound_cue','value':{'owner':None,'name':'','valid':False},'unproven_storage_byte_offsets':[i for i in range(40)if values[offset+i]is None],'provenance':{'kind':'compiled_native_default','constructor_rva':'0xd8430','assignment_call_va':'0x1800d8a6e','temporary_base':'post-prologueRSP+20;RBP=RSP+100','stores':[json.loads(s)for s in sorted(stores)]}})
report={'schema':'hades-projectile-soundcue-compiled-defaults-v1','dll_sha256':TARGET,'pdb_identity':p['pdb_identity'],'source_reports':{str(ctor.relative_to(ROOT)):sha(ctor),'native-player-projectile-compiled-defaults-v2.json':sha(R/'native-player-projectile-compiled-defaults-v2.json')},'fields':fields,'limits':['Source SoundCue.Read overrides remain separate; invalid omitted cue is not evidence that named cue cannot play.','Seven per-cue padding/stateless functor bytes remain unknown, no40-byte memset claimed.','Matching PDB-bounded LLVM constant-store proof, no target execution.']}
out=R/'native-projectile-soundcue-compiled-defaults.json';out.write_text(json.dumps(report,indent=2)+'\n');print(out)
