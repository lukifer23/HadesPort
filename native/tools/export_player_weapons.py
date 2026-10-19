#!/usr/bin/env python3
"""Exact-source player initialization closure. Private derived data; no target execution."""
import argparse, copy, hashlib, json, struct, sys, re
from pathlib import Path
from export_room_creation import RangedSjson
from build_room_scene import merge
from projectile_thing_data import write_sidecar
from lua_static import LuaFile,top_level_tables
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'native/analysis'))
from sound_cue_normalization import normalize_sound_cue_source
from weapon_effect_data import write_effect_sidecar,normalize_effect
TARGET='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'
CONTROLS=('Control','AlternateControl','SecondaryControl','DisallowedControl')
BUNDLES=('SwapOnFire','AddOnFire','SwapOnRelease','CollisionWeapon','PostFireWeapon','MovingWeapon','CooldownWeapon')
SLOTS=('OnHitWeapon','OnStunWeapon','OnDeathWeapon','OnTouchdownWeapon','CollisionWeapon','InitiatedCollisionWeapon','ContinuousWeapon')
EFFECT_TYPES={'SPEED':0,'GRIP':1,'DAMAGE_OUTPUT':2,'DAMAGE_TAKEN':3,'DAMAGE_OVER_TIME':4,'DAMAGE_OVER_DISTANCE':5,'DELAYED_KNOCKBACK':6,'CHARM':7,'WEAPON_FIRE':8,'PROJECTILE_DEFENSE':9,'TAG':10,'INVULNERABLE':11,'UNKNOWN':12}
def sha(p):return hashlib.file_digest(p.open('rb'),'sha256').hexdigest()
def f32(v):return struct.unpack('<f',struct.pack('<f',v))[0]
def lua_table_block(path,marker):
    raw=path.read_bytes();text=raw.decode('utf-8-sig');start=text.index(marker);begin=text.index('{',start);level=0;quoted=False;escaped=False
    for at in range(begin,len(text)):
        c=text[at]
        if quoted:
            if escaped:escaped=False
            elif c=='\\':escaped=True
            elif c=='"':quoted=False
        elif c=='"':quoted=True
        elif c=='{':level+=1
        elif c=='}':
            level-=1
            if not level:return text[begin:at+1],{'path':str(path.relative_to(ROOT)),'sha256':sha(path),'marker':marker,'source_block':text[begin:at+1]}
    raise ValueError('Unclosed original Lua table')
