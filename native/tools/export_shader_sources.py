#!/usr/bin/env python3
"""Extract bounded, original HLSL source streams from shipped DXBC/SPDB files.

Does not execute or decompile shaders. MSF block ranges retain byte provenance.
"""
import argparse, hashlib, json, struct
from pathlib import Path

def u32(b, o):
 if o < 0 or o+4 > len(b): raise ValueError('uint32 out of bounds')
 return struct.unpack_from('<I', b, o)[0]

def sources(path):
 b=path.read_bytes()
 if b[:4]!=b'DXBC' or u32(b,24)!=len(b):raise ValueError('Invalid DXBC size/header')
 count=u32(b,28)
 if count>128:raise ValueError('Chunk cap')
 for i in range(count):
  off=u32(b,32+4*i);size=u32(b,off+4)
  if off+8+size>len(b):raise ValueError('Chunk exceeds DXBC')
  if b[off:off+4]!=b'SPDB':continue
  p=b[off+8:off+8+size]
  if not p.startswith(b'Microsoft C/C++ MSF 7.00\r\n'):raise ValueError('Unexpected PDB')
  bs,_,nb,ds,_,bm=struct.unpack_from('<6I',p,32)
  if bs not in (512,1024,2048,4096) or nb*bs!=len(p) or ds>len(p):raise ValueError('MSF bounds')
  def blocks(indices,size):
   if any(v>=nb for v in indices):raise ValueError('MSF block out of range')
   return b''.join(p[v*bs:(v+1)*bs]for v in indices)[:size]
  n=(ds+bs-1)//bs
  directory=blocks([u32(p,bm*bs+4*j)for j in range(n)],ds)
  ns=u32(directory,0)
  if ns>65536:raise ValueError('Stream cap')
  sizes=[u32(directory,4+4*j)for j in range(ns)];q=4+4*ns;streams=[]
  for z in sizes:
   ids=[]if z==0xffffffff else [u32(directory,q+4*j)for j in range((z+bs-1)//bs)]
   q+=4*len(ids);streams.append((blocks(ids,0 if z==0xffffffff else z),ids))
  info=streams[1][0];namesize=u32(info,28);names=info[32:32+namesize];q=32+namesize
  entries,capacity=u32(info,q),u32(info,q+4);q+=8
  words=u32(info,q);q+=4;present=[u32(info,q+4*j)for j in range(words)];q+=4*words
  words=u32(info,q);q+=4+4*words
  populated=sum(x.bit_count()for x in present)
  if populated!=entries or entries>capacity:raise ValueError('Named stream table mismatch')
  for j in range(entries):
   no,idx=u32(info,q),u32(info,q+4);q+=8
   end=names.find(b'\0',no)
   if end<0 or idx>=ns:raise ValueError('Named stream invalid')
   name=names[no:end].decode('utf-8')
   if not name.startswith('/src/files/'):continue
   data,ids=streams[idx]
   data.decode('utf-8')
   yield {'name':name,'stream':idx,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'source_blocks':[{'offset':off+8+v*bs,'size':min(bs,max(0,len(data)-k*bs))}for k,v in enumerate(ids)]},data

def main():
 a=argparse.ArgumentParser();a.add_argument('--input',type=Path,required=True);a.add_argument('--output',type=Path,required=True);args=a.parse_args()
 args.output.mkdir(parents=True,exist_ok=False);records=[]
 for p in sorted(args.input.iterdir()):
  if p.suffix not in ('.frag','.vert'):continue
  for rec,data in sources(p):
   key=hashlib.sha256(data).hexdigest()[:16];name=rec['name'].replace('\\','/').rsplit('/',1)[-1];dest=args.output/f'{key}-{name}'
   if not dest.exists():dest.write_bytes(data)
   records.append({'shader':str(p.resolve()),'shader_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'shader_bytes':p.stat().st_size,**rec,'output':dest.name})
 (args.output/'manifest.json').write_text(json.dumps({'format':'DXBC SPDB / MSF7 original named source streams','records':records},indent=2)+'\n')
 print(json.dumps({'streams':len(records),'unique_sources':len(set(r['output']for r in records)),'output':str(args.output)}))
if __name__=='__main__':main()
