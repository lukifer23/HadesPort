#!/usr/bin/env python3
"""Focused original 0x86 music timeline metadata, without event execution."""
import argparse,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from hades_audio import Bank,Cursor,guid_names,digest,boxes,guid_list,read_original
class Reader(Cursor):
    def f(self):return struct.unpack('<f',self.take(4))[0]
    def text(self):return self.take(self.x16()).decode('utf-8')
    def versioned(self,reader):
        count=self.x16()>>1
        if count>65536:raise ValueError('Unbounded versioned array')
        out=[]
        for _ in range(count):
            size=self.u16();start=self.pos;out.append(reader(self))
            if self.pos-start!=size:raise ValueError('Unaccounted versioned element')
        return out
    def evaluators(self):
        size=self.i32();end=self.pos+size;out=[]
        if size<0 or end>len(self.raw):raise ValueError('Unbounded evaluator program')
        while self.pos<end:
            raw_type=self.u32();typ=raw_type&255;value=None
            if typ in [0,1,2,3,0x12,0x30]:pass
            elif typ==0x10:value=self.u32()
            elif typ==0x11:value=self.guid()
            elif typ==0x20:value=[self.f(),self.f()]
            else:raise ValueError('Unmapped evaluator instruction '+hex(typ))
            out.append({'raw_type':raw_type,'type':typ,'value':value})
        if self.pos!=end:raise ValueError('Evaluator program exceeds declared bytes')
        return out
    def quantization(self):
        unit=self.u32();return {'unit':unit,'multiplier':self.i32() if unit<=2 else 0}
    def routable(self):
        size=self.u16();start=self.pos;out={'bus_guid':self.guid(),'output_channel_layout':self.u32(),'channel_mask':self.u32()}
        if self.pos-start!=size:raise ValueError('Routable size mismatch')
        return out
def finished(r):
    if r.pos!=len(r.raw):raise ValueError('Unaccounted selected record bytes '+str((r.pos,len(r.raw))))
def timeline(ch):
    r=Reader(ch['payload']);out={'guid':r.guid(),'trigger_boxes':boxes(r),'time_locked_trigger_boxes':boxes(r)}
    out['sustain_points']=r.versioned(lambda q:{'position':q.u32(),'evaluators':q.evaluators()})
    out['named_markers']=r.versioned(lambda q:{'guid':q.guid(),'position':q.u32(),'name':q.text(),'length':q.u32()})
    out['tempo_markers']=r.array(lambda q:{'guid':q.guid(),'time_signature':struct.unpack('<q',q.take(8))[0],'position':q.u32(),'tempo':q.f()});finished(r);return out
def transition(ch):
    r=Reader(ch['payload']);out={'guid':r.guid(),'destination_guid':r.guid(),'start':r.u32(),'end':r.u32(),'evaluators':r.evaluators(),'quantization':r.quantization(),'chance_percent':r.f(),'flags':r.u32()};finished(r);return out
def transition_body(ch):
    if not ch['payload']:return {'empty_record':True}
    r=Reader(ch['payload']);out={'length':r.u32(),'fade_overrides_legacy':r.array(lambda q:{'controller_guid':q.guid(),'curve_guid':q.guid()}),'time_locked_trigger_boxes':boxes(r),'triggered_trigger_boxes':boxes(r),'lead_in_length':r.u32(),'lead_out_length':r.u32(),'lead_in_curves':r.array(lambda q:{'bus_guid':q.guid(),'curve_guid':q.guid()}),'lead_out_curves':r.array(lambda q:{'bus_guid':q.guid(),'curve_guid':q.guid()}),'curve_mapping_guid':r.guid(),'fade_overrides':r.array(lambda q:{'controller_guid':q.guid(),'curve_guid':q.guid()})};finished(r);return out
def parameter(ch):
    r=Reader(ch['payload']);out={'guid':r.guid(),'flags':r.i32(),'type':r.u32(),'name':r.text(),'minimum':r.f(),'maximum':r.f(),'default_value':r.f(),'velocity':r.f(),'seek_speed':r.f(),'seek_speed_down':r.f()};finished(r);return out
