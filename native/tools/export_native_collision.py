#!/usr/bin/env python3.13
"""Export source/evidence-backed static original obstacle collision polygons.

No actor creation, collision solver, runtime RNG, or original game execution.
Unknown/dynamic inputs are retained explicitly and excluded from HCG1.
"""
import argparse,collections,hashlib,json,math,struct
from pathlib import Path
from build_room_scene import identity
ROOT=Path(__file__).resolve().parents[2]
EXPECTED_DLL='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'
DEG=struct.unpack('<f',bytes.fromhex('35fa8e3c'))[0] # 0.01745329238474369
TAU=struct.unpack('<f',bytes.fromhex('db0fc940'))[0] # native 2pi literal

def f32(v):
 v=float(v)
 if not math.isfinite(v):raise ValueError('Nonfinite source transform')
 return struct.unpack('<f',struct.pack('<f',v))[0]
def add(a,b):return f32(a+b)
def sub(a,b):return f32(a-b)
def mul(a,b):return f32(a*b)
def native_add_angle(current,delta):
 if delta==0:return current
 summed=add(current,delta)
 return sub(summed,mul(math.floor(f32(summed/TAU)),TAU))
def native_ccw(points):
 # Native IsCounterClockwise counts signed local turns and returns signbit of
 # signed vote (negative means CCW in engine's downward-Y world convention).
 votes=0
 for i,(x,y)in enumerate(points):
  bx,by=points[(i+1)%len(points)];cx,cy=points[(i+2)%len(points)]
  cross=sub(mul(sub(bx,x),sub(cy,by)),mul(sub(by,y),sub(cx,bx)))
  votes+=1 if cross>0 else -1
 return votes<0

def transform(points,loc,scale,skew,skew_angle,flip_h,flip_v,rotation):
 # Native constructs world-space polygon first, then mutates around location.
 # Float32 rounds each SSE scalar arithmetic stage. Host sin/cos rounding may
 # differ by ulps from Windows CRT; no bit-for-bit transcendental claim.
 ox,oy=loc;p=[[f32(x),f32(y)]for x,y in points]
 if not native_ccw(p):p.reverse()
 p=[[add(ox,x),add(oy,y)]for x,y in p]
 if skew!=1:
  co,si=f32(math.cos(skew_angle)),f32(-math.sin(skew_angle));factor=sub(skew,1)
  q=[]
  for x,y in p:
   dot=add(mul(mul(sub(x,ox),factor),co),mul(mul(sub(y,oy),factor),si))
   q.append([add(x,mul(dot,co)),add(y,mul(dot,si))])
  p=q
 if scale!=1:p=[[add(ox,mul(sub(x,ox),scale)),add(oy,mul(sub(y,oy),scale))]for x,y in p]
 if flip_h:p=[[add(ox,-sub(x,ox)),add(oy,sub(y,oy))]for x,y in p][::-1]
 if flip_v:p=[[add(ox,sub(x,ox)),add(oy,-sub(y,oy))]for x,y in p][::-1]
 if rotation!=0:
  co,si=f32(math.cos(rotation)),f32(math.sin(rotation));q=[]
  for x,y in p:
   dx,dy=sub(x,ox),sub(y,oy)
   q.append([add(ox,sub(mul(dx,co),mul(dy,si))),add(oy,add(mul(dy,co),mul(dx,si)))])
  p=q
 return p

