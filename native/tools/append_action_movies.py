#!/usr/bin/env python3.13
"""Bind covered original Book movies in a fresh small action companion bank.

Existing atlas/actor banks stay immutable. A complete authored single-angle Book
window may be a prefix of its original carrier; no tail frames are invented,
repeated or substituted. Publication follows validation of actual source proofs.
"""
import argparse,copy,hashlib,json,os,shutil,tempfile
from pathlib import Path
from types import SimpleNamespace
from build_room_scene import identity
from export_actor_animations import export as export_movies
from export_effect_initialization import lua
from export_action_effects import load_records
from stage_action_effects import resolved_movie_definition,validate_embedded_movies


def verified(row):
 p=Path(row['path'])
 if p.stat().st_size!=row['bytes']or hashlib.file_digest(p.open('rb'),'sha256').hexdigest()!=row['sha256']:raise ValueError('Action movie source identity changed: '+str(p))
 return p


def export(a):
 final=a.output.resolve()
 if final.exists():raise ValueError('Fresh action/movie output required')
 base=json.loads(a.base_manifest.read_text());catalog=json.loads(verified(base['catalog']).read_text());verified(base['lua']);packet=verified(base['packet']);coverage=json.loads(verified(base['coverage']).read_text())
 if base.get('schema')!='hades-action-effect-resources-v1'or coverage.get('schema')!='hades-animation-coverage-v1'or base.get('target_sha256')!=coverage.get('target_sha256'):raise ValueError('Action movie base/coverage identity mismatch')
 reference=json.loads(a.decoder_reference.read_text());decoder=identity(a.decoder or Path(reference['decoder']['path']))
 if decoder['sha256']!=reference['decoder']['sha256']or decoder['bytes']!=reference['decoder']['bytes']:raise ValueError('Action movie decoder differs from retained verified decoder')
 alpha=Path(decoder['path']).parent/'libavcodec/bink2.c';patch=Path(__file__).with_name('bink2-alpha-normalization.patch')
 if not alpha.is_file()or 'c->has_alpha = !!(c->flags & BINK_FLAG_ALPHA);'not in alpha.read_text():raise ValueError('Original Bink2 alpha normalization source unavailable')
 records,origins,sources=load_records(a.content);definitions={}
 names=list(dict.fromkeys(a.names))
 if len(names)!=len(a.names):raise ValueError('Duplicate requested action movie name')
 for name in names:
  if name not in coverage.get('banks',{}).get('actor_bink',[])or not coverage.get('provenance',{}).get(name):raise ValueError('Movie name is not reached by original selected coverage: '+name)
  if any(name in catalog.get(key,{})for key in ['Definitions','MovieDefinitions','EmbeddedMovieDefinitions']):raise ValueError('Movie animation already bound: '+name)
  definitions[name]=resolved_movie_definition(records,name)
 if catalog.get('EmbeddedMovieDefinitions'):raise ValueError('Cumulative embedded movie append requires preserving an existing companion; use an unbound base')
 final.parent.mkdir(parents=True,exist_ok=True)
 with tempfile.TemporaryDirectory(prefix='.'+final.name+'-',dir=final.parent)as temporary:
  work=Path(temporary)/'pack';work.mkdir();decoded=Path(temporary)/'decode'
  args=SimpleNamespace(content=a.content,decoder=Path(decoder['path']),output=decoded,sources=sources,names=names,existing_names=set(catalog['Definitions'])|set(catalog.get('MovieDefinitions',{}))|set(names),unit_scale=1.,directions=1,movie_directory=a.content/'Movies/720p',allow_book_movie_prefix=True)
  export_movies(args);movies=json.loads((decoded/'manifest.json').read_text())
  if {c['name']for c in movies['clips']}!=set(names):raise ValueError('Original action movie decode escaped requested covered closure')
  target=work/'action-movie-frames.bin';shutil.move(movies['packet']['path'],target);movies['packet']=identity(target)
  movie_map={row['name']:row for row in movies['movies']};windows={}
  for clip in movies['clips']:
   name=clip['name'];d=definitions[name];exported=copy.deepcopy(clip['definition']);exported.setdefault('StartFrame',1);exported.setdefault('EndFrame',exported.get('NumFrames',1));exported.setdefault('NumAngles',1)
   if exported!=d:raise ValueError('Movie exporter/source native inheritance differs: '+name)
   clip['definition']=copy.deepcopy(d);info=movie_map[clip['movie']];original=a.content/'Movies/720p'/(d['VideoTexture']+'.bik')
   windows[name]={'video_texture':d['VideoTexture'],'source_sha256':identity(original)['sha256'],'atlas_sha256':identity(original.with_suffix('.bik_atlas'))['sha256'],'source_frame_count':info['actual_movie_frames'],'authored_frame_count':d['NumFrames'],'exported_source_frames':list(range(1,d['NumFrames']+1)),'unused_tail_frames':info['actual_movie_frames']-d['NumFrames'],'canvas':[info['width'],info['height']],'original_size':[info['original_width'],info['original_height']],'atlas_scale':info['atlas_scale']}
  proof={'schema':'hades-action-movie-prefix-v1','decoder':decoder,'decoder_reference':identity(a.decoder_reference),'alpha_source':identity(alpha),'alpha_patch':identity(patch),'source_variant':'KB2j','pixel_format':'rgba','windows':windows,'native_window':'AnimationData.PostRead978a0..978b1 sets absent EndFrame from explicit NumFrames; Book.Init/Update use source Start/End, not carrier count.'}
  additions=[decoder,proof['decoder_reference'],proof['alpha_source'],proof['alpha_patch']]
  movies['sources']+= [row for row in additions if row not in movies['sources']];movies['prefix_binding']=proof
  movie_manifest=work/'action-movies.json';movie_manifest.write_text(json.dumps(movies,indent=2)+'\n')
  catalog['EmbeddedMovieDefinitions']=definitions;catalog['EmbeddedMovieBankSha256']=movies['packet']['sha256']
  for name in names:catalog['Rejected'].pop(name,None)
  os.link(packet,work/packet.name);(work/'catalog.json').write_text(json.dumps(catalog,indent=1)+'\n');(work/'action-effects.lua').write_text('return '+lua(catalog)+'\n')
  report=copy.deepcopy(base);report['packet']=identity(work/packet.name);report['catalog']=identity(work/'catalog.json');report['lua']=identity(work/'action-effects.lua');report['embedded_movie_bank']=movies['packet'];report['embedded_movie_source_manifest']=identity(movie_manifest);report['base_action_manifest']=identity(a.base_manifest);report['embedded_movie_definitions']=sorted(definitions);report['statistics']['embedded_movie_definitions']=len(definitions);report['statistics']['rejected']=len(catalog['Rejected'])
  for row in movies['sources']+[report['base_action_manifest']]:
   if row not in report['sources']:report['sources'].append(row)
  validate_embedded_movies(report,catalog)
  # Paths move only inside the fresh publication; original source paths and
  # decoder identities stay exact. Neither previous bank is ever modified.
  movies['packet']['path']=str(final/target.name);movie_manifest.write_text(json.dumps(movies,indent=2)+'\n')
  for key in ['packet','catalog','lua','embedded_movie_bank']:report[key]['path']=str(final/Path(report[key]['path']).name)
  report['embedded_movie_source_manifest']=identity(movie_manifest);report['embedded_movie_source_manifest']['path']=str(final/movie_manifest.name)
  (work/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
  if final.exists():raise ValueError('Fresh action movie publication raced with existing output')
  os.rename(work,final)
 print(json.dumps({'output':str(final),'embedded_movie_definitions':len(definitions),'embedded_frames':movies['statistics']['movie_frames'],'embedded_raw_bytes':movies['statistics']['cropped_raw_bytes'],'embedded_packet':report['embedded_movie_bank'],'atlas_bytes_preserved':True}),flush=True)
 return report


if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for key in ['base-manifest','decoder-reference','content','output']:p.add_argument('--'+key,type=Path,required=True)
 p.add_argument('--decoder',type=Path);p.add_argument('--names',nargs='+',required=True)
 export(p.parse_args())