def instrument(ch):
    r=Reader(ch['payload']);out={'timeline_guid':r.guid(),'volume':r.f(),'pitch':r.f(),'loop_count':r.i32(),'flags':r.i32(),'offset_3d_distance':r.f(),'trigger_chance_percent':r.f(),'trigger_delay_min':r.f(),'trigger_delay_max':r.f(),'quantization':r.quantization(),'control_parameter_guid':r.guid(),'auto_pitch_reference':r.f(),'initial_seek_position':r.f(),'maximum_polyphony':r.i32(),'routable':r.routable(),'polyphony_limit_behavior':r.i32(),'left_trim_offset':r.u32(),'initial_seek_percent':r.f(),'auto_pitch_at_minimum':r.f(),'evaluators':r.evaluators()};finished(r);return out
def prop(ch):
    r=Reader(ch['payload']);out={'index':r.u32(),'method':r.u16(),'type':r.u16(),'mapping_guid':r.guid(),'controllers':guid_list(r),'modulators':guid_list(r)};finished(r);return out
def control(ch):
    r=Reader(ch['payload']);out={'guid':r.guid(),'property_owner_guid':r.guid(),'curve_guid':r.guid(),'property_index':r.i32()};finished(r);return out
def curve(ch):
    def point(q):
        raw=q.take(4);return {'x_float':struct.unpack('<f',raw)[0],'x_raw_u32':struct.unpack('<I',raw)[0],'y':q.f(),'shape':q.f(),'type':q.u32()}
    r=Reader(ch['payload']);out={'guid':r.guid(),'owner_guid':r.guid(),'points':r.array(point)};finished(r);return out
def bus_body(ch):
    r=Reader(ch['payload']);out={'flags':r.take(1)[0],'input_channel_layout':r.u32(),'pre_fader_effects':guid_list(r),'post_fader_effects':guid_list(r)}
    size=r.u16();start=r.pos;out['mixer_strip']={'volume':r.f(),'pitch':r.f()}
    if r.pos-start!=size:raise ValueError('Mixer strip fixed payload size mismatch')
    out['mixer_strip']['vcas']=guid_list(r)
    out.update(maximum_polyphony=r.i32(),polyphony_limit_behavior=r.i32(),pre_fader_input_channel_layouts=r.array(lambda q:q.u32()),post_fader_input_channel_layouts=r.array(lambda q:q.u32()),object_panner_index=r.i32());finished(r);return out
def source(ch,parsed):return {'kind':ch['kind'],'offset':ch['offset'],'bytes':ch['bytes'],'payload_sha256':digest(ch['payload']),'raw_hex':ch['payload'].hex(),'value':parsed}
def _index(b):
    """One pass over a bank's chunks: the same lookups audit() needs, built once
    per Bank instead of rescanning every chunk per node (identical results and
    chunk order; derived catalogs audit hundreds of events per bank)."""
    if getattr(b,'_audit_index',None) is not None:return b._audit_index
    by_parent={};own={};own_key={};prefix={'TRNB':{},'PROP':{}};ctro=[]
    for ch in b.chunks:
        by_parent.setdefault(ch['parents'],[]).append(ch)
        if ch['kind']in ['EVTB','TLNB','PMLB','PRMB','WAIB','MUIB','SCIB','PRIB','EVIB','WAV ','TRNB','CTRL','CURV','GBSB','IBSB','MBSB','RBSB']:
            k=(ch['kind'],ch['payload'][:16].hex());own[k]=ch
        if ch['kind']in prefix:
            for n in range(len(ch['parents'])+1):prefix[ch['kind']].setdefault(ch['parents'][:n],[]).append(ch)
        if ch['kind']=='CTRO':
            r=Reader(ch['payload']);owned=guid_list(r);finished(r);ctro.append((ch,owned))
    for (typ,k),ch in own.items():own_key.setdefault(k,[]).append((typ,ch))
    b._audit_index=(by_parent,own,own_key,prefix,ctro);return b._audit_index
