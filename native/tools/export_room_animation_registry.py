#!/usr/bin/env python3.13
"""Source-bound animation ownership recipes, not fabricated live registrations."""
import argparse, collections, hashlib, json
from pathlib import Path
from export_room_creation import RangedSjson
from build_room_scene import identity

TARGET='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'
def export(a):
    if a.output.exists(): raise ValueError('Fresh output directory required')
    creation=json.loads(a.creation.read_text())
    if creation['native_target']['sha256']!=TARGET: raise ValueError('Native identity mismatch')
    if identity(Path(creation['map_source']['path']))['sha256']!=creation['map_source']['sha256']: raise ValueError('Original map changed')
    defs={}; sources=[]
    for p in sorted((a.content/'Game/Animations').glob('*.sjson')):
        raw=p.read_bytes(); text=raw.decode(); parser=RangedSjson(text)
        if text.startswith('\ufeff'): parser.pos=1
        source=identity(p); sources.append(source)
        for d in parser.parse().get('Animations',[]):
            n=d['Name']
            if n in defs: raise ValueError('Duplicate original animation '+n)
            span=parser.ranges[id(d)]
            defs[n]={'raw':d,'source':{'path':source['path'],'file_sha256':source['sha256'],'range':span,'range_sha256':hashlib.sha256(raw[span[0]:sum(span)]).hexdigest()}}
    resolved={}
    def resolve(n,stack=()):
        if n in resolved: return resolved[n]
        if n in stack or len(stack)>64: raise ValueError('Original animation inheritance cycle/depth')
        own=defs[n]; parent=own['raw'].get('InheritFrom'); base=resolve(parent,stack+(n,)) if parent and parent!='null' else None
        d=dict(base['definition']) if base else {}; provenance=dict(base['property_provenance']) if base else {}
        for k,v in own['raw'].items(): d[k]=v; provenance[k]=n
        # ClearSlides applies even when the child supplies no replacement array.
        if own['raw'].get('ClearSlides'):
            d['Slides']=own['raw'].get('Slides',[]); provenance['Slides']=n
        result={'name':n,'definition':d,'property_provenance':provenance,'inheritance':([*base['inheritance']] if base else [])+[n]}
        resolved[n]=result; return result
    scene=json.loads(a.scene.read_text()); animated=json.loads(a.animations.read_text()); hero=json.loads(a.hero.read_text())
    scene_by_id=collections.defaultdict(list)
    for i,s in enumerate(scene['sprites']):scene_by_id[s['object_id']].append(i)
    clip_by_name={c['name']:c['index'] for c in animated['clips']}
    hero_by_name={c['name']:i for i,c in enumerate(hero['clips'])}
    used=set(); roots=[]; counts=collections.Counter(); source_types=collections.Counter(); spawners=[]
    for rec in creation['records']:
        name=rec['resolved_thing'].get('Graphic'); exists=name in defs
        d=resolve(name)['definition'] if exists else {}
        if exists: used.add(name)
        active=rec['raw_map']['Active'] and rec['raw_map']['DoCreate']
        kind='named' if exists else 'dataless'
        typ=d.get('Type','ConstantNoUpdate') if exists else 'Dataless'
        count_key=typ if exists else 'Dataless'
        if active: counts[count_key]+=1; source_types[d.get('Type','compiled ConstantNoUpdate3' if exists else 'Dataless')]+=1
        row={'ordinal':rec['ordinal'],'owner_map_id':rec['id'],'owner_definition':rec['definition_key'],'map_source_range':rec['source_range'],'graphic':name,'source_active':rec['raw_map']['Active'],'source_do_create':rec['raw_map']['DoCreate'],'source_attach_to':rec['raw_map']['AttachToID'],'kind':kind,'resolved_type':typ,'type_provenance':resolve(name)['property_provenance'].get('Type','compiled AnimationData ctor1800940cb Type3') if exists else 'CreateDataless branch1802defbc','manager_vector_member':False,'ownership':'Thing.primary+0x138','resources':{'scene_sprite_indices':scene_by_id.get(rec['id'],[]),'hda_clip_index':clip_by_name.get(name),'haa_clip_index':hero_by_name.get(name)},'spawn_recipe_only':True}
        roots.append(row)
        if active and d.get('VisualFx') and any(d.get(k,0)>0 for k in ['VisualFxIntervalMin','VisualFxIntervalMax','VisualFxDistanceMin','VisualFxDistanceMax']):
            spawners.append({'owner_map_id':rec['id'],'name':name,'visual_fx':d['VisualFx'],'time':[d.get('VisualFxIntervalMin',0),d.get('VisualFxIntervalMax',0)],'distance':[d.get('VisualFxDistanceMin',0),d.get('VisualFxDistanceMax',0)],'manager_vector_member':False})
    # Preserve source dependency definitions without allocating/chance-selecting them.
    queue=list(sorted(used)); closure=set(); missing=[]; edges=[]
    while queue:
        n=queue.pop(0)
        if n in closure:continue
        if len(closure)>4096:raise ValueError('Room animation dependency bound')
        closure.add(n); row=resolve(n); used.update(row['inheritance']); d=row['definition']
        for field in ['ChildAnimation','CreateAnimation','VisualFx','ChainTo','CreateAnimationOnEnd']:
            v=d.get(field)
            if v and v!='null':
                e={'from':n,'field':field,'to':v,'manager_vector_member':field in ['CreateAnimation','CreateAnimationOnEnd','VisualFx']}
                edges.append(e)
                if v in defs:queue.append(v)
                else:missing.append(e)
        for field in ['Random','CreateAnimations']:
            for entry in d.get(field,[]):
                v=entry.get('Name')
                if v and v!='null':
                    e={'from':n,'field':field,'to':v,'manager_vector_member':field=='CreateAnimations'};edges.append(e)
                    if v in defs:queue.append(v)
                    else:missing.append(e)
    used.update(closure)
    definitions=[]
    for n in sorted(used):
        r=resolve(n);d=r['definition'];fields={}
        for k in ['Type','Loop','CancelOverCap','VisualFx','ChildAnimation','CreateAnimation','CreateAnimations','VisualFxIntervalMin','VisualFxIntervalMax','VisualFxDistanceMin','VisualFxDistanceMax','OnlyWhenVisible','RandomStartFrame','RandomStartFraction','Alpha','Light']:
            if k in d:fields[k]={'value':d[k],'kind':'original_source','definition':r['property_provenance'][k]}
        # Separate the reviewed constructor-zero default from original omission.
        for k in ['Loop','CancelOverCap']:
            if k not in fields:fields[k]={'value':False,'kind':'compiled zero-template default','report':'native-active-animation-data-ctor-static.json','anchors':['0x180093d85','0x1800940b5']}
        if 'Type'not in fields:fields['Type']={'value':'ConstantNoUpdate','native_value':3,'kind':'compiled default','anchor':'0x1800940cb'}
        definitions.append({**r,**defs[n],'flags':fields})
    a.output.mkdir(parents=True)
    report={'schema':'hades-room-animation-ownership-v1','target_sha256':TARGET,'creation':identity(a.creation),'scene':identity(a.scene),'hda':identity(a.animations),'haa':identity(a.hero),'sources':sources,'primary_recipes':roots,'definitions':definitions,'dependency_edges':edges,'missing_dependencies':missing,'active_primary_counts':dict(counts),'active_source_type_counts':dict(source_types),'map_primary_spawners':spawners,'counts':{'map_recipes':len(roots),'active_primary_recipes':sum(counts.values()),'map_primary_manager_vector_members':0,'active_map_primary_vfx_spawners':len(spawners),'definitions_with_ancestors':len(definitions)},'native_contract':'native/analysis/animation-manager-ownership-contract.json','limits':['Recipes are not live allocations. Current actor renderer binds only supported banks; no fabricated animation handles.','Primary and child animations are ECS entities but not AnimationManager.mAnimations members. Manager Create/ScriptAction wrapper Add is a distinct operation.','OnSpawn/script callbacks and source CreateAnimation create real managed entries; their runtime order/count cannot be inferred from this map recipe.','Source named definitions omitted from current scene are still retained, including invisible primaries and spawners. Scene sprite indices are resource bindings, never counts.','No unsupported complex motion/random/child/light behavior is silently admitted.']}
    (a.output/'registry.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'output':str(a.output),'counts':report['counts'],'types':dict(counts),'primary_spawners':spawners,'missing_dependencies':missing}))
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['content','creation','scene','animations','hero','output']:p.add_argument('--'+name,type=Path,required=True)
    export(p.parse_args())
