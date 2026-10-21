#!/usr/bin/env python3
"""Export original opening audio event graphs and source DSP topology, read-only.

Runtime DSP/resampler adaptations are recorded separately; none of this metadata
claims original FMOD execution. No game files are executed or edited.
"""
import argparse,json,sys,uuid,struct
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'analysis'))
from inspect_audio_event import audit,Reader,bus_body,prop,control,curve,finished
import inspect_audio_event
from functools import lru_cache
# audit() reparses its bank per event; derived catalogs hold hundreds of events
# per bank, so share one read-only parse per original bank path.
_bank_cache=lru_cache(maxsize=None)(inspect_audio_event.Bank)
inspect_audio_event.Bank=lambda path:_bank_cache(Path(path).resolve())
from hades_audio import Bank,read_original,guid_names,digest,guid_list
from export_actor_catalog import literal
from audit_audio_namespace import audit as audit_namespace
ZERO='00000000-0000-0000-0000-000000000000'
class Graph:
 def __init__(self,bank):
  self.b=bank;self.own={};self.parents={};self.modulators={};self.props={}
  for ch in bank.chunks:
   # PROP children by every parent-path prefix (chunk order kept).
   if ch['kind']=='PROP':
    for n in range(len(ch['parents'])+1):self.props.setdefault(ch['parents'][:n],[]).append(ch)
   self.parents.setdefault(ch['parents'],[]).append(ch)
   if ch['kind']in ['GBSB','IBSB','MBSB','RBSB','BEFB','SEFB','SCEF','CTRL','CURV','MAP ']:
    self.own[(ch['kind'],str(uuid.UUID(bytes_le=ch['payload'][:16])))]=ch
   if ch['kind']=='MODB':
    r=Reader(ch['payload']);size=r.u16();guid=r.guid();owner=r.guid();index=r.i32();typ=r.i32();property_type=r.i32()
    if typ==1:
     amount=r.f();finished(r);self.modulators[guid]={'guid':guid,'owner':owner,'property_index':index,'type':typ,'property_type':property_type,'amount':amount,'source':self.source(ch)}
 def get(self,g,kinds):
  matches=[self.own[(k,g)]for k in kinds if(k,g)in self.own]
  if len(matches)!=1:raise ValueError('Missing/ambiguous original graph node '+g)
  return matches[0]
 def mapping(self,g):
  # Property MappingModel (MAP chunk): [x,y] pairs, evaluated by fmodstudio.dll
  # 0x1800d1fb0 (forward, clamped piecewise linear; inverse at 0x1800d1f20).
  ch=self.get(g,['MAP ']);q=Reader(ch['payload']);q.guid();points=q.array(lambda x:[x.f(),x.f()]);finished(q)
  if not points:raise ValueError('Empty source property mapping '+g)
  return points
 def source(self,ch):return {'bank_sha256':self.b.original['sha256'],'kind':ch['kind'],'offset':ch['offset'],'bytes':ch['bytes'],'sha256':digest(ch['payload'])}
 def effect(self,g):
  ch=self.get(g,['BEFB','SEFB','SCEF']);r=Reader(ch['payload']);r.guid();out={'guid':g,'kind':ch['kind'],'source':self.source(ch)}
  if ch['kind']=='BEFB':
   out.update(type=r.u32(),legacy_bypass=bool(r.take(1)[0]));ps=[c for c in self.parents[ch['parents']]if c['kind']=='PMEF']
   if len(ps)!=1:raise ValueError('Missing exact DSP parameters')
   q=Reader(ps[0]['payload']);out['parameters']=[];out['parameter_types']=[]
   for _ in range(q.i32()):
    typ=q.i32();value=q.f()if typ==0 else q.i32()if typ==1 else bool(q.take(1)[0])if typ==2 else q.take(q.x16()).hex()if typ==3 else None
    if value is None:raise ValueError('Unknown original DSP parameter encoding')
    out['parameters'].append(value);out['parameter_types'].append(typ)
   out['sidechain_enabled']=bool(q.take(1)[0]);finished(q);out['parameter_source']=self.source(ps[0])
  elif ch['kind']=='SEFB':out.update(return_guid=r.guid(),level_db=r.f(),legacy_bypass=bool(r.take(1)[0]))
  else:out.update(active=bool(r.take(1)[0]),targets=guid_list(r),modulators=guid_list(r))
  finished(r)
  # Preserve generic wet/dry/other fields even when Android uses an adaptation.
  out['effect_body']=[{'source':self.source(c),'raw_hex':c['payload'].hex()}for c in self.parents[ch['parents']]if c['kind']=='EFCT']
  return out
 def bus(self,g):
  ch=self.get(g,['GBSB','IBSB','MBSB','RBSB']);r=Reader(ch['payload']);r.guid();route=r.routable();extra=r.take(len(r.raw)-r.pos).hex()
  bodies=[c for c in self.parents[ch['parents']]if c['kind']=='BUS ']
  if len(bodies)!=1:raise ValueError('Missing exact source bus body')
  v=bus_body(bodies[0]);v.update(guid=g,kind=ch['kind'],destination=route['bus_guid'],routable=route,extra_hex=extra,source=self.source(ch),properties=[])
  for p in self.props.get(ch['parents'],[]):
   if True:
    value=prop(p);v['properties'].append({'value':value,'source':self.source(p),'modulators':[self.modulators[g]for g in value['modulators']if g in self.modulators]})
  return v

