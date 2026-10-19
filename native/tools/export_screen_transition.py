#!/usr/bin/env python3.13
"""Append original RoomTransition Book movie frames to a fresh screen bank."""
import argparse,copy,hashlib,json,struct,os
from pathlib import Path
from types import SimpleNamespace
from export_actor_animations import export
from export_effect_initialization import lua
from build_room_scene import identity

def split(raw):
 if raw[:4]!=b'HAA1':raise ValueError('Original screen bank magic')
 nf,nc=struct.unpack_from('<II',raw,4);at=12
 for _ in range(nf):
  size=struct.unpack_from('<I',raw,at+44)[0];at+=52+size
 if at>len(raw):raise ValueError('Original screen bank frame bound')
 return nf,nc,at

def clips(raw,start,count,frame_base,reverse_name):
 at=start;out=bytearray()
 for _ in range(count):
  begin=at;names=[]
  for _ in range(3):
   size=struct.unpack_from('<I',raw,at)[0];at+=4;names.append(raw[at:at+size].decode());at+=size
  original,angles,flags,slides=struct.unpack_from('<4I',raw,at);at+=32
  at+=4*angles
  header=raw[begin:at];rows=[]
  for _ in range(slides):
   row=bytearray(raw[at:at+8]);at+=8
   for _ in range(angles):
    index=struct.unpack_from('<I',raw,at)[0];at+=4;row+=struct.pack('<I',index+frame_base)
   size=struct.unpack_from('<I',raw,at)[0];row+=raw[at:at+4+size];at+=4+size;rows.append(row)
  if names[0]in reverse_name:rows.reverse()
  out+=header+b''.join(rows)
 if at!=len(raw):raise ValueError('Original screen bank clip/trailing bounds')
 return bytes(out)

def main(a):
 if a.output.exists():raise ValueError('Fresh output required')
 base=json.loads(a.base_manifest.read_text());packet=Path(base['packet']['path']);raw=packet.read_bytes()
 if hashlib.sha256(raw).hexdigest()!=base['packet']['sha256']:raise ValueError('Original retained screen bank identity')
 a.output.mkdir(parents=True)
 if a.prepared_movie:
  prepared=json.loads(a.prepared_movie.read_text());os.environ['HADES_PREDECODED_BINK_MANIFEST']=str(a.prepared_movie.resolve());a.decoder=Path(__file__).with_name('emit_predecoded_bink.py')
 args=SimpleNamespace(content=a.content,decoder=a.decoder,output=a.output/'transition-source',source=a.content/'Game/Animations/GUIAnimations.sjson',names=['RoomTransitionIn','RoomTransitionOut'],unit_scale=1.,directions=1,allow_unused_movie_tail=True,movie_directory=a.content/'Movies/720p')
 export(args);extra=json.loads((args.output/'manifest.json').read_text());newraw=Path(extra['packet']['path']).read_bytes();nf,nc,cut=split(raw);xf,xc,xcut=split(newraw)
 if set(c['name']for c in base['clips'])&set(c['name']for c in extra['clips']):raise ValueError('Screen clip duplicate')
 target=a.output/'actor-animations.bin';target.write_bytes(b'HAA1'+struct.pack('<II',nf+xf,nc+xc)+raw[12:cut]+newraw[12:xcut]+raw[cut:]+clips(newraw,xcut,xc,nf,{'RoomTransitionOut'}))
 for c in extra['clips']:
  if c['definition'].get('PlayBackwards'):c['source_sequence'].reverse()
  for row in c['source_sequence']:row['frame_indices']=[v+nf for v in row['frame_indices']]
 extra_frames=copy.deepcopy(extra['frames'])
 for f in extra_frames:f['index']+=nf
 m={**base,'packet':identity(target),'sources':base['sources']+extra['sources'],'clips':base['clips']+extra['clips'],'frames':base['frames']+extra_frames,'transition_source_manifest':identity(args.output/'manifest.json'),'statistics':{**base['statistics'],'clips':nc+xc,'movie_frames':nf+xf,'cropped_raw_bytes':base['statistics']['cropped_raw_bytes']+extra['statistics']['cropped_raw_bytes'],'zlib_bytes':base['statistics']['zlib_bytes']+extra['statistics']['zlib_bytes'],'max_frame_rgba_bytes':max(base['statistics']['max_frame_rgba_bytes'],extra['statistics']['max_frame_rgba_bytes'])},'transition_timing':'Original source Book30fps. RoomTransitionIn sequence1..20, HoldLastFrame=true; Out original sequence20..1, PlayBackwards=true,HoldLastFrame=false. HAA1 playback index advances in native-ready source order.'}
 if a.prepared_movie:m['independent_movie_predecode']=identity(a.prepared_movie)
 catalog={'schema':'hades-screen-animations-v1','bank_sha256':m['packet']['sha256'],'target_sha256':'d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254','clips':{c['name']:{'definition':c['definition'],'source_sequence':c['source_sequence']}for c in m['clips']}}
 (a.output/'screen-animations.lua').write_text('return '+lua(catalog)+'\n');m['screen_catalog']=identity(a.output/'screen-animations.lua');(a.output/'manifest.json').write_text(json.dumps(m,indent=2)+'\n');print(json.dumps({'packet':m['packet'],'statistics':m['statistics']}))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for key in ['content','decoder','base-manifest','output']:p.add_argument('--'+key,type=Path,required=True)
 p.add_argument('--prepared-movie',type=Path)
 main(p.parse_args())