def export(output,projectile_defaults='native-player-projectile-compiled-defaults.json',encounter_room='RoomSimple01'):
    if output.exists():raise ValueError('Output must be new')
    assert sha(ROOT/'.local/game/base/x64/EngineWin64s.dll')==TARGET
    sources=[];tables={}
    for cat in ('Weapons','Projectiles','Units'):
        table={};tables[cat]=table
        for path in sorted((ROOT/'.local/game/base/Content/Game'/cat).glob('*.sjson')):
            raw=path.read_bytes();text=raw.decode('utf-8');p=RangedSjson(text)
            if text.startswith('\ufeff'):p.pos=1
            parsed=p.parse();identity={'path':str(path.relative_to(ROOT)),'sha256':sha(path)};sources.append(identity)
            for i,d in enumerate(parsed.get(cat,[])):
                n=d['Name'];assert n not in table,(cat,n)
                span=p.ranges[id(d)];table[n]={'value':d,'source':{**identity,'definition_index':i,'byte_range':span,'range_sha256':hashlib.sha256(raw[span[0]:sum(span)]).hexdigest()}}
    def resolve(cat,name,stack=()):
        if name in stack:raise ValueError('Inheritance cycle')
        own=tables[cat][name];v=own['value'];chain=[];fields={};effects=[]
        parent=v.get('InheritFrom')
        if parent:
            assert isinstance(parent,str)
            chain,fields,effects=resolve(cat,parent,stack+(name,))
        fields=copy.deepcopy(fields);effects=copy.deepcopy(effects)
        for k,x in v.items():
            if k in ('Name','InheritFrom'):continue
            if k in ('Effect','Effects'):
                incoming=[{'source':own['source'],'value':e} for e in ([x]if k=='Effect'else x)]
                if cat=='Projectiles':
                    # Native EffectDataAddReplace RVAe1050 reads each source
                    # effect into a FRESH constructor-default temporary, then
                    # replaces every same-name vector entry or appends it.
                    # Other inherited entries and their ordering survive.
                    for effect in incoming:
                        effect_name=effect['value']['Name'];matches=[i for i,old in enumerate(effects)if old['value']['Name']==effect_name]
                        if matches:
                            for i in matches:effects[i]=copy.deepcopy(effect)
                        else:effects.append(copy.deepcopy(effect))
                else:effects=incoming
                continue
            if cat=='Projectiles' and k=='RemoveEffects' and x:
                raise ValueError('Projectile RemoveEffects inheritance requires its separate native branch: '+name)
            previous=fields.get(k)
            if previous and isinstance(previous['value'],dict) and isinstance(x,dict):
                fields[k]={'value':merge(previous['value'],x),'provenance':{'kind':'original_nested_source_inheritance','inherited':previous['provenance'],'source':own['source'],'property_path':k,'explicit_leaf_value':x}}
            else:
                fields[k]={'value':copy.deepcopy(x),'provenance':{'kind':'original_source_override','source':own['source'],'property_path':k}}
        return chain+[name],fields,effects
    defaults={}
    for cat,file in [('Weapons','native-weapon-compiled-defaults.json'),('Projectiles',projectile_defaults)]:
        d=json.loads((ROOT/'.local/reports'/file).read_text());assert d['target_sha256']==TARGET
        defaults[cat]=d
    sound_defaults=json.loads((ROOT/'.local/reports/native-soundcue-compiled-defaults.json').read_text());assert sound_defaults['dll_sha256']==TARGET
    sound_names={f['name']for f in sound_defaults['fields']}
    projectile_sounds=json.loads((ROOT/'.local/reports/native-projectile-soundcue-compiled-defaults.json').read_text());assert projectile_sounds['dll_sha256']==TARGET
    projectile_sound_names={f['name']for f in projectile_sounds['fields']}
    offset_default=json.loads((ROOT/'.local/reports/native-weapon-projectile-offset-justification.json').read_text());assert offset_default['dll_sha256']==TARGET
    def normalized(cat,name):
        chain,explicit,effects=resolve(cat,name);fields={}
        for f in defaults.get(cat,{}).get('fields',[]):
            if f['name']=='Effects':continue
            fields[f['name']]={'type':f['type'],'value':f['value'],'native_definition_offset':f['offset'],'provenance':{'kind':'compiled_native_default','report':'.local/reports/'+('native-weapon-compiled-defaults.json'if cat=='Weapons'else projectile_defaults),'stores':f['provenance']}}
            if f.get('enum') and 'values'in f['enum']:fields[f['name']]['enum']=f['enum']
        if cat=='Weapons':
            field=offset_default['field'];fields['ProjectileOffsetStart']={'type':'uint8','value':field['value'],'enum_type':field['enum_type'],'enum_name':field['enum_name'],'provenance':field['provenance']}
            for f in sound_defaults['fields']:
                fields[f['name']]={'type':'sound_cue','value':f['value'],'provenance':{'kind':'compiled_native_default','report':'.local/reports/native-soundcue-compiled-defaults.json','stores':f['provenance']}}
        elif cat=='Projectiles':
            for f in projectile_sounds['fields']:
                fields[f['name']]={'type':'sound_cue','value':f['value'],'native_definition_offset':f['offset'],'provenance':{'kind':'compiled_native_default','report':'.local/reports/native-projectile-soundcue-compiled-defaults.json','stores':f['provenance']}}
        for k,v in explicit.items():
            typ=fields.get(k,{}).get('type','source_json');value=v['value']
            enum=fields.get(k,{}).get('enum')
            if enum and isinstance(value,str):
                source_enum_name=value;value={key.lower():val for key,val in enum['values'].items()}[value.lower()]
                v=copy.deepcopy(v);v['provenance']['native_enum_source_name']=source_enum_name;v['provenance']['enum_evidence']=enum
            if cat=='Weapons' and k=='ProjectileOffsetStart' and isinstance(value,str):value={'LEFT':0,'RIGHT':1,'CENTER':2,'UNKNOWN':3}[value]
            if (cat=='Weapons' and k in sound_names)or(cat=='Projectiles' and k in projectile_sound_names):
                value=normalize_sound_cue_source(value);typ='sound_cue'
            if typ=='float32' and isinstance(value,(int,float)):value=f32(value)
            if cat=='Projectiles' and k in {'IncreaseAdjustRateOfFriendlyProjectilesInside','MaxAdjustRate','AdjustRateAcceleration','SpinRate','ArcStart','ArcEnd','ArcSize'}:
                # SGD_Deserialize(ProjectileDataDef) does float32 -> double *
                # exact pi/180 -> float32 at source assignment. resolve keeps
                # original source values, so normalize once after inheritance;
                # never re-normalize an already inherited native value.
                serialized=value;value=f32(float(f32(value))*0.017453292519943295)
                v=copy.deepcopy(v);v['provenance']={'kind':'native_inline_degrees_to_radians','serialized_source_value':serialized,'source_assignment':v['provenance'],'report':'.local/reports/native-player-projectile-deserialize-properties.json','constant_va':'0x180610e80','constant_float64':0.017453292519943295}
            fields[k]={**fields.get(k,{}),**v,'value':value,'type':typ}
        if cat=='Projectiles' and isinstance(fields['SpawnType']['value'],str):
            fields['SpawnType']['value']={'UNKNOWN':0,'OBSTACLE':1,'UNIT':2,'PROJECTILE':3,'WEAPON':4,'ANIMATION':5,'TEXT':6,'COMPONENT':7,'PREFAB':8}[fields['SpawnType']['value']]
            fields['SpawnType']['enum_type']='sgg::GameDataType'
            fields['SpawnType']['provenance']['enum_evidence']='.local/reports/native-player-spawn-type-pdb.txt; Weapon::init compares Projectile3'
        if cat=='Weapons':
            fields['Type']['value']={'GUN':0,'BLINK':1,'RAM':2}.get(fields['Type']['value'],fields['Type']['value'])
            for frames,seconds in [('ChargeTimeFrames','ChargeTime'),('DisableOwnerForDurationFrames','DisableOwnerForDuration')]:
                if fields[frames]['value']>=0:
                    fields[seconds]={'type':'float32','value':f32(f32(fields[frames]['value'])/f32(60)),'provenance':{'kind':'native_postread_frames_conversion','frames_field':frames,'report':'.local/reports/native-player-weapon-postread.json'}}
        outEffects=[]
        for e in effects:
            v=e['value'];typ=v.get('Type','UNKNOWN');trigger=v.get('Trigger','Fire'if cat=='Weapons'else'Unknown')
            outEffects.append({'name':v['Name'],'type':{k.replace('_','').lower():v for k,v in EFFECT_TYPES.items()}[typ.replace('_','').lower()]if isinstance(typ,str)else typ,'trigger':{'Unknown':0,'Fire':1,'FireEnd':2,'Charging':3}.get(trigger,trigger),'source_fields':v,'source':e['source'],'execution_supported':False,'unresolved_defaults':True})
        result={'name':name,'inheritance_base_to_leaf':chain,'effective_fields':fields,'effects':outEffects,'unresolved_fields':[f for f in defaults.get(cat,{}).get('unknown_fields',[])if not(cat=='Weapons' and f['name']in (sound_names|{'ProjectileOffsetStart'}))]}
        if cat=='Projectiles':
            # Original ctor zeroes temporary RemoveEffects vector's three
            # pointers via xmm1@1800d89f4 and xmm0@1800d8a03; assignment d8a6e.
            # resolve above explicitly rejects every nonempty source override.
            result['remove_effects_known_empty']=True
            result['remove_effects_provenance']={'report':'.local/reports/native-player-projectile-default-ctor.json','member_definition_offset':'0x6e8','temporary_offset':'RBP+0x608','stores':['0x1800d89f4','0x1800d8a03'],'source_policy':'nonempty source RemoveEffects rejected, not silently discarded'}
        return result
    initial,unitFields,_=resolve('Units','_PlayerUnit')
    slots=[unitFields.get(k,{}).get('value','')for k in SLOTS]
    # Omitted strings need compiled UnitData proof independently; only explicit
    # source OnTouchdownWeapon is promoted to the executable startup list.
    startup=[{'slot':k,'name':unitFields[k]['value'],'provenance':unitFields[k]['provenance']}for k in SLOTS if k in unitFields]
    requests=[x['name']for x in startup if x['name']]+['ArmorBreakAttack','RushWeapon','RangedWeapon','SwordWeapon','SwordParry']
    set_block,set_source=lua_table_block(ROOT/'.local/game/base/Content/Scripts/WeaponSets.lua','HeroWeaponSets =')
    hero_sets={name:re.findall(r'"([^"\\]*)"',body)for name,body in re.findall(r'(\w+)\s*=\s*\{([^{}]*)\}',set_block)}
    assert set(hero_sets)=={'SwordWeapon','SpearWeapon','ShieldWeapon','BowWeapon','FistWeapon','GunWeapon'}
    sources.append({k:v for k,v in set_source.items()if k!='source_block'})
    missing_block,missing_source=lua_table_block(ROOT/'.local/game/base/Content/Scripts/WeaponData.lua','GameData.MissingPackages =')
    missing_packages=re.findall(r'"([^"\\]*)"',missing_block)
    sources.append({k:v for k,v in missing_source.items()if k!='source_block'})
    # Bound gameplay closure: derive the forced first encounter from the
    # actual room table, then its authored wave spawns. Include both native
    # Unit.Init startup slots and Lua SetupEnemyObject WeaponOptions, following
    # source inheritance. Do not admit unrelated generated encounters/enemies.
    encounter_units=[];encounter_provenance={}
    def lua_tables(pattern):
        result={}
        for path in sorted((ROOT/'.local/game/base/Content/Scripts').glob(pattern)):
            source={'path':str(path.relative_to(ROOT)),'sha256':sha(path)}
            sources.append(source)
            for key,value,line,*_ in top_level_tables(LuaFile(path)):
                if key in result:raise ValueError('Duplicate Lua data root '+key)
                result[key]=(value,{**source,'root':key,'line':line})
        return result
    # Physics invokes the original OnTouchdown script for every obstacle. Its
    # inherited OnTouchdown.Weapon is a weapon producer independently of unit
    # startup slots. Admit the whole original field class, retaining record
    # and source provenance, then reuse native weapon/projectile closure.
    obstacle_tables=lua_tables('ObstacleData*.lua')
    obstacle_data={};obstacle_sources={}
    for root,(value,proof) in obstacle_tables.items():
        if root!='ObstacleData' or not isinstance(value,dict):continue
        for name,record in value.items():
            if isinstance(record,dict):obstacle_data[name]=record;obstacle_sources[name]=proof
    def obstacle(name,stack=()):
        if name in stack:raise ValueError('Obstacle Lua inheritance cycle')
        own=obstacle_data[name];out={};parents=own.get('InheritFrom',[])
        for parent in ([parents]if isinstance(parents,str)else parents):out=merge(out,obstacle(parent,stack+(name,)))
        return merge(out,own)
    touchdown_rows=[]
    for name in sorted(obstacle_data):
        value=obstacle(name).get('OnTouchdown',{})
        if not isinstance(value,dict)or 'Weapon'not in value:continue
        weapon=value['Weapon']
        if not isinstance(weapon,str):raise ValueError('Original OnTouchdown.Weapon requires literal source name')
        if weapon and weapon!='null':requests.append(weapon)
        touchdown_rows.append({'obstacle':name,'weapon':weapon,'field':'OnTouchdown.Weapon','source':obstacle_sources[name],'inheritance':'original parent-then-own Lua merge'})
    touchdown_provenance={'class':'ObstacleData.OnTouchdown.Weapon','records':touchdown_rows,'consumer':'Original obstacle OnTouchdown callback reached from native physics; original table inheritance resolved before lookup.'}
    if encounter_room:
        room_tables=lua_tables('RoomData*.lua');matches=[(value[encounter_room],proof)for value,proof in room_tables.values()if isinstance(value,dict)and encounter_room in value]
        if len(matches)!=1:raise ValueError('Forced encounter room not uniquely source-bound: '+encounter_room)
        room,roomproof=matches[0];encounter=room.get('ForceIfEncounterNotCompleted')
        if not isinstance(encounter,str):raise ValueError('Bound room requires literal forced encounter')
        encounters=lua_tables('EncounterData*.lua');matches=[(value[encounter],proof)for value,proof in encounters.values()if isinstance(value,dict)and encounter in value]
        if len(matches)!=1:raise ValueError('Forced encounter not uniquely source-bound: '+encounter)
        encounter_data,encounterproof=matches[0]
        for wave in encounter_data.get('SpawnWaves',[]):
            for spawn in wave.get('Spawns',[]):
                name=spawn.get('Name')
                if not isinstance(name,str):raise ValueError('Encounter spawn Name requires literal')
                if name not in encounter_units:encounter_units.append(name)
        enemies=lua_tables('EnemyData*.lua');enemy_data={}
        for value,_ in enemies.values():
            if isinstance(value,dict):
                for key,item in value.items():
                    if isinstance(item,dict):
                        if key in enemy_data:raise ValueError('Duplicate original enemy data '+key)
                        enemy_data[key]=item
        def enemy(name,stack=()):
            if name in stack:raise ValueError('Enemy Lua inheritance cycle')
            own=enemy_data[name];out={};parents=own.get('InheritFrom',[])
            if isinstance(parents,str):parents=[parents]
            for parent in parents:out=merge(out,enemy(parent,stack+(name,)))
            return merge(out,own)
        rows=[]
        for name in encounter_units:
            chain,fields,_=resolve('Units',name)
            native_slots=[{'slot':slot,'name':fields[slot]['value'],'provenance':fields[slot]['provenance']}for slot in SLOTS if slot in fields]
            options=enemy(name).get('WeaponOptions',[])
            if not isinstance(options,list)or not all(isinstance(n,str)for n in options):raise ValueError('Enemy WeaponOptions require literal list')
            requests.extend(row['name']for row in native_slots if row['name']);requests.extend(options)
            rows.append({'unit_name':name,'inheritance_base_to_leaf':chain,'explicit_initial_slots':native_slots,'lua_weapon_options':options})
        encounter_provenance={'room':encounter_room,'room_source':roomproof,'encounter':encounter,'encounter_source':encounterproof,'units':rows}
    weapons=[];seen=set();missing_weapons=set();missing_projectiles=set()
    def add(name):
        if not name or name=='null':return
        if name in seen:return
        if name not in tables['Weapons']:missing_weapons.add(name);return
        seen.add(name);w=normalized('Weapons',name);weapons.append(w)
        for key in BUNDLES:add(w['effective_fields'][key]['value'])
        for e in w['effects']:
            if e['type']==8:add(e['name'])
    for n in requests:add(n)
    for n,linked in hero_sets.items():
        add(n)
        for item in linked:add(item)
    projectiles=[];pseen=set()
    def addproj(name):
        if not name or name=='null' or name in pseen:return
        if name not in tables['Projectiles']:missing_projectiles.add(name);return
        pseen.add(name);p=normalized('Projectiles',name);projectiles.append(p)
        fields=p['effective_fields']
        # These names are copied during Weapon::init only for SpawnType3.
        if fields['SpawnType']['value']==3:
            for k in ('SpawnOnDetonate','SpawnOnImpact','SpawnOnDeath'):addproj(fields[k]['value'])
    for w in weapons:addproj(w['effective_fields']['Projectile']['value'])
    packages=[]
    package_root=ROOT/'.local/game/base/Content/Win/Packages'
    for w in weapons:
        package=package_root/(w['name']+'.pkg');manifest=package_root/(w['name']+'.pkg_manifest')
        if package.is_file():packages.append({'name':w['name'],'path':str(package.relative_to(ROOT)),'sha256':sha(package),'bytes':package.stat().st_size,'manifest':{'path':str(manifest.relative_to(ROOT)),'sha256':sha(manifest)}if manifest.is_file()else None})
    data={'schema':'hades-player-weapons-v1','target_sha256':TARGET,'source_integrity':sources,'weapons':weapons,'projectiles':projectiles,'controls':[{'name':n,'native_index':i}for n,i in [('Attack1',21),('Attack2',22),('Attack3',23),('Rush',25)]],'unit_loadouts':[{'unit_name':'_PlayerUnit','inheritance_base_to_leaf':initial,'explicit_initial_slots':startup,'lua_initial_requests':requests[len(startup):]}],'hero_weapon_sets':hero_sets,'hero_weapon_sets_source':set_source,'missing_lua_packages':missing_packages,'missing_lua_packages_source':missing_source,'available_weapon_packages':packages,'current_default_sword_equip_requests':['ArmorBreakAttack','RushWeapon','RangedWeapon','SwordParry','SwordWeapon']+hero_sets['SwordWeapon'],'known_missing_weapons':['','null']+sorted(missing_weapons),'known_missing_projectiles':['','null']+sorted(missing_projectiles),'limits':['Initialization and source catalog only. No firing, effects, projectile actor simulation or live unit creation.','Seven native startup slots are identified; omitted UnitData strings are not promoted without default provenance.','Full typed effective properties retain source/compiled provenance; complex unknown defaults remain explicit.','Effect order follows source inheritance replacement; no effect execution or omitted effect defaults claimed.','Nested Thing source objects retain inherited source fields; compiled ThingData defaults and postprocessing remain outside this export.']}
    data['forced_encounter_closure']=encounter_provenance
    data['obstacle_touchdown_weapon_closure']=touchdown_provenance
    # Same AddControls +8 Index assignment mechanism as Attack1/2/3/Rush.
    # Use is5 (VA18018e6f1), not the adjacent weapon-control index24.
    data['controls'].append({'name':'Use','native_index':5})
    # Same AddControls Index stores: Shout 0x18 (VA18018f1be), Assist 0x1c (VA18018f39a);
    # script-level controls (OnControlPressed), no weapon binding by default.
    data['controls'].append({'name':'Shout','native_index':24});data['controls'].append({'name':'Assist','native_index':28})
    data['control_identity_provenance']={'report':'.local/reports/native-player-control-add.json','sha256':sha(ROOT/'.local/reports/native-player-control-add.json'),'Use':{'global':'0x181ca1d70','load_va':'0x18018e696','index_store_va':'0x18018e6f1','index':5,'layout_report':'.local/reports/native-interaction-control-layout-v1.txt','layout_sha256':sha(ROOT/'.local/reports/native-interaction-control-layout-v1.txt')}}
    data['compiled_provenance']=[{'path':'.local/reports/'+name,'sha256':sha(ROOT/'.local/reports'/name)}for name in ['native-weapon-compiled-defaults.json',projectile_defaults,'native-soundcue-compiled-defaults.json','native-projectile-soundcue-compiled-defaults.json','native-weapon-projectile-offset-justification.json']]
    data['loader_semantics']={'Weapons.Effect/Effects':'replace full vector, preload each Trigger Fire1 then native EffectData.Read','Projectiles.Effect/Effects':'fresh EffectData defaults (Trigger Unknown0); replace every matching Name in place, append if absent; retain other inherited entries','Projectiles.RemoveEffects':'explicit unsupported exporter branch if present/nonempty','Projectiles.angular_fields':'source float32 -> double*pi/180 -> float32 for IncreaseAdjustRateOfFriendlyProjectilesInside,MaxAdjustRate,AdjustRateAcceleration,SpinRate,ArcStart,ArcEnd,ArcSize; compiled defaults untouched','projectile_effect_evidence':['.local/reports/native-player-projectile-deserialize-properties.json','.local/reports/native-player-projectile-effects-add-replace.json']}
    effect_defaults=json.loads((ROOT/'.local/reports/native-effect-compiled-defaults.json').read_text());assert effect_defaults['target_sha256']==TARGET
    data['compiled_provenance'].append({'path':'.local/reports/native-effect-compiled-defaults.json','sha256':sha(ROOT/'.local/reports/native-effect-compiled-defaults.json')})
    thing_defaults=json.loads((ROOT/'.local/reports/native-projectile-thing-compiled-defaults.json').read_text());assert thing_defaults['target_sha256']==TARGET
    data['compiled_provenance'].append({'path':'.local/reports/native-projectile-thing-compiled-defaults.json','sha256':sha(ROOT/'.local/reports/native-projectile-thing-compiled-defaults.json')})
    data['exporter']={'path':'native/tools/export_player_weapons.py','sha256':sha(Path(__file__))}
    data['limits']=[s.replace('Effect order follows source inheritance replacement;','Effects follow category-specific native loader semantics (weapon replacement, projectile name AddReplace);')for s in data['limits']]
    output.mkdir(parents=True)
    thing_rows=write_sidecar(output,projectiles,thing_defaults,TARGET)
    effect_count=write_effect_sidecar(output,weapons,effect_defaults,TARGET)
    for projectile in projectiles:
        for effect in projectile["effects"]:normalize_effect(effect,effect_defaults)
    data['limits']=[x for x in data['limits']if not x.startswith('Nested Thing source')]
    data['limits'].append('Nested Thing includes proven compiled defaults, original inherited source, nullable string normalization and explicit unresolved vector defaults; actual Thing initialization remains a real runtime consumer.')
    data['loader_semantics']['Projectiles.Thing']='Compiled scalar/string/color/Point2/sound defaults then inherited source; nullable string literal null clears; Width/Height and OffsetX/Y conditional post-loop aliases.'
    source=output/'catalog.json';source.write_text(json.dumps(data,indent=2,allow_nan=False)+'\n')
    packet=bytearray(b'HWA3'+bytes.fromhex(sha(source))+bytes(32)+bytes.fromhex(TARGET))
    def u(v):packet.extend(struct.pack('<I',v))
    def i(v):packet.extend(struct.pack('<i',v))
    def f(v):packet.extend(struct.pack('<f',v))
    def b(v):packet.extend(struct.pack('<B',bool(v)))
    def text(v):
        raw=v.encode();assert len(raw)<=1048576 and b'\0'not in raw;u(len(raw));packet.extend(raw)
    def js(v):return json.dumps(v,separators=(',',':'),allow_nan=False)
    u(len(weapons))
    for w in weapons:
        field=lambda k:w['effective_fields'][k]['value']
        for v in (w['name'],field('Projectile'),w['effects'][0]['name']if w['effects']else''):text(v)
        for v in (field('Type'),field('StartingAmmo'),field('MaxAmmo'),field('ClipSize'),w['effects'][0]['type']if w['effects']else 12,w['effects'][0]['trigger']if w['effects']else 1):i(v)
        for k in ('ChargeTime','ClipRegenInterval','InitialCooldown'):f(field(k))
        for v in (field('ReloadOnCreation'),field('ReloadOnEquip'),field('DefaultControl'),True):b(v)
        for k in CONTROLS+BUNDLES:text(field(k))
        unknown=[x['name']for x in w['unresolved_fields']];u(len(unknown))
        for v in unknown:text(v)
        for k in ('ControlWindow','Cooldown','CooldownMin','CooldownMax','ReloadTime','ReloadTimeMin','ReloadTimeMax'):f(field(k))
        b(field('IgnoreForceCooldown'))
        for k in ('ReloadGraphicFinish','SwapOnReloadFinish','ShareAmmoWeapon'):text(field(k))
        text(js({k:{'type':v['type'],'value':v['value']}for k,v in w['effective_fields'].items()}));u(len(w['effects']))
        for e in w['effects']:text(e['name']);i(e['type']);i(e['trigger']);text(js(e))
    u(len(projectiles))
    for p in projectiles:
        fields=p['effective_fields'];text(p['name']);i(fields['SpawnType']['value'])
        for k in ('SpawnOnDetonate','SpawnOnImpact','SpawnOnDeath'):text(fields[k]['value'])
        # Keep the constructor/source closure proof in the consumed packet,
        # alongside the catalog. Runtime cannot infer it from omitted fields.
        text(js({'name':p['name'],'effective_fields':{k:{'type':v['type'],'value':v['value']}for k,v in p['effective_fields'].items()},'effects':p['effects'],'unresolved_fields':[f['name']for f in p['unresolved_fields']],'remove_effects_known_empty':p['remove_effects_known_empty']}))
    u(len(data['controls']))
    for c in data['controls']:text(c['name']);i(c['native_index'])
    u(len(data['unit_loadouts']))
    for l in data['unit_loadouts']:
        text(l['unit_name']);u(len(l['explicit_initial_slots']))
        for s in l['explicit_initial_slots']:text(s['slot']);text(s['name'])
    for key in ('known_missing_weapons','known_missing_projectiles'):
        u(len(data[key]))
        for name in data[key]:text(name)
    path=output/'weapons.bin';path.write_bytes(packet)
    manifest={'schema':'hades-player-weapons-runtime-v3','catalog_sha256':sha(source),'packet_sha256':sha(path),'packet_bytes':len(packet),'weapon_count':len(weapons),'projectile_count':len(projectiles),'projectile_effect_count':sum(len(p['effects'])for p in projectiles),'names':[w['name']for w in weapons],'forced_encounter_closure':encounter_provenance,'obstacle_touchdown_weapon_closure':touchdown_provenance,'hero_weapon_sets':hero_sets,'available_weapon_packages':packages,'default_sword_requested_packages':['SwordParry','SwordWeapon'],'weapon_effect_packet':{'path':'effects.lua','sha256':sha(output/'effects.lua'),'count':effect_count},'projectile_thing_packet':{'path':'projectiles.lua','sha256':sha(output/'projectiles.lua'),'count':len(thing_rows)},'limits':data['limits']}
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(manifest,indent=2))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--projectile-defaults',default='native-player-projectile-compiled-defaults.json');p.add_argument('--encounter-room',default='RoomSimple01');a=p.parse_args();export(a.output,a.projectile_defaults,a.encounter_room)
