#!/usr/bin/env python3
"""Export original Dust/Bat/Ember atlas frames without simulating particles."""
import argparse, hashlib, json, struct, zlib
from pathlib import Path
from build_room_scene import identity
from hades_assets import Package, xnb_image
from export_actor_animations import string

def room_leaf_names(registry,definitions):
    """Leaf images reachable from this room's map primary spawners' VisualFx
    through registry dependency edges (Random/VisualFx/ChainTo...). RoomOpening
    yields exactly the previously reviewed DustFallA-G/TartarusEmber*/EnvironmentBat set."""
    edges={}
    for e in registry['dependency_edges']:edges.setdefault(e['from'],[]).append(e['to'])
    seen=set();stack=[x['visual_fx'] for x in registry['map_primary_spawners']]
    while stack:
        n=stack.pop()
        if n in seen:continue
        if n not in definitions:raise ValueError('Spawner leaf dependency missing from registry: '+n)
        seen.add(n);stack+=edges.get(n,[])
    names=sorted(n for n in seen if definitions[n]['Type'] in ('Constant','Book'))
    if not names:raise ValueError('Room spawners reach no leaf images')
    return names

def export(a):
    if a.output.exists():raise ValueError('Fresh output required')
    registry=json.loads(a.registry.read_text())
    if registry['target_sha256']!='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254':raise ValueError('Native source target identity mismatch')
    definitions={r['name']:r['definition'] for r in registry['definitions']}
    names=room_leaf_names(registry,definitions)
    wanted=set();clipnames={}
    for name in names:
        d=definitions[name];angles=d.get('NumAngles',1);frames=d['NumFrames']
        if d['Type'] not in ('Constant','Book') or angles not in (1,32) or frames not in (1,13):raise ValueError('Unreviewed original leaf shape')
        clipnames[name]=[[d['FilePath']+f'{angle*frames+frame:04}' if frames>1 else d['FilePath'] for angle in range(angles)]for frame in range(1,frames+1)]
        wanted.update(s for row in clipnames[name]for s in row)
    packages=a.content/'Win/Packages';manifest=packages/'Fx.pkg_manifest';matches={}
    for e in Package(manifest).entries():
        for s in e.get('sprites',[]):
            if s['name'] in wanted:
                if s['name'] in matches:raise ValueError('Ambiguous original leaf image')
                matches[s['name']]={'sprite':s,'atlas':e['name']}
    if set(matches)!=wanted:raise ValueError('Missing original leaf images '+repr(wanted-set(matches)))
    atlases={};payloads=[]
    for e in Package(packages/'Fx.pkg').entries():
        if e.get('name')in {m['atlas']for m in matches.values()}:
            if e['name']in atlases:raise ValueError('Duplicate source atlas')
            atlases[e['name']],info=xnb_image(e['data'])
            payloads.append({'atlas':e['name'],'xnb_sha256':hashlib.sha256(e['data']).hexdigest(),'location':e['location'],'decoded':info})
    a.output.mkdir(parents=True);frames=[];lookup={};rawbytes=0
    for name,m in sorted(matches.items()):
        s=m['sprite'];x,y,w,h=s['rect'];im=atlases[m['atlas']]
        if min(x,y,w,h)<0 or x+w>im.width or y+h>im.height or s['scale_ratio']!=[1.,1.]:raise ValueError('Unreviewed leaf atlas geometry')
        raw=im.crop((x,y,x+w,y+h)).tobytes();lookup[name]=len(frames);rawbytes+=len(raw)
        angle=int(name[-4:])-1 if name.startswith('Fx\\BasicBat\\BasicBat')else 0
        angle=angle//13 if name.startswith('Fx\\BasicBat\\BasicBat')else 0
        frames.append({'name':name,**m,'width':w,'height':h,'angle':angle,'rgba':raw,'packed':zlib.compress(raw,6),'origin':[s['original'][0]/2-s['trim'][0],s['original'][1]/2-s['trim'][1]],'rgba_sha256':hashlib.sha256(raw).hexdigest()})
    packet=a.output/'room-leaf-frames.bin';clips=[]
    with packet.open('xb')as out:
        out.write(b'HAA1'+struct.pack('<II',len(frames),len(names)))
        for f in frames:
            w,h=f['width'],f['height'];out.write(struct.pack('<8I2f',w,h,0,0,w,h,0,f['angle'],*f['origin']));out.write(struct.pack('<III',len(f['rgba']),len(f['packed']),zlib.crc32(f['rgba'])));out.write(f['packed'])
        for name in names:
            d=definitions[name];angles=d.get('NumAngles',1);rows=clipnames[name]
            for s in [name,'','']:string(out,s)
            out.write(struct.pack('<4I4f',angles,angles,512 if d['Type']=='Book'else 0,len(rows),1.,0.,0.,1/60));out.write(struct.pack('<'+'I'*angles,*range(angles)))
            ids=[]
            for i,row in enumerate(rows,1):
                frameids=[lookup[s]for s in row];ids.append(frameids);out.write(struct.pack('<II',i,1));out.write(struct.pack('<'+'I'*angles,*frameids));string(out,'{}')
            clips.append({'name':name,'definition':d,'original_angles':angles,'angle_indices':list(range(angles)),'frame_indices':ids})
    sha=hashlib.sha256(packet.read_bytes()).digest();(a.output/'room-leaf-sha256.bin').write_bytes(sha)
    original_facing=a.facing.read_bytes()
    if hashlib.sha256(original_facing).hexdigest()!='a14a705185b3f47e70fca3ef7f2d54831619c162544c4a81376a08f720c3ceba':raise ValueError('Exact retained native facing table required')
    if len(original_facing)<2924 or original_facing[:4]!=b'HAF1' or struct.unpack_from('<I',original_facing,40)[0]!=720:raise ValueError('Missing exact retained original angle table')
    facing=a.output/'room-leaf-facing.bin'
    with facing.open('xb')as out:
        out.write(b'HAF1'+sha+struct.pack('<II',len(clips),720)+original_facing[44:44+2880])
        for clip in clips:
            # This image bank consumes actual animation body heading supplied
            # by the particle provider, not raw attached Thing facing. Native
            # randomized source angle has already entered that heading.
            d=clip['definition']
            out.write(struct.pack('<IffiII',d.get('UseIsometricAngle',True),0.,0.,0,d.get('Mirror',False),False))
    report={'schema':'hades-original-room-leaf-frames-v1','packet':identity(packet),'facing':identity(facing),'identity':identity(a.output/'room-leaf-sha256.bin'),'registry':identity(a.registry),'sources':[identity(manifest),identity(packages/'Fx.pkg'),identity(a.facing)],'source_payloads':payloads,'clips':clips,'frames':[{k:v for k,v in f.items()if k not in ('rgba','packed')}for f in frames],'raw_bytes':rawbytes,'limits':['Original atlas pixels/pivots only; no particle timing, motion, RNG, sound or lifetime execution supplied by this bank.','All32 original Bat source directions retained; no synthesized angle frames.','Facing helper requires actual particle animation heading after native owner/source angle initialization. Raw attached Thing facing is insufficient.','Nominal one-tick bank schedules are indexing only; Constant duration and Book play speed/LoopFramesOnly remain native live components.']}
    (a.output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'packet':report['packet'],'facing':report['facing'],'frames':len(frames),'clips':len(clips),'raw_bytes':rawbytes}))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for k in ['content','registry','facing','output']:p.add_argument('--'+k,type=Path,required=True)
    export(p.parse_args())
