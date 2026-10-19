#!/usr/bin/env python3
"""Bounded original Hades Primary snapshot/Reverb audit. Never executes FMOD."""
import argparse,json,sys,struct,uuid
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from hades_audio import Bank,guid_names,digest,guid_list
from inspect_audio_event import Reader,finished,parameter,prop,control,curve

def audit(content):
    path=content/'Audio/FMOD/Build/Desktop/MasterBank.bank';b=Bank(path)
    names,_=guid_names(content/'Audio/FMOD/Build/GUIDs.txt')
    event=b.nodes[names['snapshot:/Primary']];snapshot=event['snapshot_guid']
    by_parent={};own={}
    for c in b.chunks:
        by_parent.setdefault(c['parents'],[]).append(c)
        if c['kind'] in ['SNAB','PRMB','PMLB','CTRL','CURV','MAP ','BEFB','SEFB','GBSB','RBSB','IBSB','MBSB']:
            own[(c['kind'],Reader(c['payload']).guid())]=c
    def proof(c):return {'kind':c['kind'],'offset':c['offset'],'bytes':c['bytes'],'sha256':digest(c['payload'])}
    sn=own['SNAB',snapshot];r=Reader(sn['payload']);assert r.guid()==snapshot
    priority=r.i32()
    # Source 0x86 layout: key BEFORE target GUID. Pinned public parser's
    # FSnapshot field order is not aligned to these retained bytes.
    entries=r.array(lambda q:{'key':q.u32(),'target_guid':q.guid(),'target_index':q.u32(),'base_value':q.f()})
    blending=bool(r.take(1)[0]);resolution=r.u32();intensity=r.f();finished(r)
    targets={}
    for key,c in own.items():
        if key[0]in ['BEFB','SEFB','GBSB','RBSB','IBSB','MBSB']:
            if key[1]in targets:raise ValueError('Ambiguous snapshot target')
            targets[key[1]]=c
    if len(entries)!=133 or any(e['target_guid']not in targets for e in entries):raise ValueError('Snapshot target closure changed')
    entry_by_key={e['key']:e for e in entries}
    if len(entry_by_key)!=len(entries):raise ValueError('Duplicate snapshot property keys')
    reverb_guid=names['parameter:/Reverb'];pc=own['PRMB',reverb_guid];param=parameter(pc)
    layouts=[c for (kind,g),c in own.items()if kind=='PMLB'and b.nodes[g]['parameter_guid']==reverb_guid]
    if len(layouts)!=1:raise ValueError('Ambiguous Reverb parameter layout')
    owners=[c for c in by_parent[layouts[0]['parents']]if c['kind']=='CTRO']
    if len(owners)!=1:raise ValueError('Missing Reverb controller ownership')
    ctrls=guid_list(Reader(owners[0]['payload']));bindings=[]
    properties={}
    for c in b.chunks:
        if c['kind']=='PROP'and c['parents'][:len(sn['parents'])]==sn['parents']:
            p=prop(c);properties[p['index']]=(c,p)
    for guid in ctrls:
        cc=own['CTRL',guid];ctrl=control(cc);key=ctrl['property_index']&0xffffffff
        if ctrl['property_owner_guid']!=snapshot:raise ValueError('Controller outside selected snapshot')
        entry=entry_by_key[key];pchunk,p=properties[key]
        if p['controllers']!=[guid]or p['modulators']:raise ValueError('Reverb property needs unresolved combination semantics')
        cv=own['CURV',ctrl['curve_guid']];cvv=curve(cv)
        if any(pt['type']!=0 or pt['shape']!=0 for pt in cvv['points']):raise ValueError('Nonlinear Reverb source curve')
        mapping=own['MAP ',p['mapping_guid']];q=Reader(mapping['payload']);q.guid();points=q.array(lambda x:[x.f(),x.f()]);finished(q)
        target=targets[entry['target_guid']];kind=target['kind'];effect_type=None
        if kind=='BEFB':
            q=Reader(target['payload']);q.guid();effect_type=q.u32()
            if effect_type!=19 or entry['target_index']!=1000:raise ValueError('Unreviewed Reverb effect field')
        elif kind not in ['RBSB','SEFB']or entry['target_index']!=0:raise ValueError('Unreviewed Reverb target kind')
        bindings.append({**entry,'target_kind':kind,'effect_type':effect_type,'property_method':p['method'],'property_type':p['type'],
          'curve':cvv['points'],'mapping':points,'source':{'target':proof(target),'property':proof(pchunk),'controller':proof(cc),'curve':proof(cv),'mapping':proof(mapping)}})
    if len(bindings)!=5:raise ValueError('Selected Reverb closure changed')
    return {'schema':'hades-primary-reverb-snapshot-v1','source_bank':b.original,'snapshot_guid':snapshot,'snapshot_event':names['snapshot:/Primary'],
      'snapshot_source':proof(sn),'priority':priority,'blending':blending,'resolution':resolution,'intensity':intensity,
      'entry_count':len(entries),'base_entries':[{**e,'controlled':e['key']in properties,'target_kind':targets[e['target_guid']]['kind'],'target_source':proof(targets[e['target_guid']])}for e in entries],'all_target_guids_verified':True,'parameter':param,'parameter_source':proof(pc),'initial':0,
      'initial_proof':'Native AudioManager.LoadMixing VA180047f27 xorps xmm2,xmm2 then LoadMixingParam at180047f31. Selected source initial override differs from PRMB default-1.',
      'bindings':bindings,'limits':['Source points/mapping/target identities are retained observations. Portable curve and mapping interpolation is piecewise linear, not proprietary FMOD execution proof.',
       'Only Reverb controls ported in this capsule; other persistent MixingControl parameters remain explicit obligations.']}

if __name__=='__main__':
    root=Path(__file__).resolve().parents[2];p=argparse.ArgumentParser();p.add_argument('--content',type=Path,default=root/'.local/game/base/Content');p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.output.exists()or a.output.is_symlink():raise ValueError('Use fresh audit output')
    out=audit(a.content);a.output.write_text(json.dumps(out,indent=2)+'\n');print(json.dumps({'entries':out['entry_count'],'bindings':len(out['bindings']),'all_targets_verified':out['all_target_guids_verified']}))
