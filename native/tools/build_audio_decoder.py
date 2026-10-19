#!/usr/bin/env python3
"""Build pinned private Vorbis decoder dependencies for Android arm64/API26.

Reuses supplied NDK and already acquired source archives; performs no download,
device operation, game execution or app build modification. Output must be new.
"""
import argparse,hashlib,json,re,subprocess,tarfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
PINS={
 'vgmstream-db77f98f':('db77f98f405a45ad08df98aa444cae3848f95e6c','a9d9f2df3b4544b9e2538618b49baa388a593d9f370c71ddf424ebc26f5f2fde'),
 'libogg-1.3.6':('be05b13e98b048f0b5a0f5fa8ce514d56db5f822','b2ff5c003e5697507169f7d614d64dc4e8bf030c85ad081bec75c164566f8246'),
 'libvorbis-1.3.7':('0657aee69dec8508a0011f47f3b69d7538e9d262','6ccc3fa4cfd3072f9a772025cc8fd712098d081c00171b09b6627ffced7faa88'),
}
def identity(path):
    if path.is_symlink()or not path.is_file():raise ValueError('Expected regular build artifact: '+str(path))
    with path.open('rb')as f:return {'path':str(path.resolve()),'bytes':path.stat().st_size,'sha256':hashlib.file_digest(f,'sha256').hexdigest()}
def verify_source(path,pin):
    archive=identity(path/'source.tar.gz')
    if archive['sha256']!=pin[1]:raise ValueError('Pinned source archive hash mismatch')
    count=0
    with tarfile.open(path/'source.tar.gz','r:gz')as tar:
        members=tar.getmembers()
        if len(members)>30000:raise ValueError('Unbounded dependency archive')
        prefix=members[0].name+'/'
        for m in members:
            if not m.isfile():continue
            if not m.name.startswith(prefix)or m.size>32*1024*1024:raise ValueError('Unexpected dependency source member')
            relative=Path(m.name[len(prefix):])
            if relative.is_absolute()or'..'in relative.parts:raise ValueError('Unsafe source archive name')
            source=path/relative
            if identity(source)['sha256']!=hashlib.sha256(tar.extractfile(m).read()).hexdigest():raise ValueError('Private dependency source changed: '+str(relative))
            count+=1
    return {'commit':pin[0],'archive':archive,'source_members_verified':count}
def build(ndk,output,jobs):
    ndk=ndk.resolve();output=output.resolve()
    if output.exists()or output.is_symlink():raise ValueError('Audio dependency build directory must be new')
    if not (ndk/'build/cmake/android.toolchain.cmake').is_file():raise ValueError('Missing existing Android NDK')
    if jobs<1 or jobs>8:raise ValueError('Bounded job count required')
    sources={name:verify_source(ROOT/'.local/tools'/name,pin)for name,pin in PINS.items()}
    output.mkdir(parents=True);commands=[]
    def run(argv,log):
        with (output/log).open('w')as f:p=subprocess.run(argv,stdout=f,stderr=subprocess.STDOUT,check=False)
        commands.append({'argv':argv,'exit_code':p.returncode,'log':identity(output/log)})
        if p.returncode:raise ValueError('Audio build failed; inspect '+str(output/log))
    source=ROOT/'.local/tools/vgmstream-db77f98f';configure=['cmake','-S',str(source),'-B',str(output),'-DCMAKE_TOOLCHAIN_FILE='+str(ndk/'build/cmake/android.toolchain.cmake'),'-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-26','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_POSITION_INDEPENDENT_CODE=ON','-DCMAKE_POLICY_VERSION_MINIMUM=3.5','-DBUILD_CLI=OFF','-DBUILD_V123=OFF','-DBUILD_AUDACIOUS=OFF','-DUSE_MPEG=OFF','-DUSE_FFMPEG=OFF','-DUSE_G7221=OFF','-DUSE_G719=OFF','-DUSE_ATRAC9=OFF','-DUSE_CELT=OFF','-DUSE_SPEEX=OFF','-DOGG_PATH='+str(ROOT/'.local/tools/libogg-1.3.6'),'-DVORBIS_PATH='+str(ROOT/'.local/tools/libvorbis-1.3.7')]
    run(configure,'configure.log');run(['cmake','--build',str(output),'--target','libvgmstream','ogg','vorbis','vorbisfile','-j',str(jobs)],'build.log')
    generated=list((output/'CMakeFiles').glob('*/CMakeCCompiler.cmake'))
    if len(generated)!=1:raise ValueError('Expected one actual generated compiler identity')
    text=generated[0].read_text();match=re.search(r'set\(CMAKE_C_COMPILER "([^"]+)"\)',text)
    if not match:raise ValueError('Missing generated actual C compiler')
    compiler=Path(match[1]).resolve()
    if not compiler.is_relative_to(ndk):raise ValueError('Compiler is outside selected NDK')
    build_commands=output/'src/CMakeFiles/libvgmstream.dir/build.make';actual_targets=set(re.findall(r'--target=([^\s]+)',build_commands.read_text()))
    if actual_targets!={'aarch64-none-linux-android26'}:raise ValueError('Actual generated compile commands are not Android arm64/API26')
    libraries={name:identity(output/relative)for name,relative in {'vgmstream':'src/libvgmstream.a','vorbisfile':'dependencies/vorbis/lib/libvorbisfile.a','vorbis':'dependencies/vorbis/lib/libvorbis.a','ogg':'dependencies/ogg/libogg.a'}.items()}
    proof={'schema':'hades-audio-decoder-build-v1','ndk':str(ndk),'abi':'arm64-v8a','api':26,'sources':sources,'source_root':str(source),'compiler':identity(compiler),'generated_compiler':identity(generated[0]),'generated_compile_commands':identity(build_commands),'actual_compiler_targets':sorted(actual_targets),'cache':identity(output/'CMakeCache.txt'),'libraries':libraries,'commands':commands,'scope':'Compilation only. External optional decoders disabled except Vorbis; vgmstream builtin formats/codecs remain in upstream library. No Android execution.'}
    (output/'verification.json').write_text(json.dumps(proof,indent=2)+'\n');return {'output':str(output),'verification':str(output/'verification.json'),'libraries':libraries}
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--ndk',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--jobs',type=int,default=4);a=p.parse_args();print(json.dumps(build(a.ndk,a.output,a.jobs),indent=2))
if __name__=='__main__':main()
