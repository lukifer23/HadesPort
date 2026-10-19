#!/usr/bin/env python3
"""Recover the original global control namespace and actual enabled initializers."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser();p.add_argument('--pdb',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--header',type=Path,required=True);a=p.parse_args()
 if a.output.exists():raise ValueError('Use fresh retained evidence directory')
 a.output.mkdir(parents=True)
 source=json.loads(a.pdb.read_text());reference=next(x for x in source['globals']if x.get('kind')=='data'and x['name']=='sgg::Controls::AdvancedTooltip');typ=reference['type_index'];records=[]
 for item in source['globals']:
  if item.get('kind')!='data'or item.get('type_index')!=typ or not re.fullmatch(r'sgg::Controls::[A-Za-z0-9_]+',item['name']):continue
  name=item['name'].split('::')[-1];proc="`dynamic initializer for 'sgg::Controls::"+name+"''";out=a.output/(name+'.json')
  subprocess.run(['python3',str(ROOT/'native/analysis/inspect_engine.py'),proc,'--pdb',str(a.pdb),'--output',str(out)],stdout=subprocess.DEVNULL,check=True)
  report=json.loads(out.read_text());enabledVA=0x180000000+item['rva']+16
  matches=[l for l in report['assembly'].splitlines()if re.search(r'movb\s+\$0x1,',l)and f'0x{enabledVA:x}'in l]
  if len(matches)!=1:raise ValueError('Enabled constructor assignment unresolved: '+name)
  records.append({'name':name,'enabled':True,'global_rva':hex(item['rva']),'initializer':report['procedure'],'enabled_store':matches[0],'evidence':str(out),'evidence_sha256':hashlib.file_digest(out.open('rb'),'sha256').hexdigest()})
 records.sort(key=lambda x:x['name']);assert records
 header='#pragma once\n#include <array>\n#include <string_view>\nnamespace hades::contract {\ninline constexpr std::array<std::string_view,'+str(len(records))+'> controlNames{{\n'+''.join('    "'+r['name']+'",\n'for r in records)+'}};\n}\n'
 a.header.write_text(header)
 (a.output/'manifest.json').write_text(json.dumps({'target_sha256':'d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254','pdb_sha256':hashlib.file_digest(a.pdb.open('rb'),'sha256').hexdigest(),'controls':records,'header_sha256':hashlib.sha256(header.encode()).hexdigest(),'observed':'Global native RemappableControl variables plus per-object enabled=true store at+0x10; runtime input/callback behavior separate.'},indent=2)+'\n')
 print('Recovered',len(records),'original controls')
if __name__=='__main__':main()
