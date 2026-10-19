#!/usr/bin/env python3.13
"""Bounded readers for shipped Hades 1 SGG v7 textures and SGB1 v12 maps.

Format reference: quaerus/deppth, SGG-Modding/HadesMapper (pins in research).
No original executables run; all source paths are read-only. RGBA conversion
uses the host's Pillow decoder, never platform-specific game code.
"""
import argparse,ctypes,hashlib,json,math,struct
from pathlib import Path
CHUNK=0x2000000
MAX_ASSET=128*1024*1024
class Reader:
 def __init__(self,data):self.data=data;self.pos=0
 def read(self,n):
  if n<0 or n>MAX_ASSET or self.pos+n>len(self.data):raise ValueError(f'Out of bounds read {n} at {self.pos}')
  b=self.data[self.pos:self.pos+n];self.pos+=n;return b
 def u8(self):return self.read(1)[0]
 def integer(self,endian='>'):return struct.unpack(endian+'i',self.read(4))[0]
 def uint(self,endian='>'):return struct.unpack(endian+'I',self.read(4))[0]
 def floating(self,endian='>'):
  x=struct.unpack(endian+'f',self.read(4))[0]
  if not math.isfinite(x):raise ValueError('Nonfinite float')
  return x
 def smallstring(self):return self.read(self.u8()).decode('utf-8')
 def count(self):
  n=self.integer()
  if not 0<=n<=100000:raise ValueError('Excessive count')
  return n
 def varint(self):
  n=0
  for i in range(5):
   b=self.u8();n|=(b&127)<<(7*i)
   if b<128:return n
  raise ValueError('Excessive varint')
 def xnbstring(self):return self.read(self.varint()).decode('utf-8')
