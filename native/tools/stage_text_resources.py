"""Validate immutable original font/localization inputs before private staging."""
import hashlib,json,shutil
from pathlib import Path
def stage_text_resources(manifest,assets):
 manifest=Path(manifest);assets=Path(assets);data=json.loads(manifest.read_text())
 if data['schema']!='hades-original-text-v1' or data['target_sha256']!='d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254':raise ValueError('Original text source target mismatch')
 for item in [data['catalog'],*data['files'],*data['sources']]:
  p=Path(item['path'])
  if p.stat().st_size!=item['bytes'] or hashlib.file_digest(p.open('rb'),'sha256').hexdigest()!=item['sha256']:raise ValueError('Original text source changed: '+str(p))
 shutil.copyfile(data['catalog']['path'],assets/'text-fonts.lua');shutil.copytree(manifest.parent/'text-fonts',assets/'text-fonts',dirs_exist_ok=True)
 return {'fonts':len(data['fonts']),'localization_count':data['localization_count']}
