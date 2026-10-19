#!/usr/bin/env python3
"""Extract the observed Lua API and return kinds from matching PDB enums + native InitLua switch tables."""
from pathlib import Path
import re,struct,json,hashlib,uuid,collections
ROOT=Path(__file__).resolve().parents[2]
b=(ROOT/'.local/game/base/x64/EngineWin64s.dll').read_bytes()
assert hashlib.sha256(b).hexdigest()=='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'
pdb=json.loads((ROOT/'.local/reports/native-engine-pdb.json').read_text())
pe=struct.unpack_from('<I',b,60)[0];ns=struct.unpack_from('<H',b,pe+6)[0];op=struct.unpack_from('<H',b,pe+20)[0];base=struct.unpack_from('<Q',b,pe+48)[0];sections=[]
for i in range(ns):
 o=pe+24+op+i*40;_,rv,rs,fo=struct.unpack_from('<IIII',b,o+8);sections.append((rv,rs,fo))
def at(rv,n):
 for r,s,f in sections:
  if r<=rv and rv+n<=r+s:return b[f+rv-r:f+rv-r+n]
 raise ValueError(hex(rv))
debugrva,debugsize=struct.unpack_from('<II',b,pe+24+112+6*8)
identity=None
for i in range(0,debugsize,28):
 d=at(debugrva+i,28);typ,size,addr,off=struct.unpack_from('<IIII',d,12)
 if typ==2 and b[off:off+4]==b'RSDS':identity={'guid':str(uuid.UUID(bytes_le=b[off+4:off+20])),'age':struct.unpack_from('<I',b,off+20)[0]}
assert identity=={k:pdb['identity'][k] for k in ['guid','age']}
def enum(index):
 d=next(t['debug'] for t in pdb['selected_types'] if t['index']==index)
 return [(int(n),s) for n,s in re.findall(r'value: (?:U\d+|I\d+)\((-?\d+)\), name: RawString\("([^"]+)"\)',d)]
returns=dict(enum(62762));assert returns=={0:'Void',1:'Bool',2:'String',3:'Object',4:'Table',5:'Float'}
targets={0x285648:1,0x28564f:2,0x285656:3,0x28565d:4,0x285664:5,0x28566b:0}
table1=struct.unpack('<4I',at(0x286548,16));indices1=at(0x286558,235)
table2=struct.unpack('<5I',at(0x286644,20));indices2=at(0x286658,51)
def ret(i):
 if i==264:return 5
 if 9<=i<=243:return targets[table1[indices1[i-9]]]
 if 265<=i<=315:return targets[table2[indices2[i-265]]]
 return 0
procedures={s['name']:s for s in pdb['module_symbols'] if s['kind']=='procedure'}
actions=[]
for i,name in enum(54260):
 if name=='COUNT':continue
 native=procedures.get('sgg::ScriptAction::'+name)
 actions.append({'id':i,'name':name,'lua_name':'LoadMapInternal' if i==148 else name,'return_kind':returns[ret(i)],'native_handler_rva':hex(native['rva']) if native else None,'native_dispatch':'sgg::ScriptAction::Execute'+{'Void':'','Bool':'Bool','String':'String','Object':'Object','Table':'Table','Float':'Float'}[returns[ret(i)]]})
triggers=[{'id':i,'name':n} for i,n in enum(54778) if n!='COUNT']
assert len(actions)==319 and len(triggers)==71
report={'identity':identity,'dll_sha256':hashlib.sha256(b).hexdigest(),'pdb_sha256':hashlib.file_digest((ROOT/'.local/game/base/x64/EngineWin64s.pdb').open('rb'),'sha256').hexdigest(),'lua_version_observed':'5.2.2','registration_function_rva':'0x285190','registration_evidence':'Native InitLua resizes 71 trigger closures and 319 action closures, classifies return kind using decoded switch tables at RVAs 0x286548/0x286558 and 0x286644/0x286658, calls TriggerToString/ActionToString, and writes Lua globals. Action 148 is renamed LoadMapInternal at instruction RVA 0x2858c2. Matching PDB supplies enum identities.','actions':actions,'triggers':triggers,'return_kind_counts':dict(collections.Counter(a['return_kind'] for a in actions)),'extra_globals':['random','randomint','randomseed','randomgaussian','newFunctionCall','verboseLogging'],'boot_scripts':['Main.lua','RoomManager.lua'],'cached_lua_callbacks':['update','print','dispatch'],'limits':['API inventory is exact for this base engine build, not a recovery of all function semantics.','Lua Object return category is native eastl::any and requires further value-conversion recovery.','Unknown startup imports and Using preprocessing are unresolved until focused Load decompilation completes.']}
(ROOT/'native/analysis/lua-bridge-contract.json').write_text(json.dumps(report,indent=2)+'\n')
header=['#pragma once','#include <cstddef>','namespace hades::contract {','enum class ReturnKind { Void, Bool, String, Object, Table, Float };','struct Action { int id; const char* name; ReturnKind result; };','inline constexpr Action actions[] = {']
header += [f'    {{{a["id"]}, "{a["lua_name"]}", ReturnKind::{a["return_kind"]}}},' for a in actions]
header += ['};','struct Trigger { int id; const char* name; };','inline constexpr Trigger triggers[] = {']
header += [f'    {{{t["id"]}, "{t["name"]}"}},' for t in triggers]
header += ['};','} // namespace hades::contract','']
(ROOT/'native/analysis/lua_bridge_contract.hpp').write_text('\n'.join(header))
print(json.dumps({'actions':len(actions),'triggers':len(triggers),'return_kind_counts':report['return_kind_counts']}))