class Package(Reader):
 def __init__(self,path):
  self.path=Path(path);self.file=self.path.open('rb');h=self.file.read(4)
  if h not in (b'\x20\0\0\7',b'\0\0\0\7'):raise ValueError('Unsupported SGG package header '+h.hex())
  self.compressed=h[0]==32;self.chunk=-1;self.virtual_base=4;self.chunks=[];self.data=b'';self.pos=0
 def nextchunk(self):
  at=self.file.tell();cap=CHUNK-4 if self.chunk<0 else CHUNK
  if self.compressed:
   flag=self.file.read(1)
   if not flag:raise ValueError('Unexpected package EOF')
   if flag==b'\1':
    b=self.file.read(4)
    if len(b)!=4:raise ValueError('Truncated chunk length')
    n=int.from_bytes(b,'big');
    if not 0<n<=CHUNK+CHUNK//255+32:raise ValueError('Chunk too large')
    data=self.file.read(n)
    if len(data)!=n:raise ValueError('Truncated package chunk')
    lib=ctypes.CDLL('/opt/homebrew/opt/lz4/lib/liblz4.dylib');lib.LZ4_decompress_safe.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_int,ctypes.c_int]
    out=ctypes.create_string_buffer(cap);got=lib.LZ4_decompress_safe(data,out,n,cap)
    if got<0:raise ValueError('LZ4 package decode failed')
    data=out.raw[:got]
   elif flag==b'\0':data=self.file.read(cap)
   else:raise ValueError('Unknown chunk compression flag')
  else:data=self.file.read(cap)
  if not data:raise ValueError('Empty package chunk')
  self.chunk+=1;self.data=data;self.pos=0
  self.chunks.append({'chunk':self.chunk,'file_offset':at,'file_bytes':self.file.tell()-at,'decoded_bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()})
 def read(self,n):
  if not 0<=n<=MAX_ASSET:raise ValueError('Excessive package read')
  if n==0:return b''
  out=[];remaining=n
  while remaining:
   if self.pos==len(self.data):self.nextchunk()
   take=min(remaining,len(self.data)-self.pos);out.append(self.data[self.pos:self.pos+take]);self.pos+=take;remaining-=take
  return b''.join(out)
 def close(self):self.file.close()
 def entries(self):
  try:
   while True:
    typ=self.u8();at={'chunk':self.chunk,'decoded_offset':self.pos-1}
    if typ==255:return
    if typ==190:self.nextchunk();continue
    if typ in (173,170):
     name=self.smallstring();n=self.integer()
     if not 0<=n<=MAX_ASSET:raise ValueError('Excessive XNB size')
     yield {'type':'texture' if typ==173 else 'texture3d','name':name,'bytes':n,'location':at,'data':self.read(n)}
    elif typ==222:yield self.atlas(at)
    elif typ==204:yield {'type':'include','name':self.smallstring(),'location':at}
    else:raise ValueError(f'Unsupported entry code {typ:02x} at {at}')
  finally:self.close()
 def atlas(self,at):
  size=self.integer();sig=self.integer();version=0
  if sig==2142336875:version=self.integer();n=self.count()
  else:n=sig
  if not 0<=n<=100000 or version not in range(5):raise ValueError('Unsupported atlas version/count')
  sprites=[]
  for _ in range(n):
   name=self.smallstring();rect=[self.integer()for _ in range(4)];trim=[self.integer()for _ in range(2)];original=[self.integer()for _ in range(2)];ratio=[self.floating()for _ in range(2)]
   flags=self.u8() if version>0 else 0
   hull=[[self.integer(),self.integer()]for _ in range(self.count())] if version>2 else []
   sprites.append({'name':name,'rect':rect,'trim':trim,'original':original,'scale_ratio':ratio,'flags':flags,'hull':hull})
  marker=self.u8()
  if marker!=221:raise ValueError('Embedded atlas texture unsupported')
  name=self.smallstring()
  return {'type':'atlas','name':name,'version':version,'declared_bytes':size,'location':at,'sprites':sprites}
def xnb_image(data):
 from PIL import Image
 r=Reader(data)
 if r.read(4)!=b'XNBw':raise ValueError('Not a Windows XNB')
 version=r.u8();flags=r.u8();size=r.uint('<')
 if version not in (5,6)or flags!=0 or size!=len(data):raise ValueError('Unsupported XNB version/flags/size')
 if version==5:
  n=r.varint()
  if n>32:raise ValueError('Too many XNB readers')
  for _ in range(n):r.xnbstring();r.integer('<')
  r.varint();r.varint()
 fmt=r.integer('<');w=r.integer('<');h=r.integer('<');mip=r.integer('<');n=r.integer('<')
 if not 0<w<=16384 or not 0<h<=16384 or w*h>64*1024*1024 or not 0<n<=MAX_ASSET:raise ValueError('Excessive texture dimensions')
 b=r.read(n)
 if fmt==0:
  if n!=w*h*4:raise ValueError('RGBA byte count mismatch')
  im=Image.frombytes('RGBA',(w,h),b,'raw','BGRA')
 elif fmt in (6,28):
  if n!=((w+3)//4)*((h+3)//4)*16:raise ValueError('BC block count mismatch')
  im=Image.frombytes('RGBA',(w,h),b,'bcn',(3 if fmt==6 else 7,))
 else:raise ValueError(f'Unsupported XNB image format {fmt}')
 return im,{'width':w,'height':h,'format':fmt,'mip_count':mip,'first_mip_bytes':n,'trailing_bytes':len(data)-r.pos}
def map_objects(path):
 r=Reader(Path(path).read_bytes())
 if r.read(4)!=b'SGB1'or r.uint('<')!=12:raise ValueError('Unsupported map header')
 count=r.uint('<')
 if count>100000:raise ValueError('Excessive map count')
 def boolean():
  v=r.u8()
  if v not in(0,1):raise ValueError('Invalid bool')
  return bool(v)
 def i():return r.integer('<')
 def f():return r.floating('<')
 def string():
  n=r.uint('<')
  if n>4096:raise ValueError('Excessive string')
  return r.read(n).decode('utf-8')
 def nullable():return string()if boolean()else None
 def boundedcount():
  n=i()
  if not 0<=n<=100000:raise ValueError('Excessive map array')
  return n
 objects=[]
 for _ in range(count):
  start=r.pos;o={'DoCreate':boolean(),'ActivateAtRange':boolean(),'ActivationRange':f(),'Active':boolean(),'AllowMovementReaction':boolean(),'Ambient':f(),'Angle':f()}
  o['AttachedIDs']=[i()for _ in range(boundedcount())];o['AttachToID']=i()
  for k in('CausesOcclusion','Clutter','Collision'):o[k]=boolean()
  # Original LoadVersion12 (VA1800d0cfb..1800d0d45) swaps stored B/R
  # into native Color{r,g,b,a}; preserve exact original bytes separately.
  bgra=list(r.read(4));o['ColorStoredBGRA']=bgra;o['Color']=[bgra[2],bgra[1],bgra[0],bgra[3]];o['Comments']=nullable()
  for k in('CreatesShadows','DataType','DrawVfxOnTop'):o[k]=i()
  o['FlipHorizontal']=boolean();o['FlipVertical']=boolean();o['GroupNames']=[]
  for _ in range(boundedcount()):r.uint('<');o['GroupNames'].append(nullable())
  o['HelpTextID']=nullable()
  for k in('Hue','Saturation','Value'):o[k]=f()
  o['Id']=i();o['IgnoreGridManager']=boolean();o['Invert']=boolean();o['Location']={'X':f(),'Y':f()};o['Name']=nullable();o['OffsetZ']=f();o['ParallaxAmount']=f();o['Points']=[[f(),f()]for _ in range(boundedcount())]
  for k in('Scale','SkewAngle','SkewScale'):o[k]=f()
  o['SortIndex']=i();o['StopsLight']=i();o['Tallness']=f();o['UseBoundsForSortArea']=i();o['source_range']=[start,r.pos-start];objects.append(o)
 if r.pos!=len(r.data):raise ValueError(f'Unparsed map trailing bytes {len(r.data)-r.pos}')
 return objects
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('action',choices=('package','map'));p.add_argument('input',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 if a.action=='map':result={'objects':map_objects(a.input)}
 else:
  pkg=Package(a.input);result={'entries':[{k:v for k,v in e.items()if k!='data'}for e in pkg.entries()],'chunks':pkg.chunks}
 result['source']={'path':str(a.input.resolve()),'bytes':a.input.stat().st_size,'sha256':hashlib.sha256(a.input.read_bytes()).hexdigest()}
 with a.output.open('x')as f:json.dump(result,f,indent=2)
 print({'output':str(a.output)})
