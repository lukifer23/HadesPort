"""Original projectile nested ThingData normalization; no game code execution."""
import copy,json,struct
from build_room_scene import merge
from export_actor_catalog import literal
NULLABLE={'Graphic','ActivateGraphic','ActivateFx','ImpulsedAnimation','ImpulsedFx','FootstepFxL','FootstepFxR','AttachToMarker','OcclusionBaseAnim','AttachedAnim','OnTouchdownFxAnim','TouchdownGraphic','Light','SpeechMeteringChannelName'}
def f32(x):return struct.unpack('<f',struct.pack('<f',x))[0]
def normalize(projectile,defaults):
    original=projectile['effective_fields'].get('Thing',{'value':{},'provenance':{'kind':'no_explicit_nested_Thing_source'}})
    raw=original['value'];values={k:copy.deepcopy(v['value'])for k,v in defaults['fields'].items()}
    properties=merge(values,raw);typed={k:{'type':v['type'],'value':copy.deepcopy(v['value']),'provenance':{'kind':'compiled_native_default','report':'.local/reports/native-projectile-thing-compiled-defaults.json','field':k}}for k,v in defaults['fields'].items()}
    for k,v in raw.items():
        typ=typed.get(k,{}).get('type','source_json');value=copy.deepcopy(v)
        if typ=='float32':value=f32(value)
        if k in NULLABLE and value=='null':value=''
        # Source Color.Opacity is preserved verbatim rather than silently
        # overlaid with the compiled Color.Alpha name from the DTO decoder.
        if k in ('Color','OutlineColor','SubtitleColor')and isinstance(value,dict):
            current=copy.deepcopy(values.get(k,{}));current.update(value)
            if 'Opacity'in value:current.pop('Alpha',None)
            value=current
        properties[k]=value;typed[k]={'type':typ,'value':value,'provenance':{'kind':'original_nested_Thing_override','property':k,'projectile_Thing_source':original['provenance']}}
    # Native ThingDataDef SGD_Deserialize post-loop folds integer Width/Height
    # and OffsetX/Y into Point2 only if both existing components equal zero.
    # No current 46-source closure uses these aliases; keep proven rule usable.
    for target,x,y in [('Size','Width','Height'),('Offset','OffsetX','OffsetY')]:
        if x in raw or y in raw:
            if properties[target]['X']==0 and properties[target]['Y']==0:
                value={'X':f32(int(raw.get(x,0))),'Y':f32(int(raw.get(y,0)))}
                properties[target]=value;typed[target]={'type':'point2','value':value,'provenance':{'kind':'native_deserializer_alias_postloop','report':'.local/reports/native-player-projectile-thing-deserialize.json','instruction_range':'0x1800ef269..0x1800ef2f7'}}
    unresolved=[x['name']for x in defaults['unknown']if x['name']not in raw]
    # For current source closure the only vectors with unproven compiler-copy
    # bytes are recorded separately; source Points are real explicit polygons.
    projectile['effective_fields']['Thing']={'type':'source_json','value':properties,'typed_fields':typed,'unresolved_fields':unresolved,'provenance':{'kind':'compiled_Thing_defaults_plus_original_inheritance','source':original['provenance'],'defaults_report':'.local/reports/native-projectile-thing-compiled-defaults.json','deserializer_report':'.local/reports/native-player-projectile-thing-deserialize.json'}}
    return {'name':projectile['name'],'thing':properties,'thing_property_types':{k:v['type']for k,v in typed.items()},'unresolved_thing_fields':unresolved,'source_thing':raw}
def write_sidecar(output,projectiles,defaults,target):
    rows=[normalize(p,defaults)for p in projectiles]
    packet={'schema':'hades-projectile-thing-catalog-v1','target_sha256':target,'projectiles':{r['name']:r for r in rows},'limits':['Original source inheritance and proven compiled Thing defaults. This does not allocate a projectile or claim World registration.','All omitted Thing definition fields have source-backed semantic defaults; native storage padding remains unmodeled. Actual source polygon values are retained.','Color source Opacity spelling is preserved; actual renderer must apply native packed color conversion.']}
    (output/'projectiles.lua').write_text('return '+literal(packet)+'\n')
    return rows
