#include "audio_bank_decoder.hpp"
extern "C" {
#include <libvgmstream.h>
}
#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace hades {
namespace {
struct IoState { std::string error; uint64_t bytesRead=0; uint64_t deniedCompanions=0; };
struct AssetStream {
    libstreamfile_t api{};
    AssetStore* assets=nullptr;
    std::unique_ptr<AssetFile> file;
    std::string name;
    std::shared_ptr<IoState> status;
    std::array<uint8_t,65536> cache{};
    uint64_t cacheStart=0;
    size_t cacheBytes=0;
    uint64_t length=0;
};
void fail(AssetStream& s,const char* error) { if(s.status->error.empty())s.status->error=error; }
libstreamfile_t* openAsset(AssetStore*,const std::string&,uint64_t,const std::shared_ptr<IoState>&);
int readAsset(void* user,uint8_t* target,int64_t offset,int length) {
    auto& s=*static_cast<AssetStream*>(user);
    if(!s.status->error.empty())return 0;
    if(offset<0||length<0||(!target&&length)){fail(s,"Invalid decoder asset read");return 0;}
    uint64_t at=static_cast<uint64_t>(offset);
    if(at>=s.length||length==0)return 0;
    size_t remaining=static_cast<size_t>(std::min<uint64_t>(static_cast<uint64_t>(length),s.length-at));
    const size_t requested=remaining;
    try {
        while(remaining) {
            if(s.cacheBytes==0||at<s.cacheStart||at-s.cacheStart>=s.cacheBytes) {
                s.cacheStart=at;
                if(!s.file->seek(static_cast<size_t>(at))){fail(s,"Audio AssetFile does not support seek");return 0;}
                const size_t needed=static_cast<size_t>(std::min<uint64_t>(s.cache.size(),s.length-at));
                s.cacheBytes=s.file->read(s.cache.data(),needed);s.status->bytesRead+=s.cacheBytes;
                if(s.cacheBytes!=needed){fail(s,"Short audio asset read");return 0;}
            }
            size_t relative=static_cast<size_t>(at-s.cacheStart);
            size_t n=std::min(remaining,s.cacheBytes-relative);
            std::memcpy(target,s.cache.data()+relative,n);target+=n;at+=n;remaining-=n;
        }
    } catch(...) {fail(s,"Audio asset provider threw while reading");return 0;}
    return static_cast<int>(requested);
}
int64_t sizeAsset(void* user){return static_cast<int64_t>(static_cast<AssetStream*>(user)->length);}
const char* nameAsset(void* user){return static_cast<AssetStream*>(user)->name.c_str();}
libstreamfile_t* reopenAsset(void* user,const char* name){
    auto& s=*static_cast<AssetStream*>(user);
    // The library probes optional .txtm/name sidecars even for a complete FSB5.
    // They are absent by construction in this one-asset provider. A required
    // missing companion causes the library's stream-open operation to fail.
    if(!name||s.name!=name){++s.status->deniedCompanions;return nullptr;}
    return openAsset(s.assets,s.name,s.length,s.status);
}
void closeAsset(libstreamfile_t* api){if(api)delete static_cast<AssetStream*>(api->user_data);}
libstreamfile_t* openAsset(AssetStore* assets,const std::string& name,uint64_t expectedBytes,const std::shared_ptr<IoState>& status){
    try {
        auto s=std::make_unique<AssetStream>();s->assets=assets;s->name=name;s->status=status;
        s->file=assets->open(name);
        if(!s->file){status->error="Audio asset is absent";return nullptr;}
        s->length=s->file->size();
        if(s->length!=expectedBytes){status->error="Audio asset length differs from catalog";return nullptr;}
        s->api={s.get(),readAsset,sizeAsset,nameAsset,reopenAsset,closeAsset};
        auto* result=&s->api;s.release();return result;
    }catch(...){status->error="Audio asset provider threw while opening";return nullptr;}
}
bool safeName(const std::string& name){
    if(name.empty()||name.front()=='/'||name.find('\\')!=std::string::npos||name.find('\0')!=std::string::npos)return false;
    size_t at=0;
    while(at<name.size()){
        size_t end=name.find('/',at);if(end==std::string::npos)end=name.size();
        if(end==at||name.substr(at,end-at)==".."||name.substr(at,end-at)==".")return false;
        at=end+1;
    }
    return name.ends_with(".fsb")||name.ends_with(".bank");
}
}
struct AudioBankDecoder::Impl {
    AssetStore* assets;
    libvgmstream_t* decoder=nullptr;
    AudioBankSample sample;
    int64_t position=0;
    std::shared_ptr<IoState> io;
    explicit Impl(AssetStore* a):assets(a){}
    ~Impl(){if(decoder)libvgmstream_free(decoder);}
};
AudioBankDecoder::AudioBankDecoder(AssetStore* assets):impl_(std::make_unique<Impl>(assets)){}
AudioBankDecoder::~AudioBankDecoder()=default;
void AudioBankDecoder::close(){
    if(impl_->decoder)libvgmstream_free(impl_->decoder);
    impl_->decoder=nullptr;impl_->sample={};impl_->position=0;impl_->io.reset();
}
bool AudioBankDecoder::open(const AudioBankSample& requested,std::string& error){
    close();error.clear();
    if(!impl_->assets||!safeName(requested.asset)||requested.subsong<1||requested.channels<1||requested.channels>8||requested.sampleRate<4000||requested.sampleRate>192000||requested.frames<=0||requested.frames>INT_MAX||requested.encodedFileBytes<60||requested.encodedFileBytes>800ull*1024*1024){error="Invalid bounded audio sample descriptor";return false;}
    impl_->io=std::make_shared<IoState>();
    libstreamfile_t* sf=openAsset(impl_->assets,requested.asset,requested.encodedFileBytes,impl_->io);
    if(!sf){error=impl_->io->error;return false;}
    libvgmstream_config_t config{};config.disable_config_override=true;config.ignore_loop=true;config.force_sfmt=LIBVGMSTREAM_SFMT_FLOAT;
    impl_->decoder=libvgmstream_create(sf,requested.subsong,&config);libstreamfile_close(sf);
    if(!impl_->decoder||!impl_->io->error.empty()){error=impl_->io->error.empty()?"Original bank stream is not decodable":impl_->io->error;close();return false;}
    const auto& format=*impl_->decoder->format;
    if(format.channels!=requested.channels||format.sample_rate!=requested.sampleRate||format.stream_samples!=requested.frames||format.sample_format!=LIBVGMSTREAM_SFMT_FLOAT||format.sample_size!=4){error="Decoded audio format differs from original catalog";close();return false;}
    impl_->sample=requested;return true;
}
bool AudioBankDecoder::readFrames(float* output,size_t capacity,size_t& decoded,std::string& error){
    decoded=0;error.clear();
    if(!impl_->decoder||!output||capacity==0||capacity>1048576){error="Invalid audio decode request";return false;}
    if(!impl_->io->error.empty()){error=impl_->io->error;return false;}
    if(libvgmstream_fill(impl_->decoder,output,static_cast<int>(capacity))<0||!impl_->io->error.empty()){error=impl_->io->error.empty()?"Original audio stream decode failed":impl_->io->error;return false;}
    const int count=impl_->decoder->decoder->buf_samples;
    if(count<0||static_cast<size_t>(count)>capacity){error="Decoder returned an invalid frame count";return false;}
    decoded=static_cast<size_t>(count);impl_->position+=count;return true;
}
bool AudioBankDecoder::seek(int64_t frame,std::string& error){
    error.clear();
    if(!impl_->decoder||frame<0||frame>impl_->sample.frames){error="Audio seek is outside original sample";return false;}
    if(!impl_->io->error.empty()){error=impl_->io->error;return false;}
    // Pinned API's seek resets the sample buffer but retains decode_done after
    // EOF. Its public reset clears that flag before the real decoder seek.
    libvgmstream_reset(impl_->decoder);
    libvgmstream_seek(impl_->decoder,frame);
    if(!impl_->io->error.empty()){error=impl_->io->error;return false;}
    if(libvgmstream_get_play_position(impl_->decoder)!=frame){error="Decoder did not reach requested audio frame";return false;}
    impl_->position=frame;return true;
}
int64_t AudioBankDecoder::position()const{return impl_->decoder?impl_->position:-1;}
const AudioBankSample* AudioBankDecoder::sample()const{return impl_->decoder?&impl_->sample:nullptr;}
uint64_t AudioBankDecoder::sourceBytesRead()const{return impl_->io?impl_->io->bytesRead:0;}
uint64_t AudioBankDecoder::deniedCompanionRequests()const{return impl_->io?impl_->io->deniedCompanions:0;}
}
