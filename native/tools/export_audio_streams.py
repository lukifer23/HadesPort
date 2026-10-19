#!/usr/bin/env python3
"""Create private single-sample FSB5 containers with unchanged original Vorbis.

Copies the selected sample header/extra chunks and encoded sample range. Only
FSB count/section sizes, relative data offset and name table are rewritten.
Every derived container must decode PCM-identically to the prior full-bank
debug export before a runtime catalog is emitted. No audio playback.
"""
import argparse,hashlib,json,struct,subprocess,zlib
from pathlib import Path
from hades_audio import Bank,FsbBank,read_original,digest
from export_audio_evidence import wav_metadata
from export_actor_catalog import literal
def export(content,evidence,decoder,output):
    if output.exists()or output.is_symlink():raise ValueError('Streaming export must be new')
    content=content.resolve();output=output.resolve()
    if output.is_relative_to(content.parent):raise ValueError('Cannot export into preserved originals')
    evidence_raw,evidence_identity=read_original(evidence);manifest=json.loads(evidence_raw);catalog=manifest['catalog']
    if catalog['schema']!='hades-native-audio-evidence-v1':raise ValueError('Expected retained original decode evidence')
    decoder_identity=read_original(decoder)[1]
    if decoder_identity['sha256']!=manifest['decoder']['binary']['sha256']:raise ValueError('Decoder differs from full-bank PCM reference')
    output.mkdir(parents=True);assets=output/'audio-streams';assets.mkdir();verification=output/'verification';verification.mkdir();records={};runs=[];total=0
    for bankname,bank_record in catalog['banks'].items():
        b=(FsbBank if bankname.endswith('.fsb') else Bank)(content/'Audio/FMOD/Build/Desktop'/bankname)
        if b.original['sha256']!=bank_record['original']['sha256']:raise ValueError('Original bank differs from PCM reference')
        for key,sample in catalog['samples'].items():
            if sample['original_bank']!=bankname:continue
            original=next(x for x in b.samples if x['global_subsong']==sample['original']['global_subsong'])
            if original['encoded_sha256']!=sample['original']['encoded_sha256']:raise ValueError('Encoded original differs from reference')
            fsb=b.embedded_banks[original['sound_bank_index']];header=bytearray(b.raw[fsb['offset']:fsb['offset']+0x3c])
            start=original['sample_header_offset'];sample_header=bytearray(b.raw[start:start+original['sample_header_bytes']]);mode=struct.unpack_from('<Q',sample_header)[0];mode&=~(0x7ffffff<<7);struct.pack_into('<Q',sample_header,0,mode)
            name=original['name'].encode('utf-8')+b'\0';name_table=struct.pack('<I',4)+name
            encoded=b.raw[original['data_offset']:original['data_offset']+original['encoded_bytes']]
            struct.pack_into('<4I',header,8,1,len(sample_header),len(name_table),len(encoded))
            derived=bytes(header)+bytes(sample_header)+name_table+encoded;sha=digest(derived);path=assets/(sha+'.fsb')
            if path.exists():raise ValueError('Unexpected duplicate single-sample container')
            if total+len(derived)>128*1024*1024:raise ValueError('Opening encoded export exceeds128MiB bound')
            path.write_bytes(derived);total+=len(derived)
            wav=verification/(sha+'.wav');argv=[str(decoder),'-i','-W','4','-s','1','-o',str(wav),str(path)]
            run=subprocess.run(argv,capture_output=True,text=True,timeout=120)
            if run.returncode:raise ValueError('Derived container decode failed: '+run.stderr)
            pcm=wav_metadata(wav);reference=sample['pcm']
            if (pcm['format'],pcm['channels'],pcm['sample_rate'],pcm['frames'],pcm['data']['sha256'])!=(reference['format'],reference['channels'],reference['sample_rate'],reference['frames'],reference['data']['sha256']):raise ValueError('Derived container PCM differs from original bank decode')
            runs.append({'argv':argv,'exit_code':run.returncode,'stdout':run.stdout,'stderr':run.stderr,'decoded_wav_identity':pcm['file'],'decoded_pcm_sha256':pcm['data']['sha256'],'original_bank_pcm_sha256':reference['data']['sha256'],'temporary_wav_removed_after_verification':True});wav.unlink()
            records[key]={'asset':'audio-streams/'+path.name,'subsong':1,'channels':original['channels'],'sample_rate':original['sample_rate'],'frames':original['sample_count'],'encoded_file_bytes':len(derived),'sha256':sha,'crc32':zlib.crc32(derived),'original':original,'original_bank':b.original,'derived_container_changes':['FSB5 sample count=1','FSB5 section sizes reflect selected header/name/data','Sample-relative encoded data offset=0','Single-entry sample name table'],'unchanged':['Original encoded sample bytes including alignment padding','Original sample duration/channels/rate','Original sample extra chunks including Vorbis setup ID/seek table','Other original FSB5 base header fields']}
    out={'schema':'hades-native-audio-streams-v1','profile':catalog['profile'],'events':catalog['events'],'samples':records,'banks':catalog['banks'],'event_playback_ready':False,'encoded_bytes':total,'rejected':catalog.get('rejected',{})}
    if 'voice_namespace' in catalog:out.update(voice_namespace=catalog['voice_namespace'],voice_header=catalog['voice_header'],voice_subtitles=catalog['voice_subtitles'],actor_definitions=catalog['actor_definitions'])
    (output/'audio-streams.lua').write_text('return '+literal(out)+'\n')
    proof={'schema':'hades-native-audio-stream-export-v1','catalog':out,'pcm_reference':evidence_identity,'decoder':decoder_identity,'verification_runs':runs,'limits':['Derived FSB5 containers retain original encoded samples but are not byte-identical original files.','Decoder output proven PCM-identical to the same decoder reading the complete original bank.','No original FMOD runtime or audio output executed.','Event scheduling, parameters, spatialization and mixing are separate unresolved work.']}
    (output/'manifest.json').write_text(json.dumps(proof,indent=2)+'\n')
    return {'output':str(output),'streams':len(records),'encoded_bytes':total,'all_pcm_identical':True,'event_playback_ready':False}
def main():
    root=Path(__file__).resolve().parents[2];p=argparse.ArgumentParser(description=__doc__);p.add_argument('--content',type=Path,default=root/'.local/game/base/Content');p.add_argument('--pcm-evidence',type=Path,required=True);p.add_argument('--decoder',type=Path,default=root/'.local/tools/vgmstream-db77f98f/build/cli/vgmstream-cli');p.add_argument('--output',type=Path,required=True);a=p.parse_args();print(json.dumps(export(a.content,a.pcm_evidence,a.decoder,a.output),indent=2))
if __name__=='__main__':main()
