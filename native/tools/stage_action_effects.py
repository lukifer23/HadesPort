"""Bounded action FX staging, callable from native packaging (no builds)."""
import copy,hashlib,json,math,shutil,struct,zlib
from pathlib import Path

def resolved_movie_definition(records,name,stack=()):
 if name in stack or name not in records:raise ValueError('Original movie inheritance unavailable/cyclic: '+name)
 own=records[name];parent=own.get('InheritFrom');d=copy.deepcopy(resolved_movie_definition(records,parent,stack+(name,)))if parent and parent!='null'else{}
 # Native inheritance adds CreateAnimations while a source Slides field
 # replaces the inherited vector; Clear* acts before this record's assignment.
 if own.get('ClearSlides'):d['Slides']=[]
 if own.get('ClearCreateAnimations'):d['CreateAnimations']=[]
 creates=d.get('CreateAnimations',[]);d.update(copy.deepcopy(own))
 if 'CreateAnimations'in own:d['CreateAnimations']=creates+copy.deepcopy(own['CreateAnimations'])
 if stack:return d
 d.setdefault('StartFrame',1);d.setdefault('NumAngles',1);n=d.get('NumFrames',1);d.setdefault('EndFrame',n+d['StartFrame']-1)
 if d.get('Type')!='Book'or not d.get('VideoTexture')or d.get('FrameDataFile')or any(type(v)is not int for v in[n,d['StartFrame'],d['EndFrame'],d['NumAngles']])or not 1<=n<=1000 or d['StartFrame']!=1 or d['EndFrame']!=n or d['NumAngles']!=1:raise ValueError('Original embedded movie must retain a complete authored single-angle Book window: '+name)
 if d.get('OriginX',0)!=0 or d.get('OriginY',0)!=0 or not math.isfinite(d.get('Scale',1))or d.get('Scale',1)<=0:raise ValueError('Original embedded movie explicit origin/scale unsupported: '+name)
 return d

def _verified(row):
 p=Path(row['path'])
 if p.stat().st_size!=row['bytes']or hashlib.file_digest(p.open('rb'),'sha256').hexdigest()!=row['sha256']:raise ValueError('Original action movie source identity changed: '+str(p))
 return p

