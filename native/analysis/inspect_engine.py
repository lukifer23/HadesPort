#!/usr/bin/env python3
"""Read the exact PE/PDB inventory and annotate selected native instructions. Never executes game code."""
from pathlib import Path
import argparse,struct,json,subprocess,re,hashlib,bisect,os
ROOT=Path(__file__).resolve().parents[2]
DLL=ROOT/'.local/game/base/x64/EngineWin64s.dll'
PDB=ROOT/'.local/reports/native-engine-pdb.json'
LLVM = Path(os.environ.get('LLVM_OBJDUMP') or Path(os.environ.get('ANDROID_HOME') or os.environ.get('ANDROID_SDK_ROOT') or Path.home()/'Library/Android/sdk')/'ndk/29.0.14206865/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-objdump')
def main():
 a=argparse.ArgumentParser();a.add_argument('name');a.add_argument('--pdb',type=Path,default=PDB);a.add_argument('--output',required=True);a.add_argument('--rva',type=lambda x:int(x,0));args=a.parse_args()
 data=DLL.read_bytes();pe=struct.unpack_from('<I',data,60)[0];n=struct.unpack_from('<H',data,pe+6)[0];opts=struct.unpack_from('<H',data,pe+20)[0];base=struct.unpack_from('<Q',data,pe+48)[0]
 sections=[]
 for i in range(n):
  off=pe+24+opts+i*40;name=data[off:off+8].rstrip(b'\0').decode();vs,rva,rs,fo=struct.unpack_from('<IIII',data,off+8);sections.append((rva,rs,fo,name))
 def read_string(va):
  r=va-base
  for rv,size,fo,_ in sections:
   if rv<=r<rv+size:
    p=fo+r-rv;end=data.find(b'\0',p,min(len(data),p+512));b=data[p:end] if end>=0 else b''
    if len(b)>1 and all(32<=x<127 or x in [9,10,13] for x in b):return b.decode()
  return None
 pdb=json.loads(args.pdb.read_text());procs=[s for s in pdb['module_symbols'] if s['kind']=='procedure' and s['rva'] is not None]
 selected=[s for s in procs if s['name']==args.name and (args.rva is None or s['rva']==args.rva)];assert len(selected)==1,selected
 proc=selected[0];start=base+proc['rva'];end=start+proc['length']
 byaddr={}
 for s in pdb['globals']+pdb['module_symbols']:
  if s.get('rva') is not None:byaddr[base+s['rva']]=s['name']
 asm=subprocess.check_output([str(LLVM),'--disassemble',f'--start-address={hex(start)}',f'--stop-address={hex(end)}',str(DLL)],text=True)
 annotations=[]
 for line in asm.splitlines():
  if not re.match(r'^[0-9a-f]+:',line):continue
  refs=[]
  for match in re.finditer(r'0x([0-9a-f]{8,})',line):
   va=int(match.group(1),16);entry={'address':hex(va)}
   if va in byaddr:entry['symbol']=byaddr[va]
   text=read_string(va)
   if text:entry['string']=text
   if len(entry)>1:refs.append(entry)
  if refs:annotations.append({'instruction':line,'references':refs})
 report={'dll_sha256':hashlib.sha256(data).hexdigest(),'pdb_identity':pdb['identity'],'procedure':proc,'image_base':hex(base),'observed':'PDB names and sizes joined to decoded native instruction addresses. String references are observations, not by themselves registration semantics.','annotations':annotations,'assembly':asm}
 Path(args.output).write_text(json.dumps(report,indent=2)+'\n')
 for a in annotations:
  print(a['instruction'].split(':',1)[0],'; '.join(r.get('symbol',r.get('string','')) for r in a['references']))
if __name__=='__main__':main()
