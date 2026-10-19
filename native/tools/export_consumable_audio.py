#!/usr/bin/env python3
"""Append original ConsumeSound event graphs from the ConsumableData class.

ConsumableUsedPresentation reads this field dynamically. Resolve every original
record's inheritance, retain field/source provenance, and only decode events
absent from the selected prior catalog. No game Lua or original FMOD executes.
"""
import argparse,json
from pathlib import Path
from audio_coverage import AudioCoverage,bank_index
from build_room_scene import identity
from export_audio_evidence import export as evidence_export
from export_audio_streams import export as stream_export
from export_audio_events import export as event_export


def export(a):
 if a.coverage.exists():raise ValueError('Fresh consumable audio coverage required')
 cov=AudioCoverage(a.content);table=cov.lua_table('ConsumableData')
 provenance={}
 for name in sorted(table):
  record=cov.lua_record(table,name);cue=record.get('ConsumeSound')
  if not isinstance(cue,str):continue
  provenance.setdefault(cue,[]).append('ConsumableData.'+name+'.ConsumeSound (ProcessDataInheritance)')
 studio={c:cov.events[c]for c in provenance if c in cov.events}
 owners,banks=bank_index(a.content,studio);cues={}
 for cue in sorted(provenance):
  candidates=[o for o in owners.get(cue,[])if o['complete']and o['waveforms']]
  if candidates:cues[cue]={'kind':'studio_event','bank':candidates[0]['bank'],'guid':studio[cue],'owners':owners[cue]}
  elif any(o['complete']for o in owners.get(cue,[])):cues[cue]={'kind':'no_waveform','bank':None,'guid':studio[cue],'owners':owners[cue]}
  else:cues[cue]={'kind':'unresolved','bank':None,'owners':owners.get(cue,[]),'reason':'Original ConsumeSound has no complete waveform Studio event in GUID namespace.'}
 report={'schema':'hades-audio-coverage-v1','method':__doc__.strip(),
  'scope':{'class':'ConsumableData','records':len(table),'field':'ConsumeSound','consumer':'ConsumableUsedPresentation','boundary':'All literal ConsumeSound values after original Lua data inheritance; other consumable presentation fields excluded.'},
  'cues':cues,'provenance':provenance,'banks':banks,
  'sources':[identity(a.content/'Scripts'/p)for p in ['ConsumableData.lua','RoomPresentation.lua']]}
 a.coverage.parent.mkdir(parents=True,exist_ok=True);a.coverage.write_text(json.dumps(report,indent=2)+'\n')
 evidence_export(a.content,a.decoder,a.evidence,256*1024*1024,coverage=a.coverage,exclude=[a.base_manifest])
 stream_export(a.content,a.evidence/'manifest.json',a.decoder,a.streams)
 previous=json.loads(a.base_manifest.read_text())
 return event_export(a.content,Path(previous['stream_manifest']['path']),a.output,
  [*[Path(p['path'])for p in previous['additional_stream_manifests']],a.streams/'manifest.json'],
  bool(previous.get('mixing_control')),True)


if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for flag in ['content','decoder','base-manifest','coverage','evidence','streams','output']:p.add_argument('--'+flag,type=Path,required=True)
 print(json.dumps(export(p.parse_args())))
