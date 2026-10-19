#!/usr/bin/env python3
"""Read-only bounded FMOD 0x86 event/sample evidence for preserved Hades banks.

Not an FMOD event player. Typed links identify reachable waveform resources;
timeline conditions, mixer automation and playlist choice remain separate.
Container/sample interpretation is corroborated against pinned public parsers.
"""
import hashlib, os, re, stat, struct, uuid
from pathlib import Path

MAX_BANK=800*1024*1024
NODE_TYPES={'EVTB','TLNB','PMLB','WAIB','MUIB','SCIB','EVIB','PRIB','WAV '}
def digest(raw): return hashlib.sha256(raw).hexdigest()
def read_original(path):
    fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW)
    with os.fdopen(fd,'rb') as f:
        before=os.fstat(f.fileno())
        if not stat.S_ISREG(before.st_mode) or before.st_size>MAX_BANK: raise ValueError('Unbounded/nonregular bank')
        raw=f.read(MAX_BANK+1); after=os.fstat(f.fileno())
        if (before.st_ino,before.st_size,before.st_mtime_ns)!=(after.st_ino,after.st_size,after.st_mtime_ns) or len(raw)!=before.st_size: raise ValueError('Original changed during read')
    return raw,{'path':str(path.resolve()),'bytes':len(raw),'sha256':digest(raw)}
class Cursor:
    def __init__(self,raw): self.raw=raw;self.pos=0
    def take(self,n):
        if n<0 or self.pos+n>len(self.raw):raise ValueError('Metadata exceeds containing chunk')
        out=self.raw[self.pos:self.pos+n];self.pos+=n;return out
    def u16(self):return struct.unpack('<H',self.take(2))[0]
    def u32(self):return struct.unpack('<I',self.take(4))[0]
    def i32(self):return struct.unpack('<i',self.take(4))[0]
    def guid(self):return str(uuid.UUID(bytes_le=self.take(16)))
    def x16(self):
        low=self.u16();return (low&0x7fff)|(self.u16()<<15) if low&0x8000 else low
    def array(self,reader):
        count=self.x16()>>1
        if count>65536:raise ValueError('Unbounded metadata array')
        if not count:return []
        size=self.u16();out=[]
        for _ in range(count):
            start=self.pos;out.append(reader(self))
            if self.pos-start!=size:raise ValueError('Unaccounted array element bytes')
        return out
    def skip_versioned(self):
        count=self.x16()>>1
        if count>65536:raise ValueError('Unbounded versioned array')
        for _ in range(count):self.take(self.u16())
def guid_list(c):return c.array(lambda r:r.guid())
def boxes(c):return c.array(lambda r:{'guid':r.guid(),'start':r.u32(),'length':r.u32()})
def chunks(raw,start,end,parents=()):
    while start+8<=end:
        kind=raw[start:start+4].decode('ascii');size=struct.unpack_from('<I',raw,start+4)[0];finish=start+8+size
        if finish>end:raise ValueError('RIFF child exceeds parent')
        if kind=='LIST':
            if size<4:raise ValueError('Short RIFF list')
            yield from chunks(raw,start+12,finish,parents+((raw[start+8:start+12].decode('ascii'),start),))
        else:yield {'kind':kind,'offset':start,'bytes':size,'parents':parents,'payload':raw[start+8:finish]}
        start=finish+(size&1)
    if start!=end:raise ValueError('Unaccounted RIFF tail')