def export(scene_path,creation_path,output):
 if output.exists():raise ValueError('Output must be new')
 scene=json.loads(scene_path.read_text());creation=json.loads(creation_path.read_text())
 if scene['room']!=creation['room']or creation['attachment_errors']:raise ValueError('Inconsistent map metadata')
 defaults_path=ROOT/'.local/reports/native-collision-compiled-defaults.json';defaults=json.loads(defaults_path.read_text())
 dll=ROOT/'.local/game/base/x64/EngineWin64s.dll'
 if defaults['target_sha256']!=EXPECTED_DLL or hashlib.sha256(dll.read_bytes()).hexdigest()!=EXPECTED_DLL:raise ValueError('Native default identity mismatch')
 # Source identities are checked, not merely copied into a new manifest.
 source_checks=[]
 for src in [creation['map_source'],*creation['definition_sources']]:
  path=Path(src['path']);actual=identity(path)
  if actual['sha256']!=src['sha256']:raise ValueError('Original source changed '+str(path))
  source_checks.append(actual)
 definitions=creation['definitions'];records={r['id']:r for r in creation['records']}
 objects=scene['all_original_objects'];ids=[e['object']['Id']for e in objects]
 if len(ids)!=len(set(ids))or set(ids)!=set(records):raise ValueError('Map object identity mismatch')
 stats=collections.Counter();candidates=[];anchors=[];rejected=[]
 relevant=['Scale','MinScale','MaxScale','StartAngle','RelativeExtentScale','ExtentScale','RotateGeometry','MirrorGeometry','StopsUnits']
 def resolved_field(definition,name):
  prop='Thing.'+name;thing=definition['value'].get('Thing',{})
  if name in thing:
   origin=definition['property_provenance'][prop];source=definitions[origin]['source']
   return {'value':thing[name],'provenance':{'kind':'original_source','property_path':prop,'definition_key':origin,'source':source}}
  d=defaults['thing_fields'][name]
  return {'value':d['value'],'provenance':{'kind':'compiled_native_default','report':str(defaults_path.relative_to(ROOT)),**d['provenance']}}
 for entry in objects:
  o=entry['object'];name=o['Name'];rid=o['Id'];stats['map_objects']+=1
  if name in ('_PlayerUnit','HeroStart','HeroEnd','HeroExit','CameraIntroStart','CameraIntroEnd','CameraClamp'):
   anchors.append({'name':name,'id':rid,'location':o['Location'],'source_range':o['source_range'],'role':'source marker only; not collider/actor creation'})
  if not o['DoCreate']:stats['excluded_absent_payload']+=1;continue
  if not o['Active']:stats['excluded_inactive']+=1;continue
  if not o['Collision']:stats['excluded_instance_collision_false']+=1;continue
  if o['DataType']!=1:stats['excluded_nonobstacle']+=1;continue
  key='Obstacles/'+name
  if key not in definitions:raise ValueError('Missing obstacle definition '+key)
  definition=definitions[key];thing=definition['value'].get('Thing',{})
  fields={n:resolved_field(definition,n)for n in relevant}
  if fields['StopsUnits']['value']is False:stats['excluded_stops_units_false']+=1;continue
  if fields['StopsUnits']['value']is not True:raise ValueError('StopsUnits is not bool')
  pts=o['Points']or[[v['X'],v['Y']]for v in thing.get('Points',[])]
  if len(pts)<3:
   stats['excluded_no_explicit_polygon']+=1
   rejected.append({'id':rid,'name':name,'reason':'No original map/definition polygon. Native fallback size/animation-bounds shape not exported.'});continue
  if len(pts)>10000:raise ValueError('Polygon point bound')
  if o['Points']:point_source={'kind':'original_map_instance','source_range':o['source_range']}
  else:
   origin=definition['property_provenance']['Thing.Points'];point_source={'kind':'original_inherited_definition','property_path':'Thing.Points','definition_key':origin,'source':definitions[origin]['source']}
  errors=[]
  if o['ActivateAtRange']:errors.append('Deferred original range activation not simulated')
  minimum,maximum=(f32(fields[n]['value'])for n in ['MinScale','MaxScale'])
  if minimum!=0 or maximum!=0:errors.append('Native definition random MinScale/MaxScale requires original RNG value/order')
  # The exact optional extent operations are proven in SetupGeometry, but this
  # room does not use them; fail closed rather than broaden unneeded behavior.
  if fields['ExtentScale']['value']!=0 or fields['RelativeExtentScale']['value']!=0:errors.append('Nonzero optional ExtentScale/RelativeExtentScale branch outside selected room scope')
  if thing.get('AttachToMarker'):errors.append('Generic dynamic attachment origin requires live parent state')
  if not all(isinstance(fields[n]['value'],bool)for n in ['RotateGeometry','MirrorGeometry']):raise ValueError('Invalid geometry flag')
  scale=mul(f32(fields['Scale']['value']),f32(o['Scale']))
  if scale<=0 or o['SkewScale']<=0:errors.append('Nonpositive scale/skew causes degenerate/reflected winding outside scope')
  angle=native_add_angle(0,f32(fields['StartAngle']['value']))
  angle=native_add_angle(angle,mul(f32(o['Angle']),DEG))
  rotate=-angle if fields['RotateGeometry']['value']else 0
  row={'id':rid,'name':name,'data_type':o['DataType'],'source_range':o['source_range'],'instance_gates':{k:o[k]for k in ['DoCreate','Active','Collision','ActivateAtRange']},'original_points':pts,'points_source':point_source,'definition_fields':fields,'location':o['Location'],'scale':o['Scale'],'angle_degrees':o['Angle'],'flip_horizontal':o['FlipHorizontal'],'flip_vertical':o['FlipVertical'],'skew_angle':o['SkewAngle'],'skew_scale':o['SkewScale'],'effective_body_scale':scale,'effective_body_angle_radians':angle,'effective_collision_rotation_radians':rotate,'world_transform_exact':not errors,'world_transform_exact_for_unskewed_zeroangle':False,'unresolved_reasons':errors,'legacy_filter_would_accept':o['Angle']==0 and o['SkewScale']==1,'numeric_policy':'Float32 stage arithmetic; native transform semantics recovered, Windows CRT sinf/cosf last-bit equality not claimed.'}
  if errors:
   row['world_points_exact']=None;stats['unresolved_polygons']+=1
  else:
   loc=[f32(o['Location'][n])for n in ['X','Y']]
   world=transform(pts,loc,scale,f32(o['SkewScale']),mul(f32(o['SkewAngle']),DEG),o['FlipHorizontal'],o['FlipVertical'],rotate)
   if not all(math.isfinite(v)for point in world for v in point):raise ValueError('Nonfinite transformed points')
   row['world_points_exact']=world;stats['deterministic_polygons']+=1
   if not row['legacy_filter_would_accept']:stats['newly_resolved_legacy_exclusions']+=1
   row['world_transform_exact_for_unskewed_zeroangle']=o['Angle']==0 and o['SkewScale']==1
  candidates.append(row)
 stats['explicit_polygon_candidates']=len(candidates)
 output.mkdir(parents=True)
 good=[r for r in candidates if r['world_transform_exact']]
 packet=output/'collision.bin'
 with packet.open('xb')as f:
  f.write(b'HCG1'+struct.pack('<I',len(good)))
  for row in good:
   f.write(struct.pack('<iI',row['id'],len(row['world_points_exact'])))
   for point in row['world_points_exact']:f.write(struct.pack('<2f',*point))
 result={'schema':'hades-native-collision-v1','room':scene['room'],'native_target':{'dll_sha256':EXPECTED_DLL,'pdb_guid':defaults['pdb_guid'],'pdb_age':1},'input_scene_manifest':identity(scene_path),'input_creation_manifest':identity(creation_path),'source_files':source_checks,'compiled_defaults':identity(defaults_path),'anchors':anchors,'collision_candidates':candidates,'excluded_without_explicit_polygon':rejected,'counts':dict(stats),'collision_packet':identity(packet),'collision_packet_format':'HCG1,u32LEpolygonCount; each i32LEoriginalMapId,u32LEpointCount,pointCount pairsf32LEx,y','contract':'native/analysis/collision-transform-contract.json','limitations':['Static original obstacle polygons gated by actual source Active/DoCreate/Collision/StopsUnits. This does not create actors or reproduce full physics.','Original random-scale instances remain unresolved; no seed or average/random scale invented.','Other native fallback shapes, dynamic attachments, transitions, motion, runtime collision masks/solver and world activation remain outside export.','Source order is serialized map order, not a claimed native manager hash traversal.','Resolved transform semantics are original native; host/Windows sinf/cosf bit equality and official runtime polygon snapshots not observed.']}
 path=output/'room-geometry.json';path.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
 print(json.dumps({'output':str(output),'counts':dict(stats),'packet_sha256':result['collision_packet']['sha256']}))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('scene_manifest',type=Path);p.add_argument('--creation',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();export(a.scene_manifest,a.creation,a.output)
