#!/usr/bin/env python3
"""Build an offline original Bink decoder against retained local tool sources.

No global installation and no supplied game code execution. The generated
helper uses only existing NihAV source and existing libswscale/libavutil.
"""
import argparse,hashlib,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for k in ['nihav','ffmpeg','output']:p.add_argument('--'+k,type=Path,required=True)
a=p.parse_args();a.nihav=a.nihav.resolve();a.ffmpeg=a.ffmpeg.resolve();a.output=a.output.resolve()
if a.output.exists():raise ValueError('Fresh build directory required')
a.output.mkdir(parents=True);(a.output/'src').mkdir();source=Path(__file__).with_name('bink2_predecode.rs').resolve();(a.output/'src/main.rs').write_bytes(source.read_bytes())
(a.output/'Cargo.toml').write_text('[package]\nname="bink2-original-predecoder"\nversion="0.1.0"\nedition="2021"\n[dependencies]\nnihav_core={path='+json.dumps(str(a.nihav/'nihav-core'))+'}\nnihav_rad={path='+json.dumps(str(a.nihav/'nihav-rad'))+',default-features=false,features=["decoder_bink2","demuxer_bink"]}\n')
commands=[['cargo','build','--release','--offline','--manifest-path',str(a.output/'Cargo.toml')],['clang','-fsanitize=address','-I',str(a.ffmpeg),str(Path(__file__).with_name('planar_bink_rgba.c').resolve()),str(a.ffmpeg/'libswscale/libswscale.a'),str(a.ffmpeg/'libavutil/libavutil.a'),'-framework','CoreFoundation','-framework','CoreVideo','-framework','CoreMedia','-lm','-pthread','-o',str(a.output/'planar_bink_rgba')]]
for cmd in commands:subprocess.run(cmd,check=True)
def identity(p):return {'path':str(p),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
sources=[source,Path(__file__).with_name('planar_bink_rgba.c').resolve()]
for sub in ['nihav-core','nihav-codec-support','nihav-rad']:
 sources.extend((a.nihav/sub/'src').rglob('*.rs'));sources.append(a.nihav/sub/'Cargo.toml')
(a.output/'build-provenance.json').write_text(json.dumps({'schema':'HadesOfflineBinkPredecoderBuild1','commands':commands,'sources':[identity(f)for f in sources],'linked_sources':[identity(a.ffmpeg/'libswscale/libswscale.a'),identity(a.ffmpeg/'libavutil/libavutil.a')],'limitations':['Offline preprocessing only. Original codec data is decoded, never executed.','NihAV is a separate original-pixel decoder; not a claim of identical original proprietary decoder rounding.','Conversion is source full-range601 selected flags0x10 via libswscale.']},indent=2)+'\n')