def fsb_samples(raw,offset,length,bank_index):
    b=raw[offset:offset+length]
    if b[:4]!=b'FSB5':raise ValueError('Embedded bank is not FSB5')
    version,count,header_bytes,name_bytes,data_bytes,codec=struct.unpack_from('<6I',b,4)
    base=0x3c if version==1 else 0x40
    if version!=1 or count>100000 or base+header_bytes+name_bytes+data_bytes!=length:raise ValueError('Unmapped FSB version/length')
    rates=[4000,8000,11000,11025,16000,22050,24000,32000,44100,48000,96000];samples=[];at=base
    for index in range(count):
        start=at;mode=struct.unpack_from('<Q',b,at)[0];at+=8
        rate_index=(mode>>1)&15
        if rate_index>=len(rates):raise ValueError('Unmapped sample rate')
        rec={'sound_bank_index':bank_index,'subsound_index':index,'sample_header_offset':offset+start,'sample_count':mode>>34,'sample_rate':rates[rate_index],'channels':[1,2,6,8][(mode>>5)&3],'data_relative_offset':((mode>>7)&0x7ffffff)<<5,'codec':codec,'extra_chunks':[]}
        more=mode&1
        while more:
            extra=struct.unpack_from('<I',b,at)[0];size=(extra>>1)&0xffffff;kind=extra>>25;more=extra&1;at+=4
            if at+size>base+header_bytes:raise ValueError('Sample extra exceeds header section')
            payload=b[at:at+size];rec['extra_chunks'].append({'kind':kind,'offset':offset+at,'bytes':size,'sha256':digest(payload)})
            if kind==1:rec['channels']=payload[0]
            elif kind==2:rec['sample_rate']=struct.unpack_from('<I',payload)[0]
            elif kind==3:rec['loop_start']=struct.unpack_from('<I',payload)[0];rec['loop_end_exclusive']=struct.unpack_from('<I',payload,4)[0]+1 if size>=8 else None
            elif kind==11:rec['vorbis_setup_id']=struct.unpack_from('<I',payload)[0]
            at+=size
        rec['sample_header_bytes']=at-start
        if name_bytes:
            n=base+header_bytes+struct.unpack_from('<I',b,base+header_bytes+4*index)[0];end=b.find(b'\0',n,base+header_bytes+name_bytes)
            if end<0:raise ValueError('Unterminated sample name')
            rec['name']=b[n:end].decode('utf-8')
        samples.append(rec)
    if at!=base+header_bytes:raise ValueError('Unaccounted sample headers')
    for i,rec in enumerate(samples):
        rec['data_offset']=offset+base+header_bytes+name_bytes+rec['data_relative_offset'];rec['encoded_bytes']=(samples[i+1]['data_relative_offset'] if i+1<count else data_bytes)-rec['data_relative_offset']
        if rec['encoded_bytes']<=0:raise ValueError('Invalid encoded sample range')
        rec['encoded_sha256']=digest(raw[rec['data_offset']:rec['data_offset']+rec['encoded_bytes']]);rec['global_subsong']=i+1
    return samples
class FsbBank:
    """Original standalone FSB sample inventory; same bounded sample interface."""
    def __init__(self,path):
        self.raw,self.original=read_original(path)
        self.samples=fsb_samples(self.raw,0,len(self.raw),0)
        self.embedded_banks=[{'offset':0,'bytes':len(self.raw)}]
        self.nodes={};self.version=1;self.subversion=None

