#!/opt/homebrew/bin/python3.13
"""Bounded original predecoded RGBA source for the existing HAA exporter.

Accepts only its raw-RGBA decode command. Manifest binds the original Bink,
all prepared bytes, dimensions and complete decoded frame count.
"""
import hashlib,json,os,struct,sys
from pathlib import Path
m=json.loads(Path(os.environ['HADES_PREDECODED_BINK_MANIFEST']).read_text());args=sys.argv[1:]
def value(flag):return args[args.index(flag)+1]
def sha(p):
 h=hashlib.sha256()
 with p.open('rb')as f:
  for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
 return h.hexdigest()
source=Path(value('-i'));rgba=Path(m['rgba']['path']);wanted=int(value('-frames:v'));w,h=m['canvas']
if sha(source)!=m['source']['sha256']or sha(rgba)!=m['rgba']['sha256']or value('-pix_fmt')!='rgba'or value('-f')!='rawvideo'or not 0<w<=2048 or not 0<h<=2048 or not 0<wanted<=m['frames']or rgba.stat().st_size!=w*h*4*m['frames']:raise ValueError('Prepared original Bink identity/format/bounds mismatch')
with rgba.open('rb')as f:
 remaining=w*h*4*wanted
 while remaining:
  b=f.read(min(remaining,1024*1024))
  if not b:raise ValueError('Truncated prepared original Bink')
  sys.stdout.buffer.write(b);remaining-=len(b)
