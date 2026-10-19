#!/usr/bin/env python3
"""Address-bound scalar defaults needed by original obstacle polygon setup.

Uses retained PDB-bounded disassembly; never executes original game code.
"""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[2];REPORTS=ROOT/'.local/reports'
SHA='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'
assert hashlib.sha256((ROOT/'.local/game/base/x64/EngineWin64s.dll').read_bytes()).hexdigest()==SHA
ctor=json.loads((REPORTS/'native-collision-thing-data-defaults.json').read_text())
assignment=json.loads((REPORTS/'native-collision-thing-def-assignment.json').read_text())
body=json.loads((REPORTS/'native-collision-body-constructor2.json').read_text())
for d in [ctor,assignment,body]:assert d['dll_sha256']==SHA
for text in ['-0x2f0(%rsp), %rbp','$0x3f0, %rsp','0x20(%rsp), %rdx','0x8(%rbx), %rcx','0x1800ebe00']:
 assert text in ctor['assembly'],text
# Native frame setup makes RBP=post-prologueRSP+0x100. Temporary starts
# RSP+0x20, so a store at RBP+d covers definition offset d+0xe0.
# EAX cleared VA0x1800eb40b, then again after memset at0x1800eb5bd.
# RSI cleared VA0x1800eb410 and remains nonvolatile. Assignment copies each
# selected field at its matching PDB offset before listed direct overrides.
def check(d,va,fragment):
 line=next(s for s in d['assembly'].splitlines()if s.startswith(va+':'))
 assert fragment in line,(va,fragment,line)
 return line
check(ctor,'1800eb40b','%eax, %eax');check(ctor,'1800eb410','%esi, %esi')
entries=[
 ('Scale',1.0,'float32',0x194,'1800eb73d','$0x3f800000, 0x19c(%rbx)',None,None),
 ('MinScale',0.0,'float32',0x198,'1800eb52c','%rax, 0xb4(%rbp)','1800ec240','1800ec246'),
 ('MaxScale',0.0,'float32',0x19c,'1800eb533','%rax, 0xbc(%rbp)','1800ec24c','1800ec252'),
 ('StartAngle',0.0,'float32',0x1a0,'1800eb533','%rax, 0xbc(%rbp)','1800ec258','1800ec262'),
 ('RelativeExtentScale',0.0,'float32',0x174,'1800eb510','%rax, 0x94(%rbp)','1800ec1d4','1800ec1da'),
 ('ExtentScale',0.0,'float32',0x178,'1800eb510','%rax, 0x94(%rbp)','1800ec1e0','1800ec1e6'),
 ('RotateGeometry',False,'bool',0x20c,'1800eb5a1','%rax, 0x128(%rbp)','1800ec3ab','1800ec3b2'),
 ('MirrorGeometry',False,'bool',0x20d,'1800eb5a1','%rax, 0x128(%rbp)','1800ec3b8','1800ec3bf'),
 ('StopsUnits',True,'bool',0x210,'1800eb80e','$0x1010000, 0x216(%rbx)',None,None),
]
fields={}
for name,value,typ,off,va,fragment,read,write in entries:
 proof={'kind':'compiled_native_default','store_va':'0x'+va,'instruction':check(ctor,va,fragment),'definition_offset':hex(off),'full_data_offset':hex(off+8)}
 if read:
  proof['temporary_stack_base']='post-prologueRSP+0x20;RBP=RSP+0x100'
  proof['assignment_call_va']='0x1800eb719'
  proof['assignment_read']=check(assignment,read,hex(off)+'(%rdx)')
  proof['assignment_write']=check(assignment,write,hex(off)+'(%rcx)')
 else:proof['post_assignment_override']=True
 fields[name]={'type':typ,'value':value,'provenance':proof}
body_fields={}
for name,value,va,fragment in [
 ('Scale',1.0,'1802b5bbe','$0x3f800000, 0x48(%rcx)'),
 ('SquashStretchScale',1.0,'1802b5bc5','$0x3f800000, 0x4c(%rcx)'),
 ('SquashStretchAngle',0.0,'1802b5bc5','$0x3f800000, 0x4c(%rcx)'),
 ('Angle',0.0,'1802b5bcd','%eax, 0x54(%rcx)'),
 ('Flips',0,'1802b5be9','%ax, 0x68(%rcx)'),
 ('ScaleTransitionActive',False,'1802b5ca8','%eax, 0x120(%rcx)')]:
 body_fields[name]={'value':value,'store_va':'0x'+va,'instruction':check(body,va,fragment)}
check(body,'1802b5bb0','%eax, %eax')
result={'schema':'hades-collision-defaults-v1','target_sha256':SHA,'pdb_guid':'15f83bcc-e740-4d0b-bf67-6ac938c8bdfa','pdb_age':1,'thing_fields':fields,'body_fields':body_fields,'reports':['native-collision-thing-data-defaults.json','native-collision-thing-def-assignment.json','native-collision-body-constructor2.json'],'notes':['Exact fresh-body scalar defaults only; later runtime adjustments/transitions are outside static export.','QWORD3f800000 at Body+0x4c stores squashScale1 and squashAngle0. WORD0 at Body+0x68 stores meshType0 and flips0.','DWORD01010000 at ThingData+0x216 stores CreatesShadows0,StopsLight0,StopsUnits1,StopsProjectiles1.']}
(REPORTS/'native-collision-compiled-defaults.json').write_text(json.dumps(result,indent=2)+'\n')
print('Recovered',len(fields),'ThingData and',len(body_fields),'Body defaults')