def validate_embedded_movies(report,catalog):
 definitions=catalog.get('EmbeddedMovieDefinitions');bank=report.get('embedded_movie_bank');source=report.get('embedded_movie_source_manifest')
 if definitions is None:
  if bank is not None or source is not None or catalog.get('EmbeddedMovieBankSha256')is not None:raise ValueError('Embedded action movie identity without definitions')
  return None
 if not isinstance(definitions,dict)or not definitions or not bank or not source or catalog.get('EmbeddedMovieBankSha256')!=bank['sha256']:raise ValueError('Embedded movie definition/packet identities missing')
 if set(definitions)&(set(catalog.get('Definitions',{}))|set(catalog.get('MovieDefinitions',{}))):raise ValueError('Conflicting original action image backends')
 packet=_verified(bank);manifest=json.loads(_verified(source).read_text());proof=manifest.get('prefix_binding',{})
 if manifest.get('schema')!='hades-original-actor-animations-v1'or manifest.get('packet')!=bank or proof.get('schema')!='hades-action-movie-prefix-v1'or set(proof.get('windows',{}))!=set(definitions):raise ValueError('Original embedded movie manifest/prefix identities differ')
 for row in manifest['sources']:_verified(row)
 decoder=_verified(proof['decoder']);reference=json.loads(_verified(proof['decoder_reference']).read_text());_verified(proof['alpha_source']);_verified(proof['alpha_patch'])
 if decoder.stat().st_size!=reference['decoder']['bytes']or proof['decoder']['sha256']!=reference['decoder']['sha256']or manifest['decoder']!=proof['decoder']or proof.get('source_variant')!='KB2j'or proof.get('pixel_format')!='rgba':raise ValueError('Embedded original movie decoder proof differs')
 if 'c->has_alpha = !!(c->flags & BINK_FLAG_ALPHA);'not in Path(proof['alpha_source']['path']).read_text()or '+    c->has_alpha = !!(c->flags & BINK_FLAG_ALPHA);'not in Path(proof['alpha_patch']['path']).read_text():raise ValueError('Embedded movie alpha normalization proof missing')
 for row in [proof['decoder'],proof['decoder_reference'],proof['alpha_source'],proof['alpha_patch']]:
  if row not in manifest['sources']:raise ValueError('Embedded movie decoder proof not source-bound')
 from build_room_scene import Sjson
 records={}
 for row in manifest['sources']:
  p=Path(row['path'])
  if p.suffix.lower()=='.sjson'and p.parent.name=='Animations':
   for d in Sjson(p.read_bytes().decode('utf-8-sig')).parse().get('Animations',[]):
    if d.get('Name')in records:raise ValueError('Duplicate original movie animation source')
    if 'Name'in d:records[d['Name']]=d
 from hades_assets import Reader
 from stage_screen_animations import _packet,_same
 count=manifest['statistics']['movie_frames'];clips_count=manifest['statistics']['clips']
 if not 0<count<=65536 or not 0<clips_count<=4096 or bank['bytes']>512*1024*1024:raise ValueError('Embedded movie bank outside native bounds')
 images,clips,raw=_packet(packet,count,clips_count)
 if set(clips)!=set(definitions)or raw!=manifest['statistics']['cropped_raw_bytes']:raise ValueError('Embedded original movie bank partition differs')
 metadata={r['index']:r for r in manifest['frames']}
 if set(metadata)!=set(range(count)):raise ValueError('Embedded movie frame metadata partition differs')
 for at,image in enumerate(images):
  row=metadata[at]
  if row['canvas']!=list(image[:2])or row['crop']!=list(image[2:6])or row['movie_frame0']!=image[6]or row['original_angle']!=image[7]or row['origin']!=list(image[8:10])or(row['raw_bytes'],row['compressed_bytes'],row['crc32'])!=tuple(image[10:13]):raise ValueError('Embedded original movie frame geometry/payload metadata differs')
 # Validate every small-companion compressed payload using the same bounded
 # extent/CRC rules as native lazy decode, plus the producer's original crop hash.
 with packet.open('rb')as stream:
  stream.read(12)
  for at,image in enumerate(images):
   stream.read(52);blob=stream.read(image[11]);decoder=zlib.decompressobj();pixels=decoder.decompress(blob,image[10]+1)
   if len(pixels)!=image[10]or not decoder.eof or decoder.unconsumed_tail or decoder.unused_data or zlib.crc32(pixels)!=image[12]or hashlib.sha256(pixels).hexdigest()!=metadata[at]['crop_rgba_sha256']:raise ValueError('Embedded original movie compressed pixel extent/CRC/source hash differs')
 byname={c['name']:c for c in manifest['clips']};all_indices=set()
 for name,d in definitions.items():
  if d!=resolved_movie_definition(records,name)or byname[name]['definition']!=d:raise ValueError('Embedded movie source definition changed: '+name)
  binding=proof['windows'][name];video=d['VideoTexture'];n=d['NumFrames']
  if not isinstance(video,str)or not video or Path(video).name!=video or '\\'in video or '..'in video:raise ValueError('Embedded movie source name outside bounds')
  def selected_source(key,suffix):
   rows=[r for r in manifest['sources']if r['sha256']==binding[key]and Path(r['path']).name==video+suffix]
   if len(rows)!=1:raise ValueError('Embedded original movie/atlas identity not unique')
   return Path(rows[0]['path'])
  movie=selected_source('source_sha256','.bik');atlas=selected_source('atlas_sha256','.bik_atlas')
  with movie.open('rb')as f:header=f.read(44)
  r=Reader(atlas.read_bytes());version=r.integer();original_name=r.smallstring();w,h,ow,oh=[r.integer()for _ in range(4)];scale=r.floating()
  if len(header)!=44:raise ValueError('Embedded original movie header truncated')
  actual=struct.unpack_from('<I',header,8)[0]
  if len(header)!=44 or header[:4]!=b'KB2j'or version!=3 or r.pos!=len(r.data)or original_name!=video or struct.unpack_from('<II',header,20)!=(w,h)or not 0<scale<=8 or min(ow,oh)<=0:raise ValueError('Embedded original carrier/atlas geometry changed')
  if binding!={'video_texture':video,'source_sha256':hashlib.file_digest(movie.open('rb'),'sha256').hexdigest(),'atlas_sha256':hashlib.file_digest(atlas.open('rb'),'sha256').hexdigest(),'source_frame_count':actual,'authored_frame_count':n,'exported_source_frames':list(range(1,n+1)),'unused_tail_frames':actual-n,'canvas':[w,h],'original_size':[ow,oh],'atlas_scale':scale}or actual<n:raise ValueError('Embedded movie authored prefix/carrier proof differs')
  clip=clips[name];speed=d.get('PlaySpeed',0)or 60;offset=[d.get('OffsetX',0),d.get('OffsetY',0)];origin=[ow/2-math.trunc((ow-w)/2),oh/2-math.trunc((oh-h)/2)]
  flags=['Loop','HoldLastFrame','AllowRestart','CancelOnOwnerMove','DisableOwnerWeapons','DisableOwnerRotation','DisableOwnerInput','DisableOwnerManualInteract','OwnerHasNoCollision'];expected_flags=512|sum(int(bool(d.get(key,False)))<<i for i,key in enumerate(flags))
  if clip['angles']!=[0]or clip['original_angles']!=1 or clip['flags']!=expected_flags or not _same(clip['scale'],d.get('Scale',1)/scale)or not all(_same(a,b)for a,b in zip(clip['offset'],offset))or not _same(clip['seconds'],1/speed)or len(clip['sequence'])!=n:raise ValueError('Embedded movie Book geometry/timing differs from source')
  for number,step in enumerate(clip['sequence'],1):
   if step['original_frame']!=number or step['ticks']!=1 or step['events']!={}or len(step['frame_indices'])!=1:raise ValueError('Embedded movie Book schedule differs')
   at=step['frame_indices'][0];all_indices.add(at);image=images[at]
   if list(image[:2])!=[w,h]or image[6]!=number-1 or image[7]!=0 or not all(_same(a,b)for a,b in zip(image[8:10],origin)):raise ValueError('Embedded movie original frame order/pivot differs')
 if all_indices!=set(range(count)):raise ValueError('Embedded movie bank contains unbound frames')
 return {'definitions':len(definitions),'original_frames':count,'bytes':bank['bytes'],'decoded_rgba_bytes':raw,'sha256':bank['sha256']}

