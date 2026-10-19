#!/usr/bin/env python3
"""Validate actual exported HDS3 bytes, frames and room coverage; no fixtures."""
import argparse,hashlib,json,math,struct
from pathlib import Path

def validate(p):
 m=json.loads((p/'scene-manifest.json').read_text());clips=json.loads((p/'animation-clips.json').read_text());coverage=json.loads((p/'coverage.json').read_text());scene=p/'scene-affine.bin';data=scene.read_bytes()
 if data[:4]!=b'HDS3':raise ValueError('Not HDS3')
 tc,sc=struct.unpack_from('<II',data,4);q=12
 if tc!=len(m['textures'])or sc!=len(m['sprites'])or tc>128:raise ValueError('Scene counts disagree')
 for t in m['textures']:
  w,h=struct.unpack_from('<II',data,q);q+=8
  if(w,h)!=(t['width'],t['height'])or not 0<w*h<=64*1024*1024:raise ValueError('Texture bounds mismatch')
  rgba=data[q:q+w*h*4];q+=w*h*4
  if len(rgba)!=w*h*4 or hashlib.sha256(rgba).hexdigest()!=t['sha256']:raise ValueError('Texture hash mismatch')
 for i in range(sc):
  row=struct.unpack_from('<I8f4f4fII',data,q);q+=76
  if row[0]>=tc or not all(math.isfinite(v)for v in row[1:17]):raise ValueError('Invalid sprite record')
  if row[-2]not in(*range(14),255):raise ValueError('Unknown blend enum')
 if q!=len(data):raise ValueError('Scene trailing/truncated bytes')
 count=0
 for c in clips.values():
  if c['missing_frames']:raise ValueError('Incomplete animation clip')
  for fr in c['frames']:
   b=(p/fr['rgba_path']).read_bytes()
   if len(b)!=fr['width']*fr['height']*4 or hashlib.sha256(b).hexdigest()!=fr['sha256']:raise ValueError('Frame hash/length mismatch')
   count+=1
 originals=m['all_original_objects'];ids=[r['object']['Id']for r in originals]
 if len(ids)!=len(set(ids))or set(ids)!={r['id']for r in coverage['records']}or sum(coverage['status_counts'].values())!=len(ids):raise ValueError('Coverage not exhaustive')
 if any(r['map_record']is None for r in coverage['records']):raise ValueError('Map ranges missing')
 report={'format':'HDS3','texture_count':tc,'sprite_count':sc,'scene_bytes':len(data),'scene_sha256':hashlib.sha256(data).hexdigest(),'sprite_record_bytes':76,'clips':len(clips),'frames':count,'all_frame_rgba_sizes_and_hashes_valid':True,'exact_file_length':True,'actual_room_objects':len(ids),'coverage_exhaustive':True,'original_bgra_bytes_retained':all('ColorStoredBGRA'in r['object']for r in originals)}
 (p/'scene-validation.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('directory',type=Path);a=p.parse_args();print(json.dumps(validate(a.directory)))