class Bank:
    def __init__(self,path):
        self.raw,self.original=read_original(path);raw=self.raw
        if raw[:4]!=b'RIFF' or raw[8:12]!=b'FEV ' or struct.unpack_from('<I',raw,4)[0]+8!=len(raw):raise ValueError('Invalid RIFF/FEV bounds')
        self.chunks=list(chunks(raw,12,len(raw)));fmt=next(x for x in self.chunks if x['kind']=='FMT ')
        self.version,self.subversion=struct.unpack('<II',fmt['payload'])
        if (self.version,self.subversion)!=(0x86,0x84):raise ValueError('Outside selected Hades FEV version')
        self.nodes={};self.samples=[];self.embedded_banks=[];by_parent={}
        for ch in self.chunks:by_parent.setdefault(ch['parents'],[]).append(ch)
        for ch in self.chunks:
            typ=ch['kind'];pl=ch['payload']
            if typ not in NODE_TYPES:continue
            c=Cursor(pl);guid=c.guid();node={k:ch[k] for k in ['kind','offset','bytes']};node.update(guid=guid,links=[],payload_sha256=digest(pl))
            if typ=='EVTB':
                node.update(snapshot_guid=c.guid(),timeline_guid=c.guid(),input_bus_guid=c.guid(),master_track_guid=c.guid());c.take(13)
                node['parameter_layouts']=guid_list(c);c.skip_versioned();c.skip_versioned();c.take(16)
                node['nonmaster_tracks']=guid_list(c);c.array(lambda r:r.take(8));node['event_triggered_instruments']=guid_list(c)
                node['links']=[node['timeline_guid']]+node['parameter_layouts']+node['event_triggered_instruments']
                if c.pos!=len(pl):raise ValueError('Unmapped selected Event body')
            elif typ=='TLNB':
                node['trigger_boxes']=boxes(c);node['time_locked_trigger_boxes']=boxes(c)
                node['links']=[x['guid'] for x in node['trigger_boxes']+node['time_locked_trigger_boxes']]
                # Sustain/marker/tempo records retained raw; not interpreted as samples.
                node['unparsed_tail_bytes']=len(pl)-c.pos
            elif typ=='PMLB':
                node['parameter_guid']=c.guid();node['links']=guid_list(c);node['flags']=c.u32()
                if c.pos!=len(pl):raise ValueError('Unmapped parameter layout')
            elif typ=='WAIB':node['waveform_guid']=c.guid();node['links']=[node['waveform_guid']]
            elif typ=='WAV ':
                node['payload_size']=c.u16();node['sound_bank_index']=c.i32();node['subsound_index']=c.i32();node['loading_mode']=c.u32()
                if c.pos!=len(pl):raise ValueError('Unmapped waveform resource')
            elif typ=='EVIB':node['links']=[c.guid()]
            for companion in by_parent[ch['parents']]:
                if companion['kind']=='PLST':
                    q=Cursor(companion['payload']);node['playlist_mode']=q.i32();node['playlist_selection']=q.i32();node['playlist']=q.array(lambda r:{'guid':r.guid(),'weight':struct.unpack('<f',r.take(4))[0]});node['links'] += [x['guid'] for x in node['playlist']]
                elif companion['kind']=='INST':node['instrument_body']={'offset':companion['offset'],'bytes':companion['bytes'],'sha256':digest(companion['payload']),'raw_hex':companion['payload'].hex()}
            if guid in self.nodes:raise ValueError('Duplicate typed node GUID')
            self.nodes[guid]=node
        snd=next((x for x in self.chunks if x['kind']=='SNDH'),None)
        if snd:
            c=Cursor(snd['payload']);entries=c.array(lambda r:(r.u32(),r.u32()))
            for bank_index,(offset,length) in enumerate(entries):
                samples=fsb_samples(raw,offset,length,bank_index);base=len(self.samples)
                for sample in samples:sample['global_subsong']+=base
                self.embedded_banks.append({'index':bank_index,'offset':offset,'bytes':length,'sha256':digest(raw[offset:offset+length]),'sample_count':len(samples)});self.samples+=samples
    def resolve(self,guid):
        pending=[guid];seen=[];missing=[];resources=[]
        while pending:
            g=pending.pop(0)
            if g in seen or g=='00000000-0000-0000-0000-000000000000':continue
            seen.append(g);node=self.nodes.get(g)
            if node is None:missing.append(g);continue
            if node['kind']=='WAV ':resources.append(node)
            pending.extend(node['links'])
        selected=[]
        for resource in resources:
            match=[x for x in self.samples if x['sound_bank_index']==resource['sound_bank_index'] and x['subsound_index']==resource['subsound_index']]
            if len(match)!=1:raise ValueError('Reachable waveform outside embedded bank')
            selected.append({'waveform_guid':resource['guid'],**match[0]})
        return {'event_guid':guid,'reachable_nodes':seen,'missing_nodes':missing,'samples':selected,'scope':'Typed reachability only; playlist choice, timeline/parameter admission, buses/effects and exact simultaneous playback are not reproduced.'}
    def inventory(self):
        return {'original':self.original,'version':self.version,'subversion':self.subversion,'chunks':[{k:v for k,v in x.items() if k!='payload'} for x in self.chunks],'nodes':self.nodes,'embedded_banks':self.embedded_banks,'samples':self.samples}
def guid_names(path):
    raw,identity=read_original(path);names={}
    for line in raw.decode('utf-8-sig').splitlines():
        match=re.fullmatch(r'\{([0-9a-fA-F-]+)\} (.+)',line)
        if not match:raise ValueError('Unexpected original GUID declaration')
        guid,name=match.groups();names[name]=str(uuid.UUID(guid))
    return names,identity