def export(content,stream_manifest,output,extra_streams=(),mixing_control=False,tolerate=False):
 if output.exists()or output.is_symlink():raise ValueError('Audio event output must be new')
 content=content.resolve();output=output.resolve()
 if output.is_relative_to(content.parent):raise ValueError('Never export into originals')
 from animation_audio_binding import from_stream
 animation_closures=[closure for path in [stream_manifest,*extra_streams]if(closure:=from_stream(path))is not None]
 raw,stream_id=read_original(stream_manifest);streams=json.loads(raw)['catalog'];additional=[];unsupported=dict(streams.get('rejected',{}))
 for path in extra_streams:
  raw,identity=read_original(path);other=json.loads(raw)['catalog'];additional.append(identity);unsupported.update(other.get('rejected',{}))
  if other['schema']!=streams['schema']:raise ValueError('Mismatched compressed stream catalog')
  for key in ['samples','events']:
   for name,value in other[key].items():
    if name in streams[key]and streams[key][name]!=value:raise ValueError('Conflicting original compressed catalog entry')
    streams[key][name]=value
  if 'voice_namespace' in other:
   for key in ['voice_namespace','voice_header','actor_definitions']:
    if key in streams and streams[key]!=other[key]:raise ValueError('Conflicting original voice identity '+key)
    streams[key]=other[key]
   subtitles=streams.setdefault('voice_subtitles',{})
   for name,value in other['voice_subtitles'].items():
    if name in subtitles and subtitles[name]!=value:raise ValueError('Conflicting original subtitle '+name)
    subtitles[name]=value
 names,guids_id=guid_names(content/'Audio/FMOD/Build/GUIDs.txt')
 master=Graph(Bank(content/'Audio/FMOD/Build/Desktop/MasterBank.bank'));graphs={};events={};buses={};effects={};allcurves={};pending=[];sources={}
 for name,ev in streams['events'].items():
  try:
   bankname=ev['bank']
   # Construct each bank graph once (setdefault would rebuild it per event).
   if bankname not in graphs:graphs[bankname]=Graph(_bank_cache((content/'Audio/FMOD/Build/Desktop'/bankname).resolve()))
   graph=graphs[bankname];sources[bankname]=graph.b.original
   detail=audit(content/'Audio/FMOD/Build/Desktop'/bankname,ev.get('event_guid',names.get('event:'+name)));meta=detail['selected_metadata'];source_event=detail['event'];tl=meta[source_event['timeline_guid']]['value'];markers={x['guid']:x for x in tl['named_markers']};params={};parameters=[]
   for n in meta.values():
    if n['kind']=='PRMB':v=n['value'];params[v['guid']]=v['name'];parameters.append({'name':v['name'],'minimum':v['minimum'],'maximum':v['maximum'],'initial':v['default_value']})
   transitions=[]
   for tr in tl['transition_regions']:
    if tr['start']!=tr['end']or tr['quantization']!={'unit':0,'multiplier':0} or tr['chance_percent']!=100:raise ValueError('Unimplemented nonpoint/quantized/chance transition in selected source')
    evs=tr['evaluators'];out={'tick':tr['start'],'destination':markers[tr['destination_guid']]['position'],'conditional':bool(evs)}
    if evs:
     if len(evs)!=2 or evs[0]['raw_type']!=17 or evs[1]['raw_type']!=20512:raise ValueError('Unmapped exact parameter evaluator')
     out.update(parameter=params[evs[0]['value']],minimum=evs[1]['value'][0],maximum=evs[1]['value'][1])
    transitions.append(out)
   # WAIB and MUIB share the native Instrument gain automation. Preserve the
   # actual controller owner/property mapping for either resource shape.
   def instrument_fades(node):
    fades=[]
    for p in node['properties']:
     pv=p['property']['value']
     for c in p['controllers']:
      cv=c['curve']['value'];owner=c['owners']
      if [(o['owner_kind'],o['owner_guid'])for o in owner]!=[('TLNB',source_event['timeline_guid'])]:raise ValueError('Unsupported source instrument automation owner')
      if (pv['index'],pv['method'],pv['type'])!=(4,4,0) or graph.mapping(pv['mapping_guid'])!=[[0.0,0.0],[1.0,1.0]]:raise ValueError('Unsupported source instrument automation property')
      fades.append({'property':pv['index'],'points':cv['points'],'owners':owner})
    return fades
   instruments=[];locked_playlists=[]
   for region in tl['time_locked_trigger_boxes']:
    n=meta[region['guid']]
    if n['kind']=='MUIB':locked_playlists.append(region);continue
    if n['kind']!='WAIB'or len(n['instruments'])!=1:raise ValueError('Selected source is not a single waveform instrument')
    v=n['instruments'][0]['value'];wave=meta[n['value']['waveform_guid']]['value'];sample=next((k for k,s in streams['samples'].items()if s['original_bank']['sha256']==graph.b.original['sha256']and s['original']['sound_bank_index']==wave['sound_bank_index']and s['original']['subsound_index']==wave['subsound_index']),None)
    if sample is None:raise ValueError('No original compressed stream for selected event '+name)
    fades=instrument_fades(n)
    instruments.append({'guid':region['guid'],'sample':sample,'bus':v['routable']['bus_guid'],'start':region['start'],'end':region['start']+region['length'],'gain_db':v['volume'],'pitch_semitones':v['pitch'],'loop_count':v['loop_count'],'left_trim_tick':v['left_trim_offset'],'fades':fades,'original':v})
   playlists=[]
   for region,time_locked in [(r,False)for r in tl['trigger_boxes']]+[(r,True)for r in locked_playlists]:
    n=meta[region['guid']]
    if n['kind']=='PRIB':
     if 'programmer_sample' not in ev or len(n['instruments'])!=1 or n['raw_hex'][32:]!='0000':raise ValueError('Unresolved source programmer sound')
     v=n['instruments'][0]['value'];sample=ev['programmer_sample'];z=streams['samples'][sample]
     if v['loop_count']!=0 or v['left_trim_offset'] or v['trigger_delay_min'] or v['trigger_delay_max'] or v['evaluators'] or n['properties'] or region['start']!=0:raise ValueError('Unsupported source programmer instrument')
     sources['VO.fsb']=z['original_bank']
     duration=(z['frames']*48000+z['sample_rate']-1)//z['sample_rate']
     instruments.append({'guid':region['guid'],'sample':sample,'bus':v['routable']['bus_guid'],'start':0,'end':duration,'gain_db':v['volume'],'pitch_semitones':v['pitch'],'loop_count':0,'left_trim_tick':0,'fades':[],'original':v,'programmer_template':ev['programmer_template'],'source_trigger_length':region['length']})
     continue
    if n['kind']!='MUIB' or len(n['instruments'])!=1:raise ValueError('Unsupported source triggered instrument')
    if time_locked and(tl['transition_regions']or tl['sustain_points']or region['length']<=0):raise ValueError('Unsupported re-entrant/empty time-locked playlist region')
    # A triggered (async) instrument starts when the playhead enters its
    # region and then plays to its own end. Without transitions or sustain
    # points the playhead enters a later region exactly once, at its start.
    if region['start']!=0 and(tl['transition_regions']or tl['sustain_points']):raise ValueError('Unsupported re-entrant triggered instrument region')
    parent=n['instruments'][0]['value'];playlist=n['value']
    if (playlist['playlist_mode'],playlist['playlist_selection'])!=(2,1):raise ValueError('Unsupported source playlist mode')
    if parent['loop_count']!=0 or parent['trigger_chance_percent']!=100 or parent['trigger_delay_min'] or parent['trigger_delay_max'] or parent['evaluators']:raise ValueError('Unsupported triggered playlist admission')
    def locked_instrument_admitted(v):
     return v['flags']==0 and v['quantization']=={'unit':0,'multiplier':0} and v['control_parameter_guid']==ZERO and not v['initial_seek_position'] and not v['initial_seek_percent'] and not v['offset_3d_distance'] and v['maximum_polyphony']==2147483647
    if time_locked and(not locked_instrument_admitted(parent)or any(p['property']['value']['modulators']for p in n['properties'])):raise ValueError('Unsupported time-locked playlist instrument/automation admission')
    choices=[]
    for entry in playlist['playlist']:
     child=meta[entry['guid']]
     if child['kind']!='WAIB' or len(child['instruments'])!=1 or child['properties']:raise ValueError('Unsupported nested playlist waveform')
     v=child['instruments'][0]['value']
     if child['value']['waveform_guid']==ZERO:
      # The source bank keeps a weighted playlist entry whose waveform is
      # absent (no audio data exists). Selection still consumes its weight and
      # shuffle slot; the runtime plays nothing for it. Same admission rules.
      if v['loop_count']!=0 or v['routable']['bus_guid']!=ZERO or v['timeline_guid']!=ZERO or v['trigger_chance_percent']!=100 or v['trigger_delay_min'] or v['trigger_delay_max'] or v['evaluators']:raise ValueError('Unsupported empty playlist waveform semantics')
      if time_locked and not locked_instrument_admitted(v):raise ValueError('Unsupported time-locked empty playlist waveform admission')
      choices.append({'guid':entry['guid'],'waveform_guid':ZERO,'empty_waveform':True,'weight':entry['weight'],'original':v});continue
     wave=meta[child['value']['waveform_guid']]['value']
     sample=next((k for k,z in streams['samples'].items()if z['original_bank']['sha256']==graph.b.original['sha256']and z['original']['sound_bank_index']==wave['sound_bank_index']and z['original']['subsound_index']==wave['subsound_index']),None)
     if sample is None:raise ValueError('No original playlist compressed sample')
     source=streams['samples'][sample]
     if v['loop_count']!=0 or v['routable']['bus_guid']!=ZERO or v['trigger_chance_percent']!=100 or v['trigger_delay_min'] or v['trigger_delay_max'] or v['evaluators']:raise ValueError('Unsupported playlist waveform semantics')
     if time_locked and not locked_instrument_admitted(v):raise ValueError('Unsupported time-locked playlist waveform admission')
     choices.append({'guid':entry['guid'],'waveform_guid':child['value']['waveform_guid'],'sample':sample,'weight':entry['weight'],'gain_db':parent['volume']+v['volume'],'pitch_semitones':parent['pitch']+v['pitch'],'left_trim_tick':parent['left_trim_offset']+v['left_trim_offset'],'frames':source['frames'],'sample_rate':source['sample_rate'],'original':v})
    playlists.append({'guid':region['guid'],'start':region['start'],'source_trigger_length':region['length'],'time_locked':time_locked,'fades':instrument_fades(n),'mode':playlist['playlist_mode'],'selection':playlist['playlist_selection'],'bus':parent['routable']['bus_guid'],'choices':choices,'original':parent,'android_selection_adaptation':'weighted shuffle without replacement per cue; audio-only deterministic generator independent of game RNG; proprietary FMOD smart-random history not claimed'})
   for g,n in meta.items():
    if n['kind']in ['IBSB','GBSB','MBSB','RBSB']:
     bus=graph.bus(g);pending.append((graph,g))
     if g==source_event['master_track_guid']:bus['destination']=source_event['input_bus_guid']
     bus['automation']=[]
     for p in n['properties']:
      pv=p['property']['value']
      for c in p['controllers']:
       owners=c['owners'];parameter=next((params[o['parameter_guid']]for o in owners if o.get('parameter_guid')in params),None)
       # Controller owner: a parameter layout (PMLB) or this event's timeline
       # (TLNB; curve x = timeline tick). Values pass through the property
       # MappingModel and combine per ConflictResolver method (fmodstudio.dll
       # PropertyInstance::refreshValue 0x180014940, resolveValue 0x180014fc0).
       kinds=[(o['owner_kind'],o['owner_guid'])for o in owners]
       if parameter is not None and len(owners)==1 and kinds[0][0]=='PMLB':owner='parameter'
       elif kinds==[('TLNB',source_event['timeline_guid'])]:owner='timeline'
       else:raise ValueError('Unmapped bus automation owner '+repr(kinds))
       bus['automation'].append({'property':pv['index'],'parameter':parameter,'owner':owner,'method':pv['method'],'type':pv['type'],'mapping':graph.mapping(pv['mapping_guid']),'points':c['curve']['value']['points']})
     buses[g]=bus
   end=max([x['end']for x in instruments]+[x['start']+x['source_trigger_length']for x in playlists if x['time_locked']]+[x['position']+x['length']for x in tl['named_markers']]+[1])
   events[name]={'guid':detail['event_guid'],'bank':bankname,'master_bus':source_event['master_track_guid'],'input_bus':source_event['input_bus_guid'],'timeline':{'end':end,'parameters':parameters,'markers':[{'tick':m['position'],'length':m['length'],'name':m['name']}for m in tl['named_markers']],'transitions':transitions},'instruments':instruments,'playlists':playlists,'source_bank':graph.b.original}
  except (ValueError,KeyError,StopIteration) as e:
   # Derived (coverage) catalogs keep every in-scope cue: a source graph
   # feature the Android event player does not reproduce is recorded with
   # its exact reason, and the runtime reports it when a script plays it.
   if not tolerate:raise
   unsupported[name]=type(e).__name__+': '+str(e)
 snapshot=None
 if mixing_control:
  from audit_audio_snapshot import audit as audit_snapshot
  snapshot=audit_snapshot(content)
  name='/Control/MixingControl';bankname='Sounds.bank';graph=graphs.setdefault(bankname,Graph(Bank(content/'Audio/FMOD/Build/Desktop'/bankname)));sources[bankname]=graph.b.original
  detail=audit(content/'Audio/FMOD/Build/Desktop'/bankname,names['event:'+name]);meta=detail['selected_metadata'];event=detail['event'];tl=meta[event['timeline_guid']]['value']
  if tl['time_locked_trigger_boxes']or len(tl['trigger_boxes'])!=1 or tl['trigger_boxes'][0]['start']!=0:raise ValueError('MixingControl trigger changed')
  trigger=meta[tl['trigger_boxes'][0]['guid']]
  if trigger['kind']!='EVIB'or trigger['value']['links']!=[snapshot['snapshot_event']]:raise ValueError('MixingControl does not start actual Primary snapshot')
  parameters=[{'name':n['value']['name'],'minimum':n['value']['minimum'],'maximum':n['value']['maximum'],'initial':n['value']['default_value']}for n in meta.values()if n['kind']=='PRMB']
  markers={m['guid']:m for m in tl['named_markers']};transitions=[]
  for tr in tl['transition_regions']:
   if tr['start']!=tr['end']or tr['evaluators']or tr['quantization']!={'unit':0,'multiplier':0}or tr['chance_percent']!=100:raise ValueError('Unreviewed MixingControl transition')
   transitions.append({'tick':tr['start'],'destination':markers[tr['destination_guid']]['position'],'conditional':False})
  if transitions!=[{'tick':956332,'destination':0,'conditional':False}]:raise ValueError('MixingControl source loop changed')
  for g in [event['input_bus_guid'],event['master_track_guid']]+event['nonmaster_tracks']:
   bus=graph.bus(g)
   if g==event['master_track_guid']:bus['destination']=event['input_bus_guid']
   buses[g]=bus;pending.append((graph,g))
  events[name]={'guid':detail['event_guid'],'bank':bankname,'master_bus':event['master_track_guid'],'input_bus':event['input_bus_guid'],'role':'mixing_control','timeline':{'end':956332,'parameters':parameters,'markers':[],'transitions':transitions},'instruments':[],'playlists':[],'source_bank':graph.b.original,'primary_snapshot_trigger':tl['trigger_boxes'][0]}
  # A source snapshot target can be inaudible until its bus is admitted; retain
  # and admit the complete bounded five-target Reverb closure now.
  for binding in snapshot['bindings']:
   guid=binding['target_guid'];kind=binding['target_kind']
   if kind in ['GBSB','RBSB','IBSB','MBSB']:buses.setdefault(guid,master.bus(guid));pending.append((master,guid));continue
   owners=[]
   for (typ,g),ch in master.own.items():
    if typ not in ['GBSB','RBSB','IBSB','MBSB']:continue
    # This build stores effect bodies in project-wide lists. Ownership is
    # the bus's explicit pre/post effect GUID list, not lexical ancestry.
    bus=master.bus(g)
    if guid in bus['pre_fader_effects']+bus['post_fader_effects']:owners.append((g,bus))
   if len(owners)!=1:raise ValueError('Missing/ambiguous source snapshot effect owner')
   g,bus=owners[0];buses.setdefault(g,bus);pending.append((master,g))
 seen=set()
 while pending:
  graph,g=pending.pop()
  if g in seen:continue
  seen.add(g);bus=buses.setdefault(g,graph.bus(g))
  dst=bus['destination']
  if dst!=ZERO and dst not in buses:
   target=graph if any((kind,dst)in graph.own for kind in ['GBSB','IBSB','MBSB','RBSB'])else master
   buses[dst]=target.bus(dst);pending.append((target,dst))
  for fx in bus['pre_fader_effects']+bus['post_fader_effects']:
   effect=graph.effect(fx);effects[fx]=effect
   if effect['kind']=='SEFB':
    rg=effect['return_guid'];target=graph if('RBSB',rg)in graph.own else master
    if rg not in buses:buses[rg]=target.bus(rg);pending.append((target,rg))
 if snapshot is not None:
  applied=[];skipped=[]
  reverb_keys={b['key']for b in snapshot['bindings']}
  for entry in snapshot['base_entries']:
   if entry['controlled']and entry['key']not in reverb_keys:
    skipped.append(entry);continue
   guid=entry['target_guid'];index=entry['target_index'];value=entry['base_value'];kind=entry['target_kind'];target=None;old=None
   if guid in buses:
    if index!=0:raise ValueError('Unreviewed snapshot bus baseline field')
    target=buses[guid];old=target['mixer_strip']['volume'];target['mixer_strip']['volume']=value
   elif guid in effects:
    target=effects[guid]
    if kind=='SEFB':
     if index!=0:raise ValueError('Unreviewed snapshot send baseline field')
     old=target['level_db'];target['level_db']=value
    elif kind=='BEFB':
     i=index-1000
     if i<0 or i>=len(target['parameters']):raise ValueError('Unreviewed snapshot DSP baseline field')
     old=target['parameters'][i];typ=target['parameter_types'][i]
     if typ==1:
      if value!=int(value):raise ValueError('Noninteger snapshot target for integer DSP')
      value=int(value)
     elif typ==2:
      if value not in [0,1]:raise ValueError('Nonboolean snapshot target for bool DSP')
      value=bool(value)
     elif typ!=0:raise ValueError('Unreviewed snapshot DSP data baseline')
     target['parameters'][i]=value
    else:raise ValueError('Unreviewed admitted snapshot baseline target')
   if target is not None:
    receipt={**entry,'original_static_value':old,'effective_value':value}
    target.setdefault('primary_snapshot_base',[]).append(receipt);applied.append(receipt)
  snapshot['baseline_applied_to_admitted_targets']=applied
  snapshot['unresolved_controlled_baselines_not_applied']=skipped
 sources['MasterBank.bank']=master.b.original
 namespace=audit_namespace(content,['/EmptyCue'])
 event_names={k.removeprefix('event:'):v for k,v in namespace['namespace'].items()if k.startswith('event:')}
 out={'schema':'hades-native-audio-events-v1','timeline_rate':48000,'samples':streams['samples'],'events':events,'buses':buses,'effects':effects,'event_namespace':event_names,'namespace_source':namespace['strings_bank'],'source_banks':sources,'guids':guids_id,'stream_manifest':stream_id,'additional_stream_manifests':additional,'unsupported_events':{k:v for k,v in sorted(unsupported.items())if k not in events},'adaptations':['Android mixer/resampler/DSP use source graph and original parameters with documented portable approximations.','FMOD bitwise PCM equivalence after event DSP is not claimed.'],'playback_ready':False}
 if animation_closures:out['selected_animation_audio_closures']=animation_closures
 if snapshot is not None:out['mixing_control']={'name':'/Control/MixingControl','reverb':snapshot}
 if 'voice_namespace' in streams:out.update(voice_namespace=streams['voice_namespace'],voice_header=streams['voice_header'],voice_subtitles=streams['voice_subtitles'],actor_definitions=streams['actor_definitions'])
 output.mkdir(parents=True);(output/'audio-events.lua').write_text('return '+literal(out)+'\n');(output/'manifest.json').write_text(json.dumps(out,indent=2)+'\n');(output/'receipt.json').write_text(json.dumps({'schema':'hades-native-audio-event-receipt-v1','selected_animation_audio_closures':animation_closures,'catalog':read_original(output/'audio-events.lua')[1],'manifest':read_original(output/'manifest.json')[1]},indent=2)+'\n')
 return {'output':str(output),'events':len(events),'unsupported_events':len(out['unsupported_events']),'buses':len(buses),'effects':len(effects),'samples':len(streams['samples'])}
def main():
 root=Path(__file__).resolve().parents[2];p=argparse.ArgumentParser(description=__doc__);p.add_argument('--content',type=Path,default=root/'.local/game/base/Content');p.add_argument('--streams',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--extra-streams',type=Path,action='append',default=[]);p.add_argument('--mixing-control',action='store_true');p.add_argument('--tolerate-unsupported',action='store_true',help='record (not abort on) source graph features the event player does not reproduce');a=p.parse_args();print(json.dumps(export(a.content,a.streams,a.output,a.extra_streams,a.mixing_control,a.tolerate_unsupported),indent=2))
if __name__=='__main__':main()
