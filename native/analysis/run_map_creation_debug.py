#!/usr/bin/env python3
"""Debug the pure creation planner against selected original decoded metadata."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--creation',type=Path,default=ROOT/'.local/native-assets/opening-creation-v2/creation.json');args=p.parse_args()
source=args.creation.resolve();metadata=json.loads(source.read_text());out=ROOT/'.local/reports'
records=out/'native-map-creation-smoke.records'
with records.open('w')as file:
    file.write(str(len(metadata['records']))+'\n')
    for record in metadata['records']:
        raw=record['raw_map'];player=record['player_controlled']
        values=[record['id'],json.dumps(record['name']),int(raw['Active']),int(raw['DoCreate']),int(raw['ActivateAtRange']),raw['ActivationRange'],raw['Location']['X'],raw['Location']['Y'],raw['AttachToID'],record['data_type'],-1 if player is None else int(player),int(record['trigger_on_spawn']['value']),len(raw['AttachedIDs']),*raw['AttachedIDs']]
        file.write(' '.join(map(str,values))+'\n')
exe=out/'native-map-creation-smoke'
command=['clang++','-std=c++20','-Wall','-Wextra','-fsanitize=address,undefined',str(ROOT/'native/analysis/map_creation_smoke.cpp'),str(ROOT/'native/app/map_creation.cpp'),'-o',str(exe)]
subprocess.run(command,check=True)
with(out/'native-map-creation-smoke.log').open('w')as log:subprocess.run([str(exe),str(records)],stdout=log,check=True)
sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
report={'source':str(source),'sha256':sha(source),'map_source':metadata['map_source'],'counts':metadata['counts'],'compile_command':command,'result':'pass','sanitizers':['address','undefined'],'log':'native-map-creation-smoke.log','source_sha256':{str(path.relative_to(ROOT)):sha(path)for path in [ROOT/'native/app/map_creation.hpp',ROOT/'native/app/map_creation.cpp',ROOT/'native/analysis/map_creation_smoke.cpp']},'limits':['Pure planning/debug only; no live entities, Lua callbacks, physics, allocation or native hash enumeration is executed.']}
(out/'native-map-creation-smoke.json').write_text(json.dumps(report,indent=2)+'\n')
print('Original RoomOpening1037-record map creation planner debug passed (ASan/UBSan).')
