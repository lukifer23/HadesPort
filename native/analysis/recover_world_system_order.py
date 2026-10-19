#!/usr/bin/env python3
"""Derive fresh native World ThingSystem order from retained immutable evidence.

This reproduces only bounded hash insertion/rehash, not game execution. An
additional unregistered GetSystem call changes the assumptions and must fail
admission or be modeled explicitly by a caller.
"""
import hashlib,json,math,re,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];R=ROOT/'.local/reports'
TARGET='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def recover():
    dll=ROOT/'.local/game/base/x64/EngineWin64s.dll';assert digest(dll)==TARGET
    paths=[R/('native-systems-'+n+'.json') for n in ['world-init','world-constructor','hash-access','hash-rehash','prime-rehash-required','world-get-system']]
    reports=[json.loads(p.read_text())for p in paths]
    assert all(d['dll_sha256']==TARGET for d in reports)
    init,ctor,access,rehash,required,_=[d['assembly']for d in reports]
    assert '$0x3f800000, 0x310(%rbx)'in ctor and '$0x40000000, 0x314(%rbx)'in ctor and '$0x1, 0x300(%rbx)'in ctor
    assert 'divq\t%r8'in access and 'movq\t%rsi, (%rax,%rbp,8)'in access
    assert 'movzbl\t(%rcx), %eax'in rehash and 'movq\t%rcx, (%rsi,%r8,8)'in rehash
    assert '0x1805cc400'in required and 'mulss\t0x4(%rsi), %xmm0'in required
    b=dll.read_bytes();pe=struct.unpack_from('<I',b,60)[0];n,opt=struct.unpack_from('<H',b,pe+6)[0],struct.unpack_from('<H',b,pe+20)[0]
    constants=None
    for i in range(n):
        _,va,size,raw=struct.unpack_from('<IIII',b,pe+24+opt+40*i+8)
        if va<=0x5cc400 and 0x5cc400+64<=va+size:constants=b[raw+0x5cc400-va:raw+0x5cc400-va+64]
    assert constants is not None;primes=list(struct.unpack('<16I',constants));assert primes[:7]==[2,3,5,7,11,13,17]
    assignments=[]
    for line in init.splitlines():
        m=re.search(r'^([\da-f]+):.*\tmovb\t\$(0x[\da-f]+), (-0x[\da-f]+)\(%rbp\)',line)
        if m and -0x20<=int(m[3],16)<=-0x16:assignments.append({'key':int(m[2],16),'va':'0x'+m[1],'stack_offset':m[3],'instruction':line})
    assert [d['key']for d in assignments]==[1,2,3,5,6,7,8,9,10,11,12]
    names={0:'Hatch',1:'Flash',2:'Magnetism',3:'Shake',4:'Wrap',5:'Translate',6:'PlayerNearby',7:'Vacuum',8:'Physics',9:'Body',10:'Life',11:'Text',12:'Speech'}
    pdb=R/'native-systems-pdb-types.json';pd=json.loads(pdb.read_text());assert pd['pdb_sha256']=='a75edd9c15ebbd4caa7a89ce2e3197cfb96cecb1e5a750f62a950caff48fcace'
    enum=next(line for line in pd['stdout'].splitlines()if line.startswith('0x5e4d '))
    for key,name in names.items():assert f'value: U16({key}), name: RawString("{name}")'in enum
    buckets=[[]];threshold=0;size=0;steps=[]
    for item in assignments:
        key=item['key'];new_count=size+1;before=len(buckets)
        if new_count>threshold:
            base=0 if len(buckets)==1 else len(buckets)
            if new_count>base:
                count=next(p for p in primes if p>=max(new_count,base*2));rebuilt=[[]for _ in range(count)]
                for chain in buckets:
                    for old in chain:rebuilt[old%count].insert(0,old)
                buckets=rebuilt;threshold=math.ceil(count)
            else:threshold=math.ceil(base)
        buckets[key%len(buckets)].insert(0,key);size=new_count
        steps.append({**item,'name':names[key],'buckets_before':before,'buckets_after':len(buckets),'resize_threshold':threshold,'iteration_after':[k for chain in buckets for k in chain]})
    order=[key for chain in buckets for key in chain]
    assert order==[11,12,1,2,3,5,6,7,8,9,10]
    return {'schema':'hades-fresh-world-system-order-v1','target_sha256':TARGET,'status':'Derived from exact native registration, constructor, hash/rehash and iteration instructions; no original execution','initial':{'buckets':1,'max_load_factor':1,'growth_factor':2,'next_resize':0},'native_prime_array':{'va':'0x1805cc400','raw_bytes':constants.hex(),'values':primes},'steps':steps,'final_bucket_count':len(buckets),'buckets':buckets,'order':[{'key':k,'name':names[k]}for k in order],'iteration_proof':{'report':'.local/reports/native-player-world-update-order.json','range':'1803153a5..18031540f','behavior':'Increasing bucket addresses, each node follows next+0x10; virtual Update at1803153ea'},'source_reports':[{'path':str(p.relative_to(ROOT)),'sha256':digest(p)}for p in paths+[pdb]],'limits':['Applies to a fresh World::Init with its eleven registered keys, without additional map insertions or deletions.','World::GetSystem uses inserting operator[]; a request for unregistered key changes the map and is not assumed harmless.','ThingSystem per-thing order remains its admission vector order, not sorted IDs.','This derivation is not evidence of original runtime execution.']}
if __name__=='__main__':
    result=recover();p=R/'native-fresh-world-system-order.json';p.write_text(json.dumps(result,indent=2)+'\n');print(p);print(' -> '.join(d['name']for d in result['order']))