def audit(path,guid):
    b=Bank(path);event=b.nodes[guid];reach=b.resolve(guid);selected=set(reach['reachable_nodes']);typed={}
    by_parent,own,own_key,prefix,ctro=_index(b)
    def lookup(g,kind=None):
        key=__import__('uuid').UUID(g).bytes_le.hex()
        if kind:return own.get((kind,key))
        matches=[ch for typ,ch in own_key.get(key,[])if typ not in ['CTRL','CURV']]
        if len(matches)>1:raise ValueError('Ambiguous typed GUID '+g)
        return matches[0]if matches else None
    param_guids={b.nodes[g]['parameter_guid']for g in event['parameter_layouts']}
    track_guids=set(event['nonmaster_tracks']+[event['master_track_guid'],event['input_bus_guid']])
    for g in list(selected)+list(param_guids)+list(track_guids):
        ch=lookup(g)
        if ch is None:raise ValueError('Missing selected metadata GUID '+g)
        kind=ch['kind'];parsed=None
        if kind=='TLNB':
            parsed=timeline(ch);regions=[]
            for candidate in prefix['TRNB'].get(ch['parents'],[]):
                if True:
                    tr=transition(candidate);tr['source']=source(candidate,tr.copy());tr['body']=[source(c,transition_body(c))for c in by_parent[candidate['parents']] if c['kind']=='TRTL'];regions.append(tr)
            parsed['transition_regions']=regions
        elif kind=='PRMB':parsed=parameter(ch)
        elif kind in ['GBSB','IBSB','MBSB','RBSB']:
            r=Reader(ch['payload']);parsed={'guid':r.guid(),'routable':r.routable()};finished(r)
            parsed['bus_body']=[source(c,bus_body(c))for c in by_parent[ch['parents']] if c['kind']=='BUS ']
        else:parsed=b.nodes.get(g)
        entry=source(ch,parsed);entry['instruments']=[source(c,instrument(c))for c in by_parent[ch['parents']]if c['kind']=='INST']
        props=[]
        # PRPS is a child list of the corresponding bus/instrument container.
        for candidate in prefix['PROP'].get(ch['parents'],[]):
            if True:
                p=prop(candidate);controllers=[]
                for ctrl in p['controllers']:
                    cc=lookup(ctrl,'CTRL')
                    if cc is None:raise ValueError('Missing selected property controller')
                    cv=control(cc);curve_ch=lookup(cv['curve_guid'],'CURV');owners=[]
                    for owner_ch,owned in ctro:
                        if ctrl in owned:
                            parents=[c for c in by_parent[owner_ch['parents']]if c['kind']in ['PMLB','TLNB']]
                            for parent in parents:
                                parent_guid=Reader(parent['payload']).guid();parent_node=b.nodes[parent_guid]
                                owners.append({'owner_kind':parent['kind'],'owner_guid':parent_guid,'parameter_guid':parent_node.get('parameter_guid'),'membership':source(owner_ch,{'controllers':owned})})
                    controllers.append({'controller':source(cc,cv),'curve':source(curve_ch,curve(curve_ch))if curve_ch else None,'owners':owners})
                props.append({'property':source(candidate,p),'controllers':controllers})
        entry['properties']=props;typed[g]=entry
    return {'schema':'hades-original-music-event-audit-v1','bank':b.original,'event_guid':guid,'event':event,'sample_reachability':reach,'selected_metadata':typed,'limits':['No original executable execution.','Parser source corroborates field layout; evaluator opcodes, curve interpolation types, property indices and FMOD event execution remain unimplemented.','Timeline integer units retained verbatim; their conversion to sample time must not be assumed.','Bank DSP and external parent buses outside selected metadata are not yet resolved.']}
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--bank',type=Path,required=True);p.add_argument('--guids',type=Path,required=True);p.add_argument('--event',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.output.exists()or a.output.is_symlink():raise ValueError('Evidence output must be new')
    names,identity=guid_names(a.guids);out=audit(a.bank,names['event:'+a.event]);out['guid_declarations']=identity;a.output.write_text(json.dumps(out,indent=2)+'\n');print(json.dumps({'event':a.event,'metadata_nodes':len(out['selected_metadata']),'samples':len(out['sample_reachability']['samples']),'output':str(a.output)},indent=2))
if __name__=='__main__':main()
