#!/usr/bin/env python3
"""Export only the first original weapon, with source/compiled provenance.

No target code execution; unknown complex defaults remain absent and explicit.
"""
from pathlib import Path
import hashlib,json,struct,sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'native/tools'))
from export_room_creation import RangedSjson
REPORTS=ROOT/'.local/reports'
compiled=json.loads((REPORTS/'native-weapon-compiled-defaults.json').read_text())
assert hashlib.sha256((ROOT/'.local/game/base/x64/EngineWin64s.dll').read_bytes()).hexdigest()==compiled['target_sha256']
table={};sources=[]
for filename in ['Weapons.sjson','PlayerWeapons.sjson']:
    path=ROOT/'.local/game/base/Content/Game/Weapons'/filename
    raw=path.read_bytes();text=raw.decode('utf-8');parser=RangedSjson(text)
    if text.startswith('\ufeff'):parser.pos=1
    data=parser.parse();source={'path':str(path.relative_to(ROOT)),'sha256':hashlib.sha256(raw).hexdigest()};sources.append(source)
    for index,definition in enumerate(data['Weapons']):
        name=definition['Name'];span=parser.ranges[id(definition)]
        assert name not in table
        table[name]={'value':definition,'source':{**source,'definition_name':name,'definition_index':index,'definition_byte_range':span,'range_sha256':hashlib.sha256(raw[span[0]:sum(span)]).hexdigest()}}
chain=['1_BaseWeapon','1_BaseMagicWeapon','ArmorBreakAttack']
for a,b in zip(chain,chain[1:]):assert table[b]['value']['InheritFrom']==a
fields={}
for field in compiled['fields']:
    if field['name']=='Effects':continue
    fields[field['name']]={'type':field['type'],'value':field['value'],'native_member':field['member'],'native_definition_offset':field['offset'],'provenance':{'kind':'compiled_native_default','report':'.local/reports/native-weapon-compiled-defaults.json','field':field['name'],**field['provenance']}}
    if field['enum']:fields[field['name']]['enum_type']=field['enum']['name']
for name in chain:
    own=table[name]
    for key,value in own['value'].items():
        if key in ['Name','InheritFrom','Effect']:continue
        assert key in fields,('unmapped source property',key)
        field=fields[key];source_value=value
        if key=='Type':assert value=='GUN';value=0;field['enum_name']='GUN'
        elif field['type']=='float32':value=struct.unpack('<f',struct.pack('<f',value))[0]
        elif field['type']=='bool':assert isinstance(value,bool)
        elif field['type']=='string':assert isinstance(value,str)
        field['value']=value
        field['provenance']={'kind':'original_source_override','source':own['source'],'property_path':key,'source_value':source_value,'compiled_fallback_report':'.local/reports/native-weapon-compiled-defaults.json'}
fields['Type']['enum_name']='GUN'
effect_source=table['ArmorBreakAttack']['value']['Effect'];effect_fields={}
for key,value in effect_source.items():
    if key=='Name':continue
    typ='bool'if isinstance(value,bool)else'float32'if isinstance(value,(int,float))else'string'
    effect_fields[key]={'type':typ,'value':value,'provenance':{'kind':'original_source_override','source':table['ArmorBreakAttack']['source'],'property_path':'Effect.'+key}}
effect_fields['Type']={'type':'int32','value':12,'enum_name':'UNKNOWN','provenance':{'kind':'compiled_native_default','report':'.local/reports/native-weapon-effect-default-ctor.json','constructor_rva':'0xae2a0','store_va':'0x1800ae6ce','full_EffectData_offset':'0x68','definition_offset':0}}
state={'mAmmo':-1,'mClipShotsRemaining':0,'mChargeTimeRemaining':0.0,'mCooldownTimeRemaining':0.0,'mReloadTimeRemaining':0.0,'mNextClipRegenTime':0.0,'mCharging':False,'mLastFireTime':0.0,'mScatter':0.0,'mValidProjectileData':False,'mValidSpawnedProjectileData':False}
assert fields['StartingAmmo']['value']==fields['MaxAmmo']['value']==-1
for key in ['ClipSize','ChargeTime','InitialCooldown','ClipRegenInterval']:assert fields[key]['value']==0
for key in ['ReloadOnCreation','ReloadOnEquip']:assert fields[key]['value']is False
assert fields['Projectile']['value']=='null'
for key in ['Control','AlternateControl','SecondaryControl','DisallowedControl','SwapOnFire','AddOnFire','SwapOnRelease','CollisionWeapon','PostFireWeapon','MovingWeapon','CooldownWeapon']:assert fields[key]['value']==''
weapon={'name':'ArmorBreakAttack','inheritance_base_to_leaf':chain,'effective_fields':fields,'effect':{'name':effect_source['Name'],'effective_fields':effect_fields,'unknown_defaults':['Only listed source properties and compiled Type default recovered; other EffectData defaults not normalized.'],'container_normalization':'Original source singular Effect retained. Native singular-Effect to mEffects vector serializer mapping not recovered.'},'initial_state':state,'initial_state_provenance':{'init_report':'.local/reports/native-weapon-init.json','instance_constructor_report':'.local/reports/native-weapon-instance-owner-ctor.json','ammo_selection':'StartingAmmo>=0 ? StartingAmmo : MaxAmmo; init VA0x180088dc9..0x180088dde','validity_zero':'Owner constructor VA0x18008877c writes little-endian DWORD0x17 at this+0x11f, making bool bytes120 and121 zero.','missing_projectile':'GetProjectileData returned null branch VA0x180088c1e..0x180088c21 skips to VA0x180088d82; missing lookup validity remains false.','nondepleting_ammo':'HasAmmo VA0x180083981 MaxAmmo<0 yields true; SpendAmmo VA0x180088eb6 mAmmo==-1 returns unchanged.','timers':'init VA0x180088d8b..0x180088e3f; ReloadOnCreation false, InitialCooldown0, ReloadOnEquip false.'},'unresolved_fields':compiled['unknown_fields']+[{'name':'Effects','reason':'Compiled empty vector is not effective after original singular Effect source override; see effect record.'}],'supported_initialization_branch':{'type':'GUN','load_packages':False,'controls':'All four empty: native lookup yields null; weapon not controllable.','projectile':'Literal string null, no matching projectile definition in original Projectiles source namespace.','bundle_dependencies':'All seven bundled weapon names empty.','weapon_fire_effect_dependencies':'Effect Type UNKNOWN12, not WEAPON_FIRE8.','remaining_limits':['No firing/combat/projectile/effect-application reconstruction.','No full actor or valid active-map unit creation claim.','No source SoundCue defaults fabricated.']}}
out={'schema':'hades-first-weapon-v1','target_sha256':compiled['target_sha256'],'pdb_guid':compiled['pdb_guid'],'pdb_age':1,'source_integrity':sources,'weapons':[weapon],'compiled_default_field_count':len(compiled['fields']),'limits':['Exact first-definition scoped export. Unknown complex fields stay explicit; this is not a complete native WeaponData memory image.','Literal Projectile string null is not JSON null.','Original source records remain immutable; export is private derived game data.']}
dest=ROOT/'.local/native-assets/first-weapon-v1';dest.mkdir(parents=True,exist_ok=True)
path=dest/'weapon.json';path.write_text(json.dumps(out,indent=2,allow_nan=False)+'\n')
print(path);print(hashlib.sha256(path.read_bytes()).hexdigest());print('Fields',len(fields),'unknown',len(weapon['unresolved_fields']))
