#!/usr/bin/env python3
"""Bounded original compressed movie cache assets; no decoder or game execution.

Exports exact observed preload names plus explicit Hero/Sword weapon preload
lists, retaining missing bases and optional masks separately. All files remain
private under the chosen new output directory.
"""
import argparse,hashlib,json,os,re,stat,struct,zlib
from pathlib import Path
from hades_assets import Reader
from export_actor_catalog import literal
ROOT=Path(__file__).resolve().parents[2]
TARGET='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'
def identity(path):
    with path.open('rb')as f:return {'path':str(path.resolve()),'sha256':hashlib.file_digest(f,'sha256').hexdigest(),'bytes':path.stat().st_size}
def table(text,begin):
    depth=0;at=begin;quoted=None
    while at<len(text):
        c=text[at]
        if quoted:
            if c=='\\':at+=2;continue
            if c==quoted:quoted=None
        elif text.startswith('--[[',at):
            end=text.find(']]',at+4)
            if end<0:raise ValueError('Unclosed source block comment')
            at=end+2;continue
        elif text.startswith('--',at):
            at=text.find('\n',at)
            if at<0:raise ValueError('Comment ended selected table')
            continue
        elif c in '\"\'':quoted=c
        elif c=='{':depth+=1
        elif c=='}':
            depth-=1
            if depth==0:return text[begin:at+1],(begin,at+1)
        at+=1
    raise ValueError('Unclosed original Lua declaration')
def assignment(text,key):
    matches=list(re.finditer(r'(?m)^\s*'+re.escape(key)+r'\s*=\s*\{',text))
    if len(matches)!=1:raise ValueError('Nonunique source declaration '+key)
    return table(text,text.index('{',matches[0].start()))
def array(text,key):
    body,span=assignment(text,key);clean=re.sub(r'--[^\n]*','',body)
    tokens=list(re.finditer(r'"([a-zA-Z0-9_./-]+)"|\'([a-zA-Z0-9_./-]+)\'',clean))
    residual=re.sub(r'"([a-zA-Z0-9_./-]+)"|\'([a-zA-Z0-9_./-]+)\'','',clean)
    if re.sub(r'[\s{},]','',residual):raise ValueError('Nonliteral source preload list')
    return [m[1]or m[2]for m in tokens],span
