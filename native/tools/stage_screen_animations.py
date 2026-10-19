"""Validate and stage the original screen movie/rectangle bank without building."""
import hashlib,json,shutil,struct
from pathlib import Path
# ActorAnimationBank::load (native/app/actor_animation.cpp) admits seekable HAA1
# packets through 512 MiB, with bounded frame/clip counts and lazy frame decode.
MAX_SCREEN_PACKET_BYTES=512*1024*1024
def stage_screen_animations(manifest_path,assets):
 m=json.loads(Path(manifest_path).read_text());assets=Path(assets)
 items=[m['packet'],m['screen_catalog'],*m['sources']]
 if 'independent_movie_predecode'in m:items.append(m['independent_movie_predecode'])
 for item in items:
  p=Path(item['path'])
  if p.stat().st_size!=item['bytes']or hashlib.file_digest(p.open('rb'),'sha256').hexdigest()!=item['sha256']:raise ValueError('Original screen resource/source changed: '+str(p))
 base={'RemBGIntro','RemBG','Backgrounds\\rectangle_01','Blank','VignetteOverlay'}
 count,frames=m['statistics']['clips'],m['statistics']['movie_frames'];names=[c['name']for c in m['clips']]
 expected=base if(count,frames)==(5,133)else(base|{'RoomTransitionIn','RoomTransitionOut'}if(count,frames)==(7,153)else None)
 gui=m.get('gui_review')
 if gui:
  # Reviewed original HUD closure (screen_gui_review.py): every admitted clip
  # is a reviewed name, its ChildAnimation closure, or a Thing.Graphic path.
  admitted=set(gui['reviewed_animations'])|{e['child']for e in gui['closure_edges']}|set(gui['reviewed_graphics'])
  expected=base|{'RoomTransitionIn','RoomTransitionOut'}|admitted
  if count!=len(expected)or frames!=153+m['statistics']['gui_original_images']:expected=None
 if expected is None or set(names)!=expected or len(names)!=len(expected):raise ValueError('Unexpected bounded screen original source closure')
 if m['packet']['bytes']>MAX_SCREEN_PACKET_BYTES or not 0<frames<=65536 or not 0<count<=4096:raise ValueError('Screen original packet exceeds native HAA1 size/count bounds')
 with Path(m['packet']['path']).open('rb')as f:header=f.read(12)
 if header[:4]!=b'HAA1'or struct.unpack_from('<II',header,4)!=(frames,count):raise ValueError('Screen source packet/header differs from validated closure')
 if count>=7:
  out=next(c for c in m['clips']if c['name']=='RoomTransitionOut')
  if not out['definition'].get('PlayBackwards')or out['definition'].get('PlaySpeed')!=30 or [r['original_frame']for r in out['source_sequence']]!=list(range(20,0,-1)):raise ValueError('Original transition backward20-frame schedule changed')
 shutil.copyfile(m['packet']['path'],assets/'screen-animation-frames.bin');shutil.copyfile(m['screen_catalog']['path'],assets/'screen-animations.lua');(assets/'screen-animation-sha256.bin').write_bytes(bytes.fromhex(m['packet']['sha256']))
 gui_images=m['statistics'].get('gui_original_images',0)
 return {'packet_sha256':m['packet']['sha256'],'bytes':m['packet']['bytes'],'original_movie_frames':frames-3-gui_images,'original_static_images':3,'original_gui_images':gui_images,'gui_clips':count-(7 if count>5 else 5)}
