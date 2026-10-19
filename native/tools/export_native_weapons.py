#!/usr/bin/env python3
"""Export source-bound first-weapon initialization; no firing or target execution."""
import argparse,hashlib,json,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
TARGET='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'

def digest(path):return hashlib.file_digest(path.open('rb'),'sha256').hexdigest()

def export(source,normalization,output):
    if output.exists():raise ValueError('Output must be new')
    data=json.loads(source.read_text());normalized=json.loads(normalization.read_text())
    if data['schema']!='hades-first-weapon-v1' or data['target_sha256']!=TARGET:raise ValueError('Unsupported weapon source identity/schema')
    if digest(ROOT/'.local/game/base/x64/EngineWin64s.dll')!=TARGET:raise ValueError('Original engine identity mismatch')
    for item in data['source_integrity']:
        relative=Path(item['path'])
        if relative.is_absolute() or '..' in relative.parts:raise ValueError('Invalid original weapon source path')
        if digest(ROOT/relative)!=item['sha256']:raise ValueError('Original weapon source changed')
    # The normalization contract must independently bind the same first weapon
    # and explain the original reader's preloaded Fire trigger.
    if normalized.get('schema')!='hades-first-weapon-effect-normalization-v1' or \
       normalized['target']['dll_sha256']!=TARGET or normalized['source_export']['sha256']!=digest(source) or \
       normalized['weapon_name']!='ArmorBreakAttack' or normalized['effectContainerResolved']is not True or \
       normalized['effectTrigger']!=1 or len(normalized['effects'])!=1 or \
       normalized['effects'][0]['name']!='ArmorBreakStun' or \
       normalized['effects'][0]['effective_fields']['Type']['value']!=12 or \
       normalized['effects'][0]['effective_fields']['Trigger']['value']!=1:
        raise ValueError('Unrecognized source effect normalization proof')
    if len(data['weapons'])!=1 or data['weapons'][0]['name']!='ArmorBreakAttack':raise ValueError('Only the recovered first weapon is supported')
    w=data['weapons'][0];fields=w['effective_fields']
    def field(name,typ):
        v=fields[name]
        if v['type']!=typ:raise ValueError('Unexpected native field type: '+name)
        return v['value']
    strings=[w['name'],field('Projectile','string'),w['effect']['name']]
    if strings!=['ArmorBreakAttack','null','ArmorBreakStun']:raise ValueError('First weapon definition identity mismatch')
    integers=[field('Type','uint8')]+[field(k,'int32')for k in ('StartingAmmo','MaxAmmo','ClipSize')]+[12,1]
    scalars=[field(k,'float32')for k in ('ChargeTime','ClipRegenInterval','InitialCooldown')]
    flags=[field(k,'bool')for k in ('ReloadOnCreation','ReloadOnEquip','DefaultControl')]+[True]
    controls=[field(k,'string')for k in ('Control','AlternateControl','SecondaryControl','DisallowedControl')]
    bundled=[field(k,'string')for k in ('SwapOnFire','AddOnFire','SwapOnRelease','CollisionWeapon','PostFireWeapon','MovingWeapon','CooldownWeapon')]
    if integers!=[0,-1,-1,0,12,1] or scalars!=[0,0,0] or flags!=[False,False,True,True] or any(controls+bundled):raise ValueError('Recovered initialization branch differs')
    unknown=[x['name']for x in w['unresolved_fields']if x['name']!='Effects']
    unknown.append('EffectData defaults and execution beyond normalized source fields')
    packet=bytearray(b'HWA1'+bytes.fromhex(digest(source))+bytes.fromhex(digest(normalization))+bytes.fromhex(TARGET)+struct.pack('<I',1))
    def text(value):
        b=value.encode('utf-8')
        if len(b)>65536 or b'\0'in b:raise ValueError('Invalid weapon string')
        packet.extend(struct.pack('<I',len(b)));packet.extend(b)
    for s in strings:text(s)
    packet.extend(struct.pack('<6i3f4B',*integers,*scalars,*flags))
    for s in controls+bundled:text(s)
    packet.extend(struct.pack('<I',len(unknown)))
    for s in unknown:text(s)
    output.mkdir(parents=True);path=output/'weapons.bin';path.write_bytes(packet)
    manifest={'schema':'hades-native-weapon-initialization-v1','target_sha256':TARGET,'source':{'path':str(source.resolve()),'sha256':digest(source)},'normalization':{'path':str(normalization.resolve()),'sha256':digest(normalization)},'packet':{'path':str(path.resolve()),'sha256':digest(path),'bytes':len(packet)},'weapons':[{'name':w['name'],'normalized_effect':{'name':'ArmorBreakStun','count':1,'type':12,'trigger':1},'unresolved_fields':unknown}],'limits':['Initialization profile only; no live map units, fire, projectiles, damage, effects or combat are supplied by this packet.','Source unknown complex fields remain explicit. Original source/export JSON unchanged.']}
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(manifest,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--normalization',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();export(a.source,a.normalization,a.output)
