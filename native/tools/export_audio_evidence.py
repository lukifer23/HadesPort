#!/usr/bin/env python3
"""Export selected original event graphs and decoded PCM to a NEW private path.

No playback or game/FMOD execution. The pinned open-source CLI decodes source
FSB5 streams. Selection/routing semantics are deliberately retained as evidence,
not advertised as a functioning FMOD event implementation.
"""
import argparse,json,struct,subprocess
from pathlib import Path
from hades_audio import Bank,read_original,guid_names,digest
from export_actor_catalog import literal
ROOT=Path(__file__).resolve().parents[2]
VGM_COMMIT='db77f98f405a45ad08df98aa444cae3848f95e6c'
PARSER_COMMIT='b7cfcfc60c93fcca7e4ffe666144e7ee3ed7ce77'
DEFAULT_CUES={
 'Ambience.bank':['/Leftovers/Object Ambiences/EvilLairAmbienceMatchSiteE'],
 'Music.bank':['/Music/MusicHadesReset_MC'],
 'Sounds.bank':['/SFX/Player Sounds/FootstepsHardSurface','/SFX/Player Sounds/FootstepsHardSurfaceRun'],
}
OPENING_CUES={
 'Ambience.bank':['/Leftovers/Object Ambiences/EvilLairAmbienceMatchSiteE'],
 'Music.bank':['/Music/MusicExploration4_MC'],
 'Sounds.bank':['/Leftovers/World Sounds/MapZoomInShortHigh','/SFX/Player Sounds/ZagreusWhooshDropIn'],
}
def wav_metadata(path):
    raw,identity=read_original(path)
    if raw[:4]!=b'RIFF' or raw[8:12]!=b'WAVE' or struct.unpack_from('<I',raw,4)[0]+8!=len(raw):raise ValueError('Decoder output RIFF mismatch')
    at=12;fmt=None;data=None
    while at+8<=len(raw):
        kind=raw[at:at+4];size=struct.unpack_from('<I',raw,at+4)[0];end=at+8+size
        if end>len(raw):raise ValueError('Decoder WAV exceeds file')
        if kind==b'fmt ':fmt=struct.unpack_from('<HHIIHH',raw,at+8)
        elif kind==b'data':data={'offset':at+8,'bytes':size,'sha256':digest(raw[at+8:end])}
        at=end+(size&1)
    if at!=len(raw) or not fmt or not data:raise ValueError('Incomplete decoder WAV')
    format_code,channels,rate,byterate,align,bits=fmt
    if (format_code,bits)!=(3,32) or align!=channels*4 or byterate!=rate*align or data['bytes']%align:raise ValueError('Expected IEEE float32 decoder WAV')
    return {'file':identity,'format':'ieee-float32-le','channels':channels,'sample_rate':rate,'frames':data['bytes']//align,'data':data}
def export(content,decoder,output,max_pcm_bytes,profile='biome-candidates',coverage=None,exclude=()):
    if output.exists()or output.is_symlink():raise ValueError('Audio output must be new')
    content=content.resolve();output=output.resolve();decoder=decoder.resolve()
    if output.is_relative_to(content.parent):raise ValueError('Output cannot be inside preserved original game')
    toolroot=decoder.parents[2]
    if toolroot.name!='vgmstream-db77f98f':raise ValueError('Select the pinned private decoder build')
    tool_sources=['src/meta/fsb5.c','src/meta/fsb5_fev.c','src/coding/vorbis_custom_decoder.c','COPYING','source.tar.gz']
    decoder_proof=read_original(decoder)[1]
    tools={'vgmstream_commit':VGM_COMMIT,'binary':decoder_proof,'sources':[read_original(toolroot/p)[1] for p in tool_sources],
      'reference_parser_commit':PARSER_COMMIT,'reference_scope':'Public source field-layout cross-check only; this exporter does not execute or vendor the C# parser.'}
    base=content/'Audio/FMOD/Build';names,names_proof=guid_names(base/'GUIDs.txt')
    output.mkdir(parents=True);(output/'audio-pcm').mkdir();events={};samples={};banks={};total=0;decode_evidence=[]
    selected_cues={'Ambience.bank':['/Leftovers/Ambience/MatchSiteIPoolAmbience']} if profile=='opening-room-ambience' else {'Sounds.bank':['/Leftovers/SFX/WingFlapMedium','/Leftovers/SFX/PowerUpFwoosh']} if profile=='movement-stop' else {'Sounds.bank':['/SFX/CeramicPotSmash']} if profile=='ceramic-pot' else {'Sounds.bank':['/Leftovers/World Sounds/MapText','/SFX/Menu Sounds/HadesTextDisappearFade']} if profile=='intro-text' else {'Sounds.bank':['/SFX/Player Sounds/FootstepsHardSurface','/SFX/Player Sounds/FootstepsHardSurfaceRun']} if profile=='player-footsteps' else {'Sounds.bank':['/SFX/Menu Sounds/MirrorMenuOpenREMEMBRANCE'],'Ambience.bank':['/Ambience/RemembranceScreenAmbience2']} if profile=='remembrance-screen' else {'Sounds.bank':['/SFX/Player Sounds/BurningFootsteps']} if profile=='burning-footsteps' else {'Sounds.bank':['/SFX/BatGroupLoop']} if profile=='room-bat' else OPENING_CUES if profile=='opening-source' else DEFAULT_CUES
    coverage_identity=None;rejected={}
    if coverage is not None:
        # Derived cue set (audio_coverage.py): every studio cue of the report,
        # minus cues an existing catalog already exports unchanged.
        raw,coverage_identity=read_original(coverage);report=json.loads(raw)
        if report['schema']!='hades-audio-coverage-v1':raise ValueError('Expected audio coverage report')
        done=set()
        for path in exclude:done.update(json.loads(read_original(path)[0])['events'])
        selected_cues={}
        for cue,r in sorted(report['cues'].items()):
            if r['kind']=='studio_event' and cue not in done:selected_cues.setdefault(r['bank'],[]).append(cue)
    for filename,cues in selected_cues.items():
        bank=Bank(base/'Desktop'/filename);selected={}
        for cue in cues:
            guid=names['event:'+cue]
            if guid not in bank.nodes:raise ValueError('Declared cue absent from selected bank')
            resolved=bank.resolve(guid)
            if resolved['missing_nodes'] or not resolved['samples']:
                if coverage is None:raise ValueError('Incomplete selected event graph')
                rejected[cue]='Incomplete original event graph in '+filename;continue
            event_samples=[]
            for sample in resolved['samples']:
                key=bank.original['sha256']+':'+str(sample['global_subsong']);event_samples.append(key)
                if key in samples:continue
                expected_bytes=sample['sample_count']*sample['channels']*4
                if total+expected_bytes>max_pcm_bytes:raise ValueError('Requested PCM export exceeds explicit bound')
                stem=digest(key.encode());wav=output/'audio-pcm'/(stem+'.wav')
                argv=[str(decoder),'-i','-W','4','-s',str(sample['global_subsong']),'-o',str(wav),str(base/'Desktop'/filename)]
                run=subprocess.run(argv,capture_output=True,text=True,timeout=120,check=False)
                result={'argv':argv,'exit_code':run.returncode,'stdout':run.stdout,'stderr':run.stderr}
                if run.returncode:raise ValueError('Pinned decoder failed: '+run.stderr)
                pcm=wav_metadata(wav)
                if (pcm['channels'],pcm['sample_rate'],pcm['frames'])!=(sample['channels'],sample['sample_rate'],sample['sample_count']):raise ValueError('Decoded PCM does not match original sample header')
                result['output']=pcm['file'];decode_evidence.append(result);total+=pcm['data']['bytes']
                samples[key]={'original_bank':filename,'original':sample,'pcm':pcm,'path':'audio-pcm/'+wav.name}
            events[cue]={'event_guid':guid,'bank':filename,'sample_keys':event_samples,'reachable_nodes':resolved['reachable_nodes'],'scope':resolved['scope']}
            for guid in resolved['reachable_nodes']:selected[guid]=bank.nodes[guid]
        banks[filename]={'original':bank.original,'version':bank.version,'subversion':bank.subversion,'embedded_banks':bank.embedded_banks,'selected_nodes':selected,'total_embedded_samples':len(bank.samples)}
    if coverage is not None:profile='coverage'
    catalog={'schema':'hades-native-audio-evidence-v1','profile':profile,'events':events,'rejected':rejected,'coverage':coverage_identity,'samples':samples,'banks':banks,
       'playback_ready':False,'pcm_bytes':total,'limitations':['Cue candidates are source-backed, not an observed PlaySound request trace.','Graph reachability is exact for supported typed edges; it does not select playlist items or implement parameter/timeline scheduling.','No bus gains, modulation, spatial attenuation, occlusion, sends, DSP, stop/fade behavior or FMOD callbacks are implemented.','Music event has multiple reachable stems, not one ready-made full mix.','Original compressed banks and waveform range hashes retained; PCM is float32 output from the pinned decoder, not original uncompressed master audio.']}
    (output/'audio-evidence.lua').write_text('return '+literal(catalog)+'\n')
    manifest={'schema':'hades-native-audio-export-manifest-v1','catalog':catalog,'guid_declarations':names_proof,'decoder':tools,'decode_runs':decode_evidence,
      'source_scripts':[read_original(content/'Scripts'/p)[1]for p in ['AudioData.lua','AudioScripts.lua','RoomDataTartarus.lua','RoomPresentation.lua']],
      'reference_links':['https://github.com/vgmstream/vgmstream/blob/'+VGM_COMMIT+'/src/meta/fsb5.c','https://github.com/vgmstream/vgmstream/blob/'+VGM_COMMIT+'/src/meta/fsb5_fev.c','https://github.com/Masusder/FModBankParser/tree/'+PARSER_COMMIT]}
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    return {'output':str(output),'events':len(events),'decoded_samples':len(samples),'pcm_bytes':total,'playback_ready':False}
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--content',type=Path,default=ROOT/'.local/game/base/Content');p.add_argument('--decoder',type=Path,default=ROOT/'.local/tools/vgmstream-db77f98f/build/cli/vgmstream-cli');p.add_argument('--output',type=Path,required=True);p.add_argument('--profile',choices=['biome-candidates','opening-source','room-bat','burning-footsteps','remembrance-screen','player-footsteps','intro-text','ceramic-pot','movement-stop','opening-room-ambience'],default='biome-candidates');p.add_argument('--max-pcm-bytes',type=int,default=768*1024*1024)
    p.add_argument('--coverage',type=Path,help='audio_coverage.py report: export its derived studio cue set');p.add_argument('--exclude-catalog',type=Path,action='append',default=[],help='existing audio-events manifest whose events are already exported');a=p.parse_args()
    print(json.dumps(export(a.content,a.decoder,a.output,a.max_pcm_bytes,a.profile,a.coverage,a.exclude_catalog),indent=2))
if __name__=='__main__':main()
