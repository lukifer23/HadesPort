"""Matched native EffectData defaults -> original Read -> PostRead DTOs."""
import copy,struct
from export_actor_catalog import literal
from sound_cue_normalization import normalize_sound_cue_source
NULLABLE={'StartFx','ReapplyFx','StartAnimation','ClearAnimation','ClearFx','OverheadFx','BackFx','FrontFx','BoostMaxStacksByName','CancelAnimationOnClear','ExpiringFrontFx','ConsumedFx','DamageFx','RequireExistingEffect','OnlyAffectName','IgnoreName','IgnoreExactName','TagName','UnitPropertyName','EnableWeaponForDuration','RefreshOnExpireWithEffect','RefreshOnExpireWithEffectAlt'}
def f32(v):return struct.unpack('<f',struct.pack('<f',v))[0]
def normalize_effect(effect,defaults):
    fields={('ChangeType'if k=='m_changeType'else k):{'type':v['type'],'value':copy.deepcopy(v['value'])}for k,v in defaults['fields'].items()}
    # Weapons helper preloads Fire BEFORE native Read, while projectile effect
    # temporaries preserve constructor Unknown0. Already normalized e.trigger
    # reflects that exact category-specific rule, independent of source omission.
    fields['Name']={'type':'string','value':effect['name']}
    fields['Type']['value']=effect['type'];fields['Trigger']['value']=effect['trigger']
    unknown=[]
    for k,v in effect['source_fields'].items():
        if k in ('Type','Trigger','Name'):continue
        if k not in fields:unknown.append(k);continue
        typ=fields[k]['type'];value=copy.deepcopy(v)
        if k=='ChangeType'and isinstance(value,str):value={'ADD':0,'MULTIPLY':1}[value.upper()]
        if k in NULLABLE and value=='null':value=''
        if typ=='float32':value=f32(value)
        elif typ=='int32':
            if not isinstance(value,int):raise ValueError('Noninteger native effect '+k)
        elif typ=='bool':
            if not isinstance(value,bool):raise ValueError('Nonboolean native effect '+k)
        elif typ=='sound_cue':value=normalize_sound_cue_source(value)
        elif typ=='color_rgba8':raise ValueError('Source effect color override needs its separate native conversion: '+effect['name'])
        fields[k]={'type':typ,'value':value}
    if unknown:raise ValueError('Unknown native effect source fields '+','.join(unknown))
    frames=fields['DurationFrames']['value']
    if frames>0:fields['Duration']={'type':'float32','value':f32(f32(frames)/f32(60))}
    effect['effective_fields']=fields
    effect['properties']={k:v['value']for k,v in fields.items()}
    effect['unresolved_defaults']=False
    effect['normalization']={'defaults_report':'.local/reports/native-effect-compiled-defaults.json','read_report':'.local/reports/native-weapon-effectnorm-effect-read-static.json','postread':'DurationFrames>0 rewrites Duration=float32(frames)/60; ChangeType source key maps native mm_changeType','sound_storage_padding_unknown':True}
    return effect

def write_effect_sidecar(output,weapons,defaults,target):
    for w in weapons:
        for effect in w['effects']:normalize_effect(effect,defaults)
    rows={w['name']:w['effects']for w in weapons}
    (output/'effects.lua').write_text('return '+literal({'schema':'hades-weapon-effects-v1','target_sha256':target,'weapon_effects':rows,'limits':['Typed native/default/source records only; actual factories/admission/Affect/Update/Clear/Dispose and original callbacks stay mandatory.','Inactive unsupported derived effect types remain source data; no Tag fallback for those factories.','Sound cue native padding bytes are unmodeled; semantic name/owner/validity retained.']})+'\n')
    return sum(len(v)for v in rows.values())
