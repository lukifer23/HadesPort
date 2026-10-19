#include "../app/audio_bank_decoder.hpp"
#include "../app/audio_sample_stream.hpp"
#include <cmath>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
using namespace hades;
struct File:AssetFile {
    std::ifstream stream;size_t length=0,pos=0;
    explicit File(const std::filesystem::path& path):stream(path,std::ios::binary){if(stream)length=std::filesystem::file_size(path);}
    size_t size()const override{return length;}
    size_t remaining()const override{return length-pos;}
    size_t read(void* out,size_t n)override{stream.read(static_cast<char*>(out),std::streamsize(n));size_t got=size_t(stream.gcount());pos+=got;return got;}
    bool seek(size_t n)override{if(n>length)return false;stream.clear();stream.seekg(std::streamoff(n));if(!stream)return false;pos=n;return true;}
};
struct Store:AssetStore {
    std::filesystem::path root;explicit Store(const char* path):root(path){}
    std::unique_ptr<AssetFile> open(std::string_view name)override{auto file=std::make_unique<File>(root/std::string(name));return file->stream?std::move(file):nullptr;}
};
int main(int argc,char** argv){
    if(argc!=10){std::cerr<<"root asset channels rate frames encodedFileBytes referenceWav dataOffset referenceFrames required\n";return 2;}
    Store store(argv[1]);AudioBankSample sample{argv[2],1,std::stoi(argv[3]),std::stoi(argv[4]),std::stoll(argv[5]),std::stoull(argv[6])};
    std::ifstream reference(argv[7],std::ios::binary);assert(reference);size_t offset=std::stoull(argv[8]);assert(sample.frames==std::stoll(argv[9]));
    AudioBankDecoder decoder(&store);std::string error;
    if(!decoder.open(sample,error)){std::cerr<<error<<'\n';return 1;}
    std::vector<float> decoded(4096*size_t(sample.channels)),expected(decoded.size());int64_t position=0;
    reference.seekg(std::streamoff(offset));
    while(position<sample.frames){
        size_t capacity=std::min<size_t>(4096,size_t(sample.frames-position)),got=0;
        if(!decoder.readFrames(decoded.data(),capacity,got,error)){std::cerr<<error<<'\n';return 1;}
        if(got==0||got>capacity){std::cerr<<"Unexpected decoded frame count\n";return 1;}
        size_t bytes=got*size_t(sample.channels)*4;reference.read(reinterpret_cast<char*>(expected.data()),std::streamsize(bytes));
        if(size_t(reference.gcount())!=bytes||std::memcmp(decoded.data(),expected.data(),bytes)){std::cerr<<"Original reference PCM differs at frame "<<position<<'\n';return 1;}
        position+=int64_t(got);
    }
    size_t eof=1;if(!decoder.readFrames(decoded.data(),16,eof,error)||eof!=0){std::cerr<<"Unexpected EOF behavior "<<eof<<' '<<error<<'\n';return 1;}
    for(int64_t frame:{int64_t(0),sample.frames/2,std::max<int64_t>(0,sample.frames-17)}){
        if(!decoder.seek(frame,error)){std::cerr<<error<<'\n';return 1;}
        size_t count=std::min<size_t>(17,size_t(sample.frames-frame)),got=0;
        if(!decoder.readFrames(decoded.data(),count,got,error)||got!=count){std::cerr<<"Seek read failed "<<error<<'\n';return 1;}
        reference.clear();reference.seekg(std::streamoff(offset+uint64_t(frame)*uint64_t(sample.channels)*4));reference.read(reinterpret_cast<char*>(expected.data()),std::streamsize(count*size_t(sample.channels)*4));
        if(std::memcmp(decoded.data(),expected.data(),count*size_t(sample.channels)*4)){std::cerr<<"Seek PCM mismatch at "<<frame<<'\n';return 1;}
    }
    assert(!decoder.seek(-1,error));assert(!decoder.seek(sample.frames+1,error));
    uint64_t reads=decoder.sourceBytesRead(),denied=decoder.deniedCompanionRequests();decoder.close();assert(decoder.sample()==nullptr);
    {
        assert(sample.frames*sample.channels*4<=32*1024*1024);
        std::vector<float> original(size_t(sample.frames)*size_t(sample.channels));reference.clear();reference.seekg(std::streamoff(offset));reference.read(reinterpret_cast<char*>(original.data()),std::streamsize(original.size()*4));assert(size_t(reference.gcount())==original.size()*4);
        AudioSampleStream stream(&store);assert(stream.open(sample,error));
        const uint64_t end=uint64_t(sample.frames)*48000/uint64_t(sample.sampleRate);
        for(uint64_t tick:{0ULL,1ULL,2ULL,3ULL,5ULL,999ULL,1000ULL,1023ULL,1024ULL,4095ULL,4096ULL,static_cast<unsigned long long>(end),16ULL}){
            float actual[2]{};assert(stream.stereoAtTick(tick,actual[0],actual[1],error));
            const uint64_t scaled=tick*uint64_t(sample.sampleRate);const auto center=int64_t(scaled/48000);const double phase=double(scaled%48000)/48000;
            double expected[2]{};double total=0;constexpr double pi=3.1415926535897932384626433832795;
            for(int tap=-15;tap<=16;++tap){const auto frame=center+tap;const double distance=double(tap)-phase;const double sinc=distance==0?1:std::sin(pi*distance)/(pi*distance);const double window=std::fabs(distance)>=16?0:.5+.5*std::cos(pi*distance/16);const double weight=sinc*window;total+=weight;
                if(frame>=0&&frame<sample.frames)for(int c=0;c<sample.channels;++c)expected[c]+=original[size_t(frame)*size_t(sample.channels)+size_t(c)]*weight;
            }
            for(int c=0;c<2;++c){const double value=expected[sample.channels==1?0:c]/total;assert(std::fabs(actual[c]-value)<1e-6);}
        }
    }
    sample.frames+=1;assert(!decoder.open(sample,error));sample.frames-=1;sample.encodedFileBytes+=1;assert(!decoder.open(sample,error));
    std::cout<<"AUDIO_SOURCE_PROBE frames="<<position<<" rate="<<sample.sampleRate<<" channels="<<sample.channels<<" sourceBytesReadIncludingSeeks="<<reads<<" deniedCompanionRequests="<<denied<<" fullPCMIdentical=1 randomSeekPCMIdentical=1 rationalSincReference=1 cacheBoundaryAndBackwards=1\n";
}
