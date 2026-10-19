#!/usr/bin/env python3
"""Append original cue graphs from every selected animation definition class.

A selected action/actor bank is the admission boundary. Resolve the producer's
catalog identity and preserve each animation-name/field path as cue provenance;
export only missing prior-catalog events and retain every previous stream bank.
"""
import argparse,json,hashlib
from pathlib import Path
from audio_coverage import AudioCoverage,bank_index
from build_room_scene import identity
from export_audio_evidence import export as evidence_export
from export_audio_streams import export as stream_export
from export_audio_events import export as event_export

def export(a):
 if a.coverage.exists():raise ValueError('Fresh animation audio coverage required')
 cov=AudioCoverage(a.content);sources=[];counts={}
 for path in a.animation_manifest:
  manifest=json.loads(path.read_text());sources.append(identity(path))
  if manifest['schema']=='hades-action-effect-resources-v1':
   row=manifest['catalog'];catalog=Path(row['path'])
   if hashlib.sha256(catalog.read_bytes()).hexdigest()!=row['sha256']:raise ValueError('Animation definition catalog changed')
   sources.append(identity(catalog));records=json.loads(catalog.read_text())['Definitions']
  elif manifest['schema']=='hades-original-actor-animations-v1':records={clip['name']:clip['definition']for clip in manifest['clips']}
  else:raise ValueError('Expected selected original action or actor animation manifest')
  counts[str(path)]=len(records)
  for name,record in records.items():cov.scan(record,'SelectedAnimation.'+name+' ('+str(path)+')')
 studio={cue:cov.events[cue]for cue in cov.cues if cue in cov.events};owners,banks=bank_index(a.content,studio);cues={}
 for cue in sorted(cov.cues):
  candidates=[row for row in owners.get(cue,[])if row['complete']and row['waveforms']]
  if candidates:cues[cue]={'kind':'studio_event','bank':candidates[0]['bank'],'guid':studio[cue],'owners':owners[cue]}
  elif any(row['complete']for row in owners.get(cue,[])):cues[cue]={'kind':'no_waveform','bank':None,'guid':studio[cue],'owners':owners[cue]}
  else:cues[cue]={'kind':'unresolved','bank':None,'owners':owners.get(cue,[]),'reason':'Selected animation cue has no complete waveform Studio graph.'}
 report={'schema':'hades-audio-coverage-v1','scope':{'class':'SelectedAnimationDefinitions','catalog_records':counts,'boundary':'Every original cue string in resolved definitions of selected action and actor catalogs; original children/slide events included by producer definition closure.'},'cues':cues,'provenance':dict(cov.cues),'banks':banks,'sources':sources}
 a.coverage.parent.mkdir(parents=True,exist_ok=True);a.coverage.write_text(json.dumps(report,indent=2)+'\n')
 evidence_export(a.content,a.decoder,a.evidence,256*1024*1024,coverage=a.coverage,exclude=[a.base_manifest])
 stream_export(a.content,a.evidence/'manifest.json',a.decoder,a.streams)
 previous=json.loads(a.base_manifest.read_text())
 return event_export(a.content,Path(previous['stream_manifest']['path']),a.output,[*[Path(p['path'])for p in previous['additional_stream_manifests']],a.streams/'manifest.json'],bool(previous.get('mixing_control')),True)

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--animation-manifest',type=Path,action='append',required=True)
 for flag in ['content','decoder','base-manifest','coverage','evidence','streams','output']:p.add_argument('--'+flag,type=Path,required=True)
 print(json.dumps(export(p.parse_args())))