def export(content,request_log,output,max_bytes):
    if output.exists()or output.is_symlink():raise ValueError('Output must be new')
    content=content.resolve();output=output.resolve()
    if output.is_relative_to(content.parent):raise ValueError('Output cannot be inside preserved original game')
    if identity(content.parent/'x64/EngineWin64s.dll')['sha256']!=TARGET:raise ValueError('Wrong original DLL')
    requests=[];provenance=[identity(request_log)]
    for line in request_log.read_text().splitlines():
        m=re.search(r'PRELOAD_MOVIE_REQUEST cache=(.*?) index=(\d+) name=(\S+)',line)
        if m:
            if m[1]and m[1]!='WeaponCache':raise ValueError('Unsupported observed cache category')
            requests.append({'cache':m[1],'index':int(m[2]),'name':m[3]})
    if not requests or len(requests)>512:raise ValueError('No bounded actual preload request list')
    additional=[]
    for filename,owner,prop,cache in [('HeroData.lua','DefaultHero','WeaponBinks','WeaponCache'),('WeaponData.lua','SwordWeapon','WeaponBinks','WeaponCache')]:
        path=content/'Scripts'/filename;text=path.read_text(encoding='utf-8-sig');block,owner_span=assignment(text,owner);names,span=array(block,prop)
        proof={**identity(path),'owner':owner,'property':prop,'owner_character_range':owner_span,'list_relative_character_range':span,'names':names};provenance.append(proof)
        additional.extend({'cache':cache,'name':name,'source_owner':owner,'source_property':prop}for name in names)
    selected=list(dict.fromkeys(x['name']for x in requests+additional))
    if any(not re.fullmatch(r'[a-zA-Z0-9_.-]+',n)or'..'in n for n in selected):raise ValueError('Source movie name outside bounded original names')
    movies=content/'Movies';indexed={};atlas_proofs={}
    for path in sorted(movies.glob('*.bik_atlas')):
        if path.is_symlink():raise ValueError('Original movie metadata must not be symlink')
        raw=path.read_bytes();r=Reader(raw);version=r.integer();name=r.smallstring();w,h,ow,oh=r.integer(),r.integer(),r.integer(),r.integer();scale=r.floating()
        if version!=3 or r.pos!=len(raw):raise ValueError('Unmapped original movie atlas format')
        if name in indexed:raise ValueError('Duplicate original movie asset name')
        indexed[name]=path.with_suffix('.bik');atlas_proofs[name]={'metadata':identity(path),'dimensions':[w,h],'original_dimensions':[ow,oh],'scale':scale}
    output.mkdir(parents=True);assets=output/'movie-preloads';assets.mkdir();records={};missing=[];missing_masks=[];total=0
    for name in selected:
        if name not in indexed:missing.append(name);continue
        for movie,optional in [(name,False),(name+'_mask',True)]:
            if movie in records:continue
            if movie not in indexed:
                (missing_masks if optional else missing).append(movie);continue
            source=indexed[movie]
            if not source.is_file():
                (missing_masks if optional else missing).append(movie);continue
            before=source.stat(follow_symlinks=False)
            if not stat.S_ISREG(before.st_mode):raise ValueError('Movie source is not regular')
            if total+before.st_size>max_bytes:raise ValueError('Selected original movie export exceeds explicit bound')
            temp=assets/('part-'+str(len(records)));sha=hashlib.sha256();crc=0;size=0
            fd=os.open(source,os.O_RDONLY|os.O_NOFOLLOW)
            with os.fdopen(fd,'rb')as original,temp.open('xb')as copied:
                opened=os.fstat(original.fileno())
                if (opened.st_dev,opened.st_ino)!=(before.st_dev,before.st_ino):raise ValueError('Original movie identity changed before read')
                while block:=original.read(1024*1024):sha.update(block);crc=zlib.crc32(block,crc);copied.write(block);size+=len(block)
                after=os.fstat(original.fileno())
            if size!=before.st_size or (after.st_size,after.st_mtime_ns,after.st_ino)!=(before.st_size,before.st_mtime_ns,before.st_ino):raise ValueError('Original movie changed during read')
            digest=sha.hexdigest();target=assets/(digest+'.bik')
            if target.exists():temp.unlink()
            else:temp.rename(target)
            total+=size;records[movie]={'filename':source.name,'path':'movie-preloads/'+digest+'.bik','sha256':digest,'bytes':size,'crc32':crc,'original':{'path':str(source.resolve()),'sha256':digest,'bytes':size},**atlas_proofs[movie]}
    catalog={'schema':'hades-native-movie-preload-v1','mask_suffix':'_mask','compiled_stream_bytes':0x23000000,'compiled_weapon_bytes':0x09600000,'movies':{name:{k:v for k,v in rec.items()if k in('filename','path','sha256','bytes','crc32')}for name,rec in records.items()}}
    packet=output/'movie-preloads.lua';packet.write_text('return '+literal(catalog)+'\n')
    report={'schema':catalog['schema'],'target_sha256':TARGET,'packet':identity(packet),'observed_request_log':identity(request_log),'observed_requests':requests,'ahead_of_call_source_lists':additional,'sources':provenance,'movies':records,'missing_base_names':missing,'missing_optional_mask_names':missing_masks,'statistics':{'base_request_names':len(selected),'exported_movies':len(records),'raw_compressed_bytes':total},'limits':['Original compressed bytes copied only; no decoder/game/installer executed, no pixel/GPU readiness.','Observed requests originate from reconstructed host running retained original Lua, not original Windows runtime execution.','Ahead-of-call weapon lists are explicit source declarations, not a fabricated reached runtime call.','Compiled cache budgets preserved; active source configuration overrides, if any, remain caller responsibility.','Optional masks missing in original metadata are explicit quiet native misses.']}
    (output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report['statistics']));print('missing bases',missing)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--content',type=Path,required=True);p.add_argument('--request-log',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--max-export-bytes',type=int,default=768*1024*1024);a=p.parse_args();export(a.content,a.request_log,a.output,a.max_export_bytes)
