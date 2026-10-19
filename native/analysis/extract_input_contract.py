#!/usr/bin/env python3
"""Decode exact original action switch entries, without executing the target."""
from pathlib import Path
import hashlib,json,struct
ROOT=Path(__file__).resolve().parents[2]
REPORTS=ROOT/'.local/reports'
DLL=ROOT/'.local/game/base/x64/EngineWin64s.dll'
binary=DLL.read_bytes();pe=struct.unpack_from('<I',binary,60)[0]
count=struct.unpack_from('<H',binary,pe+6)[0];optional=struct.unpack_from('<H',binary,pe+20)[0]
sections=[]
for i in range(count):
    section=pe+24+optional+i*40
    rva,size,offset=struct.unpack_from('<III',binary,section+12)
    sections.append((rva,size,offset))
def read(rva,size):
    for start,length,offset in sections:
        if start<=rva and rva+size<=start+length:return binary[offset+rva-start:offset+rva-start+size]
    raise ValueError(hex(rva))
actions=[]
for action,index_base,target_base,minus,report in [
    (60,0x254cc8,0x254a1c,0,'native-execute-groups.json'),
    (61,0x254cc8,0x254a1c,0,'native-execute-groups.json'),
    (229,0x2555b8,0x255548,81,'native-executebool-groups.json')]:
    selector=read(index_base+action-minus,1)[0]
    target=struct.unpack('<I',read(target_base+selector*4,4))[0]
    assembly=json.loads((REPORTS/report).read_text())['assembly']
    lines=assembly.splitlines();begin=next(i for i,line in enumerate(lines)if line.startswith(f'{0x180000000+target:x}:'))
    end=begin
    while end<len(lines):
        end+=1
        if 'jmpq' in lines[end-1]:break
    actions.append({'action_id':action,'selector_table_rva':hex(index_base),'selector_bias':minus,'switch_index':selector,'target_table_rva':hex(target_base),'case_rva':hex(target),'bounded_assembly':'\n'.join(lines[begin:end])})
result={'dll_sha256':hashlib.sha256(binary).hexdigest(),'matching_pdb_guid':'15f83bcc-e740-4d0b-bf67-6ac938c8bdfa','age':1,'actions':actions,'scope':'Observed static switch bytes and PDB-bounded LLVM disassembly; not runtime execution.'}
(REPORTS/'native-input-action-contract.json').write_text(json.dumps(result,indent=2)+'\n')
print('\n'.join(f"action {a['action_id']} -> {a['case_rva']}"for a in actions))
