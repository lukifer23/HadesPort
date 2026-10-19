#!/usr/bin/env python3
"""Export requested original programmer-voice samples; no playback or FMOD execution."""
import argparse,csv,io,json,re,subprocess
from pathlib import Path
from hades_audio import FsbBank,Bank,read_original,digest,guid_names
from export_room_creation import RangedSjson
from export_actor_catalog import literal
from export_audio_evidence import wav_metadata
ROOT=Path(__file__).resolve().parents[2]

def export(content,decoder,output,cues,max_pcm_bytes=32*1024*1024,coverage=None):
    content=content.resolve();decoder=decoder.resolve()
    if output.exists() or output.is_symlink():raise ValueError('Voice output must be new')
    output=output.resolve()
    if output.is_relative_to(content.parent):raise ValueError('Never write into preserved game')
    if decoder.parents[2].name!='vgmstream-db77f98f':raise ValueError('Use pinned private decoder')
    base=content/'Audio/FMOD/Build/Desktop';b=FsbBank(base/'VO.fsb')
    header,header_id=read_original(base/'VO.h')
    index={k.lower():int(v) for k,v in re.findall(rb'^#define\s+(\w+)\s+(\d+)\s*$',header,re.M)}
    index={k.decode():v for k,v in index.items()}
    if len(index)!=len(b.samples):raise ValueError('Original voice header/sample count mismatch')
    output.mkdir(parents=True);(output/'audio-pcm').mkdir();samples={};events={};runs=[];total=0;subtitles={}
    actor_raw,actor_id=read_original(content/'Game/Audio.sjson');actor_table=RangedSjson(actor_raw.decode('utf-8-sig')).parse()['Actors']
    declarations,_=guid_names(content/'Audio/FMOD/Build/GUIDs.txt');event_names={v:k[6:]for k,v in declarations.items()if k.startswith('event:')}
    caption_files=[]
    for path in sorted((content/'Subtitles/en').glob('*.csv')):
        data,proof=read_original(path);caption_files.append((proof,list(csv.reader(io.StringIO(data.decode('utf-8-sig'))))))
    coverage_identity=None;rejected={};template_banks={}
    if coverage is not None:
        # Derived voice set (audio_coverage.py): every in-scope VO.fsb cue.
        raw,coverage_identity=read_original(coverage);report=json.loads(raw)
        if report['schema']!='hades-audio-coverage-v1':raise ValueError('Expected audio coverage report')
        cues=list(cues)+[c for c,r in sorted(report['cues'].items())if r['kind']=='programmer_voice'and c not in cues]
    for cue in cues:
        if not cue.startswith('/VO/') or '/' in cue[4:]:raise ValueError('This bounded export accepts direct /VO/sample only')
        name=cue[4:].lower()
        if name not in index:raise ValueError('Requested original voice is not indexed')
        sample=b.samples[index[name]]
        if total+sample['sample_count']*sample['channels']*4>max_pcm_bytes:raise ValueError('Bounded voice PCM evidence exceeds bound')
        matches=[(proof,i+1,row[7])for proof,rows in caption_files for i,row in enumerate(rows)if len(row)>7 and row[3].lower()==name]
        if not matches and coverage is not None:
            # Dialogue lines carry their text in Lua data (Text=...); the
            # original subtitle lookup then finds no row and shows nothing.
            matches=[(None,None,'')]
        if len(matches)!=1:
            if coverage is None:raise ValueError('Requested voice requires unique original subtitle row')
            rejected[cue]=f'{len(matches)} original subtitle rows (unique row required)';continue
        actors=[a for a in actor_table if a['Name']!='Default'and a['Name'].lower()in name]
        if len(actors)>1:
            if coverage is None:raise ValueError('Ambiguous native actor hash traversal for voice')
            rejected[cue]='Ambiguous native GetActorEvent actor substring: '+','.join(a['Name']for a in actors);continue
        subtitles[cue]={'text':matches[0][2],'row':matches[0][1],'source':matches[0][0],'language':'en'}
        if sample['name'].lower()!=name:raise ValueError('Original VO.h index disagrees with FSB names')
        key=b.original['sha256']+':'+str(sample['global_subsong']);wav=output/'audio-pcm'/(digest(key.encode())+'.wav')
        argv=[str(decoder),'-i','-W','4','-s',str(sample['global_subsong']),'-o',str(wav),str(base/'VO.fsb')]
        run=subprocess.run(argv,capture_output=True,text=True,timeout=120)
        if run.returncode:raise ValueError('Original voice decode failed: '+run.stderr)
        pcm=wav_metadata(wav)
        if (pcm['channels'],pcm['sample_rate'],pcm['frames'])!=(sample['channels'],sample['sample_rate'],sample['sample_count']):raise ValueError('Original voice PCM header mismatch')
        total+=pcm['data']['bytes']
        if total>max_pcm_bytes:raise ValueError('Bounded voice PCM evidence exceeds bound')
        samples[key]={'original_bank':'VO.fsb','original':sample,'pcm':pcm,'path':'audio-pcm/'+wav.name}
        # Native GetActorEvent lowercases configured actor keys and matches a substring.
        # Multiple distinct matches need actual native hash traversal; do not guess.
        actors=[a for a in actor_table if a['Name']!='Default'and a['Name'].lower()in name]
        if len(actors)>1:raise ValueError('Ambiguous native actor hash traversal for voice')
        actor=actors[0]if actors else next(a for a in actor_table if a['Name']=='Default')
        guid=actor['GUID'].strip('{}').lower();template=event_names[guid]
        # The actor template's event body (EVTB) lives in a resident bank:
        # Sounds.bank for the hero/default template, MasterBank.bank for the
        # god/NPC templates (Athena Template ...). First resident owner wins.
        for n in ['Sounds.bank','MasterBank.bank']:
            if n not in template_banks:template_banks[n]=Bank(base/n)
        owner=next((n for n in ['Sounds.bank','MasterBank.bank']if template_banks[n].nodes.get(guid,{}).get('kind')=='EVTB'),None)
        if owner is None:raise ValueError('Voice template event body absent from resident banks: '+template)
        events[cue]={'event_guid':guid,'bank':owner,'sample_keys':[key],'programmer_sample':key,'programmer_template':template,'actor_source_key':actor['Name'],'scope':'VO.h exact index + unique original actor substring or configured Default, static native GetActorEvent49140'}
        runs.append({'argv':argv,'exit_code':run.returncode,'stdout':run.stdout,'stderr':run.stderr,'output':pcm['file']})
    catalog={'schema':'hades-native-audio-evidence-v1','profile':'original-requested-speech','events':events,'samples':samples,'banks':{'VO.fsb':{'original':b.original,'standalone_fsb':True}},'pcm_bytes':total,'playback_ready':False,'voice_header':header_id,'voice_namespace':index,'voice_subtitles':subtitles,'actor_definitions':actor_id,'rejected':rejected,'coverage':coverage_identity}
    tool=read_original(decoder)[1]
    manifest={'schema':'hades-native-audio-export-manifest-v1','catalog':catalog,'decoder':{'binary':tool},'decode_runs':runs,'source_scripts':[read_original(content/'Game/Audio.sjson')[1]],'limitations':['No original FMOD execution or hardware playback','Actor templates resolved only with unique matching source actor key; ambiguous native hash traversal is rejected','Debug PCM is private evidence; only unchanged encoded bytes are packaged']}
    (output/'audio-evidence.lua').write_text('return '+literal(catalog)+'\n');(output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    return {'events':len(events),'samples':len(samples),'pcm_bytes':total,'voice_namespace':len(index),'output':str(output)}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--content',type=Path,default=ROOT/'.local/game/base/Content');p.add_argument('--decoder',type=Path,default=ROOT/'.local/tools/vgmstream-db77f98f/build/cli/vgmstream-cli');p.add_argument('--output',type=Path,required=True);p.add_argument('--cue',action='append',default=[]);p.add_argument('--coverage',type=Path);p.add_argument('--max-pcm-bytes',type=int,default=32*1024*1024);a=p.parse_args()
    if not a.cue and a.coverage is None:p.error('--cue or --coverage required')
    print(json.dumps(export(a.content,a.decoder,a.output,a.cue,a.max_pcm_bytes,a.coverage),indent=2))
