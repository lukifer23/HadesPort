#!/usr/bin/env python3
"""Source-bound actor definitions and map records for the native actor runtime."""
import argparse,hashlib,json,math,struct
from pathlib import Path
from export_room_creation import RangedSjson
from build_room_scene import merge
ROOT=Path(__file__).resolve().parents[2]
TARGET='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'
def identity(path):return {'path':str(path.resolve()),'sha256':hashlib.file_digest(path.open('rb'),'sha256').hexdigest(),'bytes':path.stat().st_size}
def literal(value):
    if value is None:return 'nil'
    if value is True:return 'true'
    if value is False:return 'false'
    if isinstance(value,(float,int)):
        if not math.isfinite(value):raise ValueError('Nonfinite source value')
        return repr(value)
    if isinstance(value,str):return json.dumps(value,ensure_ascii=False)
    if isinstance(value,list):return '{'+','.join(literal(v)for v in value)+'}'
    if isinstance(value,dict):return '{'+','.join('['+literal(k)+']='+literal(v)for k,v in value.items())+'}'
    raise ValueError('Unsupported source value')

def native_angle(value):
    # UnitData SGD_Deserialize VA1800f473b/478f uses SGD_DeserializeAngle
    # VA180205cf0: JSON number -> float32 -> double*pi/180 -> float32.
    # Compiled defaults and inherited native data are already radians; callers
    # invoke this only for a key present in resolved authored source data.
    number=value if isinstance(value,(int,float)) and not isinstance(value,bool) else 0
    rounded=struct.unpack('<f',struct.pack('<f',number))[0]
    return struct.unpack('<f',struct.pack('<f',rounded*float.fromhex('0x1.1df46a2529d39p-6')))[0]

def runtime_default(field):
    if field['type']=='sound_cue':
        # The native actor API consumes a cue name. A proven default cue has
        # null owner, empty name and mValid=false; preserve that empty name,
        # then real authored strings override it through ordinary inheritance.
        cue=field['value']
        if cue!={'owner':None,'name':'','valid':False}:raise ValueError('Uncovered compiled actor SoundCue default')
        return cue['name']
    return field['value']

def export(content,creation,defaults,output):
    if output.exists():raise ValueError('Output must be new')
    source=json.loads(creation.read_text());compiled=json.loads(defaults.read_text())
    if compiled['target_sha256']!=TARGET or identity(content.parent/'x64/EngineWin64s.dll')['sha256']!=TARGET:raise ValueError('Actor default target mismatch')
    map_source=Path(source['map_source']['path'])
    if identity(map_source)['sha256']!=source['map_source']['sha256']:raise ValueError('Original map changed')
    tables={};source_files=[]
    for category in ('Obstacles','Units'):
        table={}
        for path in sorted((content/'Game'/category).glob('*.sjson')):
            raw=path.read_bytes();text=raw.decode('utf-8');parser=RangedSjson(text)
            if text.startswith('\ufeff'):parser.pos=1
            parsed=parser.parse();proof=identity(path);source_files.append(proof)
            for definition in parsed.get(category,[]):
                name=definition['Name']
                if name in table:raise ValueError('Duplicate original actor definition '+name)
                table[name]={'source':proof,'range':parser.ranges[id(definition)],'raw':definition}
        tables[category]=table
    values={};proofs={}
    def resolve(category,name,ancestors=()):
        key=category+'/'+name
        if key in values:return values[key]
        if name in ancestors:raise ValueError('Actor inheritance cycle')
        own=tables[category][name];parent=own['raw'].get('InheritFrom');base={}
        for p in ([parent]if isinstance(parent,str)else parent or[]):base=merge(base,resolve(category,p,ancestors+(name,)))
        resolved=merge(base,own['raw']);values[key]=resolved;proofs[key]={'source':own['source'],'source_range':own['range'],'parent':parent};return resolved
    definitions={}
    for category,table in tables.items():
        native='Unit'if category=='Units'else'Obstacle'
        for name in table:
            key=category+'/'+name;raw=resolve(category,name)
            fields={n:runtime_default(v)for n,v in compiled['definitions'][native]['fields'].items()if v['type']!='null_pointer'}
            fields=merge(fields,raw)
            if category=='Units':
                for angular_speed in ('RotationSpeed','CosmeticRotationSpeed'):
                    if angular_speed in raw:fields[angular_speed]=native_angle(raw[angular_speed])
            fields['Thing']=merge({n:runtime_default(v)for n,v in compiled['definitions']['Thing']['fields'].items()if v['type']!='null_pointer'},raw.get('Thing',{}))
            if category=='Units'or fields.get('NeedsLife'):
                fields['Life']=merge({n:runtime_default(v)for n,v in compiled['definitions']['Life']['fields'].items()if v['type']!='null_pointer'},raw.get('Life',{}))
            definitions[key]={'key':key,'name':name,'kind':native,'properties':fields,'unresolved_compiled_fields':[f['name']for f in compiled['definitions'][native]['unknown']],'unresolved_thing_fields':[f['name']for f in compiled['definitions']['Thing']['unknown']]}
    rows=[]
    for record in source['records']:
        key=record['definition_key']
        if key not in definitions:raise ValueError('Map actor definition missing')
        rows.append({'id':record['id'],'definition':key,'map':record['raw_map']})
    data={'schema':'hades-source-actor-catalog-v1','room':source['room'],'target':TARGET,'native_unit_angles':True,'definitions':definitions,'map':rows,'property_types':{category:{key:field['type']for key,field in data['fields'].items()if field['type']in ('bool','float32','int32','string','null_pointer')and not field.get('enum')}for category,data in compiled['definitions'].items()}}
    output.mkdir(parents=True);packet=output/'actors.lua';packet.write_text('return '+literal(data)+'\n')
    manifest={'schema':data['schema'],'native_unit_angles':True,'angle_fields':['RotationSpeed','CosmeticRotationSpeed'],'room':source['room'],'target_sha256':TARGET,'creation':identity(creation),'defaults':identity(defaults),'source_files':source_files,'map_source':source['map_source'],'definition_provenance':proofs,'packet':identity(packet),'definitions':len(definitions),'map_records':len(rows),'limits':['Known compiled scalar/string/color defaults plus original resolved source properties. Other compiled complex defaults remain explicit.','This data catalog does not itself allocate, register, initialize or activate actors.']}
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps({k:manifest[k]for k in ('packet','definitions','map_records')},indent=2))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--content',type=Path,required=True);p.add_argument('--creation',type=Path,required=True);p.add_argument('--defaults',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();export(a.content,a.creation,a.defaults,a.output)
