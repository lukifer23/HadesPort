#!/usr/bin/env python3
"""Bounded read-only original strings.bank radix namespace, not FMOD execution."""
import argparse,json,sys,struct
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from hades_audio import Cursor,chunks,read_original,guid_names,digest

def audit(content,selected):
 base=content/'Audio/FMOD/Build';raw,identity=read_original(base/'Desktop/MasterBank.strings.bank')
 if raw[:4]!=b'RIFF' or raw[8:12]!=b'FEV ' or struct.unpack_from('<I',raw,4)[0]+8!=len(raw):raise ValueError('Original strings bank format/bounds')
 records=list(chunks(raw,12,len(raw)));data=[c for c in records if c['kind']=='STDT']
 formats=[c for c in records if c['kind']=='FMT ']
 if len(formats)!=1 or formats[0]['payload']!=struct.pack('<II',0x86,0x84):raise ValueError('Outside selected FEV version')
 if len(data)!=1:raise ValueError('Expected one original string table')
 r=Cursor(data[0]['payload'])
 if r.u32()!=1:raise ValueError('Outside selected 24bit radix table type')
 nodes=r.array(lambda q:(q.u32(),q.u32()));guids=r.array(lambda q:q.guid())
 blob=r.take(r.x16())
 def array24():
  count=r.x16()
  if count>100000:raise ValueError('Unbounded radix index table')
  return [int.from_bytes(r.take(3),'little')for _ in range(count)]
 leaves=array24();parents=array24()
 if r.pos!=len(r.raw) or len(leaves)!=len(guids) or len(parents)!=len(nodes):raise ValueError('Incomplete original radix table')
 namespace={}
 for guid,leaf in zip(guids,leaves):
  segments=[];seen=set();node=leaf
  while node!=0xffffff:
   if node>=len(nodes)or node in seen:raise ValueError('Invalid original radix parent chain')
   seen.add(node);offset=nodes[node][0]&0xffffff
   if offset!=0xffffff:
    end=blob.find(b'\0',offset)
    if offset>=len(blob)or end<0:raise ValueError('Invalid original string offset')
    segments.append(blob[offset:end].decode('utf-8'))
   node=parents[node]
  path=''.join(reversed(segments))
  if path in namespace and namespace[path]!=guid:raise ValueError('Ambiguous original namespace')
  namespace[path]=guid
 declared,proof=guid_names(base/'GUIDs.txt')
 matched=sum(declared.get(k)==v for k,v in namespace.items())
 return {'schema':'hades-original-audio-namespace-v1','strings_bank':identity,'string_table':{k:data[0][k]for k in ['offset','bytes']},'string_table_sha256':digest(data[0]['payload']),'guid_declarations':proof,'namespace':namespace,'node_count':len(nodes),'guid_count':len(guids),'declaration_matches':matched,'selected':{name:{'lookup_path':'event:'+name,'present_in_strings_bank':'event:'+name in namespace,'present_in_guid_declarations':'event:'+name in declared}for name in selected},'reference':{'commit':'b7cfcfc60c93fcca7e4ffe666144e7ee3ed7ce77','files':['Metadata/StringTable.cs','Objects/FRadixTreePacked.cs','Objects/FPackedNode.cs','FModReader.cs'],'scope':'Focused public source field layout corroboration. Original bank fully consumed and bounded independently.'},'limits':['Static namespace membership; no original FMOD execution.','Native LookupEventDescription adds event: for non-VO non-GUID paths and returns null when FMOD.lookupID fails; named SoundCue validity requires a nonnull description.','No absent name is turned into a fabricated silent event.']}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--content',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('names',nargs='+');a=p.parse_args()
 if a.output.exists()or a.output.is_symlink():raise ValueError('Output must be new')
 result=audit(a.content,a.names);a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items()if k!='namespace'},indent=2))
if __name__=='__main__':main()
