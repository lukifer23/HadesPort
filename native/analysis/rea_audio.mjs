import {Client} from '../../../rea/node_modules/@modelcontextprotocol/client/dist/index.mjs';
import {StdioClientTransport} from '../../../rea/node_modules/@modelcontextprotocol/client/dist/stdio.mjs';
import fs from 'node:fs';
import path from 'node:path';
const root=path.resolve('hades');
const t=new StdioClientTransport({command:'python3',args:[path.join(root,'scripts/rea.py'),'--mcp'],stderr:'pipe'});
t.stderr?.on('data',d=>fs.appendFileSync(path.join(root,'.local/reports/native-rea-session.log'),d));
const c=new Client({name:'hades-native-contract',version:'1.0.0'});await c.connect(t);
const opts={timeout:600000};
const tag=process.argv[3]||'audio-fmod-';
const library=process.argv[4]||'fmodstudio.dll';
if(!['fmodstudio.dll','fmod.dll'].includes(library))throw new Error('Only preserved FMOD libraries are selected');
async function call(name,args,file){
 const r=await c.callTool({name,arguments:args},opts);
 const output=path.join(root,'.local/reports/'+file);
 fs.writeFileSync(output,JSON.stringify(r,null,2));console.log(name+' '+file);
 let payload=r.structuredContent;
 if(!payload)for(const block of r.content??[])if(block.type==='text'){try{payload=JSON.parse(block.text);break;}catch{}}
 const error=payload?.error;
 if(error?.code==='resource_constraint'&&error.details?.resource==='transport'){
  const source=error.details.reported_limits?.evidence_reference;
  if(!source)throw new Error('Oversized analysis did not provide retained Evidence');
  // close_binary clears the session ledger. Export the complete record before
  // cleanup, then retain a compact view rather than losing the continuation.
  const bundle=output.replace(/\.json$/,'-bundle.json');
  const exported=await c.callTool({name:'export_evidence_bundle',arguments:{path:bundle,overwrite:false}},opts);
  fs.writeFileSync(output.replace(/\.json$/,'-export.json'),JSON.stringify(exported,null,2));
  if(exported.isError||!fs.existsSync(bundle))throw new Error('Full Evidence export failed; inspect export receipt');
  const summary=await c.callTool({name:'inspect_analysis_view',arguments:{source,view:{kind:'native',facet:'procedure'}}},opts);
  fs.writeFileSync(output.replace(/\.json$/,'-summary.json'),JSON.stringify(summary,null,2));
  if(summary.isError)throw new Error('Evidence summary failed after export');
  console.log('complete_evidence '+path.basename(bundle));
 }else if(error||r.isError)throw new Error('REA operation failed; inspect '+file);
 return r;
}
try {
 await call('binary_session',{},'native-rea-'+tag+'start.json');
 await call('open_binary',{path:path.join(root,'.local/game/base/x64/'+library),provider_id:'ghidra'},'native-rea-'+tag+'open.json');
 const functions=process.argv[2]?JSON.parse(process.argv[2]):[['audio-fmod-get-timeline','0x1800848a0']];
 for(const entry of functions) {
  if(Array.isArray(entry)) await call('analyze_function',{procedure:entry[1]},'native-rea-'+entry[0]+'.json');
  else await call(entry.tool,entry.args,'native-rea-'+entry.name+'.json');
 }
} finally {try{await call('close_binary',{},'native-rea-'+tag+'close.json');}finally{await c.close();}}