def stage_action_effects(manifest_path,assets):
 manifest_path=Path(manifest_path);assets=Path(assets);report=json.loads(manifest_path.read_text())
 if report['schema']!='hades-action-effect-resources-v1' or report['target_sha256']!='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254':raise ValueError('Original action effect manifest target mismatch')
 for item in [report['packet'],report['catalog'],report['lua'],*report['sources']]:
  p=Path(item['path'])
  if hashlib.file_digest(p.open('rb'),'sha256').hexdigest()!=item['sha256']:raise ValueError('Original action effect source identity changed: '+str(p))
 catalog=json.loads(Path(report['catalog']['path']).read_text())
 if not catalog.get('FrameMetadata')or catalog['ImageBankSha256']!=report['packet']['sha256']:raise ValueError('Original action effect atlas metadata/hash unavailable')
 embedded=validate_embedded_movies(report,catalog)
 if embedded:
  shutil.copyfile(report['embedded_movie_bank']['path'],assets/'action-movie-frames.bin');(assets/'action-movie-sha256.bin').write_bytes(bytes.fromhex(report['embedded_movie_bank']['sha256']))
 for key,name in [('packet','action-effect-frames.bin'),('lua','action-effects.lua')]:shutil.copyfile(report[key]['path'],assets/name)
 (assets/'action-effect-sha256.bin').write_bytes(bytes.fromhex(report['packet']['sha256']))
 return {'definitions':len(catalog['Definitions']),'original_frames':len(catalog['FrameMetadata']),'bytes':report['packet']['bytes'],**({'embedded_movies':embedded}if embedded else{})}
