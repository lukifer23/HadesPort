#!/usr/bin/env python3
"""Extend retained Thing defaults with source-bound Point2/default color bytes.

Reuses the existing bounded constant-store decoder without executing its report
writer. No game code or machine instructions are executed.
"""
import ast,hashlib,json,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
source=ROOT/'native/analysis/recover_actor_defaults.py'
tree=ast.parse(source.read_text())
# Keep imports, definitions and the exact decoder setup. Omit its output writer.
selected=[]
for node in tree.body:
    if isinstance(node,(ast.Import,ast.ImportFrom,ast.FunctionDef)):selected.append(node)
    elif isinstance(node,ast.Assign) and all(not isinstance(t,ast.Name) or t.id!='result' for t in node.targets):
        # Only assignments before the result object belong to decoder setup.
        if node.lineno < next(n.lineno for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name)and t.id=='result'for t in n.targets)):selected.append(node)
    elif isinstance(node,ast.Assert):selected.append(node)
code=ast.unparse(ast.Module(body=selected,type_ignores=[]))
code=code.replace("else:\n            unknown.append({**item, 'reason': 'Complex type outside proven scalar scope'})", "elif typ == 0x1035:\n            kind,width,fmt='point2',8,None\n        elif item['name'] in ('OutlineColor','SubtitleColor'):\n            kind,width,fmt='packed_color_rgba',4,None\n        else:\n            unknown.append({**item, 'reason': 'Complex type outside proven scalar scope'})")
code=code.replace("elif kind == 'string':", "elif kind == 'point2':\n            value=dict(zip(('X','Y'),struct.unpack('<ff',raw)))\n        elif kind == 'string':")
code=code.replace("'known_definition_bytes': sum((v is not None for v in direct))","'constant_bytes': direct, 'constant_provenance': direct_prov, 'known_definition_bytes': sum((v is not None for v in direct))")
ns={'__file__':str(source)};exec(compile(code,str(source),'exec'),ns)
result=ns['recover']('Thing','native-collision-thing-data-defaults.json','native-collision-thing-def-assignment.json',0x1800eb719,8)
raw=result.pop('constant_bytes');proofs=result.pop('constant_provenance')

for item in result['unknown'].copy():
    off=item['offset'];name=item['name'];proof=[]
    if name in ('Interact','Metering') and raw[off:off+8]==[0]*8:
        typ='null_pointer';value=None;indices=range(off,off+8)
    elif name in ('Points','OccluderPoints') and raw[off:off+24]==[0]*24:
        typ='point_array';value=[];indices=range(off,off+24)
    elif name in ('AmbientSound','ImpulsingSound','FootstepSound','RotationSound','TouchdownSound'):
        required=list(range(off,off+8))+[off+8,off+31,off+33]
        if any(raw[i]is None for i in required):continue
        if raw[off:off+8]!=[0]*8 or raw[off+8]!=0 or raw[off+31]!=0x17 or raw[off+33]!=0:continue
        typ='sound_cue';value={'owner':None,'name':'','valid':False,'storage_padding_unknown':True};indices=required
    else:continue
    for i in indices:
        if proofs[i]not in proof:proof.append(proofs[i])
    result['fields'][name]={'type':typ,'value':value,'native_member':item['member'],'offset':off,'full_data_offset':off+8,'provenance':proof}
    result['unknown'].remove(item)
# The full ThingData constructor explicitly resets both point-vector ends
# to their begins AFTER assignment, and invokes UsingInfo vector::clear.
# These are semantic empty vectors even when the bounded scalar decoder does
# not retain alias-copied pointer bytes. Do not promote unknown bytes to zero.
ctor=json.loads((ROOT/'.local/reports/native-collision-thing-data-defaults.json').read_text())
assert ctor['dll_sha256']==ns['TARGET']
for name,begin,end,site in [('Points','0x378','0x380','0x1800eb830..0x1800eb837'),('OccluderPoints','0x390','0x398','0x1800eb83e..0x1800eb845')]:
    assert f'movq\t{begin}(%rbx), %rax'in ctor['assembly'] and f'movq\t%rax, {end}(%rbx)'in ctor['assembly']
    item=next(x for x in result['unknown']if x['name']==name)
    result['fields'][name]={'type':'point_array','value':[],'native_member':item['member'],'offset':item['offset'],'full_data_offset':item['offset']+8,'provenance':[{'kind':'native_vector_end_reset_to_begin','report':'.local/reports/native-collision-thing-data-defaults.json','instruction_range':site}]};result['unknown'].remove(item)
assert 'leaq\t0x3b8(%rbx), %rcx'in ctor['assembly'] and 'callq\t0x180078aa0'in ctor['assembly']
item=next(x for x in result['unknown']if x['name']=='Using')
result['fields']['Using']={'type':'using_array','value':[],'native_member':item['member'],'offset':item['offset'],'full_data_offset':item['offset']+8,'provenance':[{'kind':'native_UsingInfo_vector_clear','report':'.local/reports/native-collision-thing-data-defaults.json','receiver_va':'0x1800eb7d5','call_va':'0x1800eb85d','callee':'eastl::vector<sgg::UsingInfo,eastl::allocator_forge>::clear'}]};result['unknown'].remove(item)
assert all(k in result['fields']for k in ['Size','Offset','OutlineColor','SubtitleColor'])
result.update(schema='hades-projectile-thing-defaults-v1',target_sha256=ns['TARGET'],base_report='.local/reports/native-actor-compiled-defaults.json',decoder_sha256=hashlib.sha256(source.read_bytes()).hexdigest())
path=ROOT/'.local/reports/native-projectile-thing-compiled-defaults.json';path.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
print(json.dumps({'report':str(path),'fields':len(result['fields']),'added':{k:result['fields'][k]for k in ['Size','Offset','OutlineColor','SubtitleColor']},'unknown':result['unknown']},indent=2))
