"""Bounded action FX staging, callable from native packaging (no builds)."""
import hashlib,json,shutil
from pathlib import Path

def stage_action_effects(manifest_path,assets):
 manifest_path=Path(manifest_path);assets=Path(assets);report=json.loads(manifest_path.read_text())
 if report['schema']!='hades-action-effect-resources-v1' or report['target_sha256']!='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254':raise ValueError('Original action effect manifest target mismatch')
 for item in [report['packet'],report['catalog'],report['lua'],*report['sources']]:
  p=Path(item['path'])
  if hashlib.file_digest(p.open('rb'),'sha256').hexdigest()!=item['sha256']:raise ValueError('Original action effect source identity changed: '+str(p))
 catalog=json.loads(Path(report['catalog']['path']).read_text())
 if not catalog.get('FrameMetadata')or catalog['ImageBankSha256']!=report['packet']['sha256']:raise ValueError('Original action effect atlas metadata/hash unavailable')
 for key,name in [('packet','action-effect-frames.bin'),('lua','action-effects.lua')]:shutil.copyfile(report[key]['path'],assets/name)
 (assets/'action-effect-sha256.bin').write_bytes(bytes.fromhex(report['packet']['sha256']))
 return {'definitions':len(catalog['Definitions']),'original_frames':len(catalog['FrameMetadata']),'bytes':report['packet']['bytes']}
