#include "actor_animation.hpp"
#include <algorithm>
#include <chrono>
#include <bit>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <zlib.h>

namespace hades {
namespace {
void read(AssetFile& f,void* p,size_t n){if(n>f.remaining())throw std::runtime_error("Truncated HAA1");auto* b=static_cast<uint8_t*>(p);while(n){auto got=f.read(b,n);if(!got)throw std::runtime_error("HAA1 read failed");b+=got;n-=got;}}
uint32_t integer(AssetFile& f){uint32_t v;read(f,&v,4);return v;}
float real(AssetFile& f){float v;read(f,&v,4);if(!std::isfinite(v))throw std::runtime_error("Non-finite HAA1");return v;}
std::string text(AssetFile& f,size_t max=4096){auto n=integer(f);if(n>max)throw std::runtime_error("HAA1 string bound");std::string s(n,'\0');read(f,s.data(),n);if(s.find('\0')!=std::string::npos)throw std::runtime_error("Embedded HAA1 null");return s;}
}
std::array<float,4> ActorAnimationImage::worldRect(const ActorAnimationClip& clip)const{
    return {(float(cropX)-originX)*clip.worldScale+clip.offsetX,(float(cropY)-originY)*clip.worldScale+clip.offsetY,width*clip.worldScale,height*clip.worldScale};
}
void ActorAnimationBank::reset(){file_.reset();frames_.clear();clips_.clear();facing_.clear();inverseAngleTable_.clear();properties_.clear();cache_.clear();compressed_.clear();stats_={};use_=0;}
void ActorAnimationBank::load(AssetStore& store,const char* filename,const char* facingFilename,const char* identityFilename){
    reset();
    try{
        file_=store.open(filename);if(!file_)throw std::runtime_error("Missing HAA1 actor animations");
        // Bounded lazy file: hero + original first-encounter movies retain 8 source
        // directions. Payloads stay seekable and decode only through the cache;
        // frame/count/raw/CRC bounds below remain independent of packet size.
        if(file_->size()>1024ull*1024*1024||integer(*file_)!=0x31414148)throw std::runtime_error("Invalid HAA1 identity/size");
        const auto frames=integer(*file_),clips=integer(*file_);if(!frames||frames>65536||!clips||clips>4096)throw std::runtime_error("HAA1 counts outside bound");
        uint64_t rawTotal=0,slideTotal=0;
        for(uint32_t i=0;i<frames;++i){
            Frame frame;auto& m=frame.image;m.packetFrameIndex=i;m.canvasWidth=integer(*file_);m.canvasHeight=integer(*file_);m.cropX=integer(*file_);m.cropY=integer(*file_);m.width=integer(*file_);m.height=integer(*file_);m.originalMovieFrame=integer(*file_);m.originalAngle=integer(*file_);m.originX=real(*file_);m.originY=real(*file_);frame.rawBytes=integer(*file_);frame.packedBytes=integer(*file_);frame.crc=integer(*file_);
            if(!m.canvasWidth||!m.canvasHeight||m.canvasWidth>2048||m.canvasHeight>2048||!m.width||!m.height||m.cropX>m.canvasWidth||m.cropY>m.canvasHeight||m.width>m.canvasWidth-m.cropX||m.height>m.canvasHeight-m.cropY||uint64_t(m.width)*m.height*4!=frame.rawBytes||frame.rawBytes>MaxFrameBytes||!frame.packedBytes||frame.packedBytes>frame.rawBytes+65536||frame.packedBytes>file_->remaining()||m.originalAngle>1023)throw std::runtime_error("HAA1 frame outside bounds");
            rawTotal+=frame.rawBytes;if(rawTotal>2ull*1024*1024*1024)throw std::runtime_error("HAA1 raw budget exceeded");
            frame.offset=file_->size()-file_->remaining();if(!file_->seek(frame.offset+frame.packedBytes))throw std::runtime_error("HAA1 needs seekable assets");frames_.push_back(frame);
        }
        for(uint32_t i=0;i<clips;++i){
            ActorAnimationClip c;c.name=text(*file_);c.chain=text(*file_);c.moveAnimation=text(*file_);c.originalAngles=integer(*file_);auto angles=integer(*file_);c.flags=integer(*file_);auto slides=integer(*file_);c.worldScale=real(*file_);c.offsetX=real(*file_);c.offsetY=real(*file_);c.secondsPerTick=real(*file_);
            slideTotal+=slides;if(c.name.empty()||find(c.name)||!angles||angles>64||c.originalAngles<angles||c.originalAngles>1024||c.flags>1023||!slides||slides>10000||slideTotal>100000||c.worldScale<=0||c.worldScale>16||c.secondsPerTick<=0||c.secondsPerTick>1)throw std::runtime_error("HAA1 clip outside bounds");
            for(uint32_t a=0;a<angles;++a){auto index=integer(*file_);if(index>=c.originalAngles||(!c.originalAngleIndices.empty()&&index<=c.originalAngleIndices.back()))throw std::runtime_error("HAA1 angle order invalid");c.originalAngleIndices.push_back(index);}
            uint64_t ticks=0;
            for(uint32_t n=0;n<slides;++n){
                ActorAnimationSlide s;s.originalFrame=integer(*file_);s.ticks=integer(*file_);ticks+=s.ticks;if(!s.originalFrame||!s.ticks||s.ticks>100000||ticks>1000000)throw std::runtime_error("HAA1 slide time bound");
                for(uint32_t a=0;a<angles;++a){auto id=integer(*file_);if(id>=frames_.size()||frames_[id].image.originalAngle!=c.originalAngleIndices[a])throw std::runtime_error("HAA1 slide frame binding invalid");s.angleFrames.push_back(id);}
                s.sourceEventsJson=text(*file_,65536);c.slides.push_back(std::move(s));
            }
            clips_.push_back(std::move(c));
        }
        if(file_->remaining())throw std::runtime_error("Unexpected HAA1 suffix");
        if((facingFilename==nullptr)!=(identityFilename==nullptr))throw std::runtime_error("Explicit actor facing requires matching identity filename");
        const bool canonical=std::string_view(filename)=="actor-animations.bin";
        const char* facingName=facingFilename?facingFilename:(canonical?"actor-facing.bin":nullptr);
        if(auto companion=facingName?store.open(facingName):nullptr){
            if(companion->size()!=44+720*4+clips_.size()*24||integer(*companion)!=0x31464148)throw std::runtime_error("HAF1 extent/version mismatch");
            std::array<uint8_t,32> bound{},expected{};read(*companion,bound.data(),bound.size());auto identity=store.open(identityFilename?identityFilename:"actor-animations-sha256.bin");
            if(!identity||identity->size()!=32)throw std::runtime_error("Missing actor animation identity");read(*identity,expected.data(),expected.size());if(bound!=expected)throw std::runtime_error("HAF1 actor bank hash mismatch");
            if(integer(*companion)!=clips_.size()||integer(*companion)!=720)throw std::runtime_error("HAF1 counts mismatch");
            for(uint32_t n=0;n<720;++n){auto v=real(*companion);if(v<0||v>6.283186f)throw std::runtime_error("HAF1 invalid original angle LUT");inverseAngleTable_.push_back(v);}
            for(size_t n=0;n<clips_.size();++n){Facing f;auto iso=integer(*companion);f.ownAngle=real(*companion);f.angleOffset=real(*companion);f.angleNumOffset=static_cast<int32_t>(integer(*companion));auto mirror=integer(*companion),ignore=integer(*companion);if(iso>1||mirror>1||ignore>1||f.angleNumOffset<-1024||f.angleNumOffset>1024)throw std::runtime_error("HAF1 invalid native angle metadata");f.isometric=iso;f.mirror=mirror;f.ignoreOwner=ignore;facing_.push_back(f);}
        }
        if(canonical)loadProperties(store);
    }catch(...){reset();throw;}
}
void ActorAnimationBank::loadProperties(AssetStore& store,const char* filename,const char* identityFilename){
    properties_.clear();auto packet=store.open(filename);if(!packet)return;
    if(packet->size()<40||packet->size()>2*1024*1024)throw std::runtime_error("Invalid HAP extent");
    const auto magic=integer(*packet);if(magic!=0x31504148&&magic!=0x32504148&&magic!=0x33504148&&magic!=0x34504148&&magic!=0x35504148)throw std::runtime_error("Invalid HAP version");const bool v5=magic==0x35504148,v4=magic==0x34504148||v5,v3=magic==0x33504148||v4,v2=magic==0x32504148||v3;
    std::array<uint8_t,32> bound{},expected{};read(*packet,bound.data(),32);auto identity=store.open(identityFilename);
    if(!identity||identity->size()!=32)throw std::runtime_error("Missing HAP1 bank identity");read(*identity,expected.data(),32);
    if(bound!=expected||integer(*packet)!=clips_.size())throw std::runtime_error("HAP1 bank binding/count mismatch");
    std::vector<Properties> parsed;parsed.reserve(clips_.size());
    for(const auto& clip:clips_){Properties p;p.known=integer(*packet);p.values=integer(*packet);auto count=integer(*packet);
        if(p.known>(v5?511u:(v4?255u:(v3?63u:(v2?7u:3u))))||(p.values&~(p.known&~4u))||count!=clip.slides.size())throw std::runtime_error("HAP property/slide mismatch");
        if(v2){p.airborneZ=real(*packet);if(std::abs(p.airborneZ)>100000.f||(!(p.known&4u)&&p.airborneZ!=0))throw std::runtime_error("HAP AirborneZ bound/unknown mismatch");}
        for(const auto& slide:clip.slides){auto frame=integer(*packet),air=integer(*packet);
            if(frame!=slide.originalFrame||air>2||((clip.flags&ActorBook)&&air!=1))throw std::runtime_error("HAP1 original slide/airborne mismatch");
            p.airborne.push_back(static_cast<uint8_t>(air));}
        parsed.push_back(std::move(p));}
    if(packet->remaining())throw std::runtime_error("Unexpected HAP1 suffix");properties_=std::move(parsed);
}
std::optional<bool> ActorAnimationBank::exactOwnerImmobile(size_t clip)const{
    (void)clips_.at(clip);if(properties_.size()!=clips_.size()||!(properties_[clip].known&1))return {};return bool(properties_[clip].values&1);
}
std::optional<bool> ActorAnimationBank::exactBlockNewOwnerAnims(size_t clip)const{
    (void)clips_.at(clip);if(properties_.size()!=clips_.size()||!(properties_[clip].known&2))return {};return bool(properties_[clip].values&2);
}
std::optional<bool> ActorAnimationBank::exactDisableOwnerInteract(size_t clip)const{(void)clips_.at(clip);if(properties_.size()!=clips_.size()||!(properties_[clip].known&64u))return {};return bool(properties_[clip].values&64u);}
std::optional<bool> ActorAnimationBank::exactDisableOwnerManualInteract(size_t clip)const{(void)clips_.at(clip);if(properties_.size()!=clips_.size()||!(properties_[clip].known&128u))return {};return bool(properties_[clip].values&128u);}
std::optional<bool> ActorAnimationBank::exactOwnerInvulnerable(size_t clip)const{(void)clips_.at(clip);if(properties_.size()!=clips_.size()||!(properties_[clip].known&8u))return {};return bool(properties_[clip].values&8u);}
std::optional<bool> ActorAnimationBank::exactOwnerNoArmorCoverage(size_t clip)const{(void)clips_.at(clip);if(properties_.size()!=clips_.size()||!(properties_[clip].known&16u))return {};return bool(properties_[clip].values&16u);}
std::optional<bool> ActorAnimationBank::exactOwnerNoInvulnerableCoverage(size_t clip)const{(void)clips_.at(clip);if(properties_.size()!=clips_.size()||!(properties_[clip].known&32u))return {};return bool(properties_[clip].values&32u);}
std::optional<bool> ActorAnimationBank::exactUseAttachedFlasher(size_t clip)const{(void)clips_.at(clip);if(properties_.size()!=clips_.size()||!(properties_[clip].known&256u))return {};return bool(properties_[clip].values&256u);}
std::optional<bool> ActorAnimationBank::exactAirborne(size_t clip,size_t slide)const{
    const auto& c=clips_.at(clip);(void)c.slides.at(slide);if(c.flags&ActorBook)return false;
    if(properties_.size()!=clips_.size()||!properties_[clip].airborne[slide])return {};return properties_[clip].airborne[slide]==2;
}
std::optional<float> ActorAnimationBank::exactAnimationZ(size_t clip)const{
    (void)clips_.at(clip);if(properties_.size()!=clips_.size()||!(properties_[clip].known&4))return {};return properties_[clip].airborneZ;
}
std::optional<size_t> ActorAnimationBank::find(std::string_view name)const{for(size_t i=0;i<clips_.size();++i)if(clips_[i].name==name)return i;return {};}
uint32_t ActorAnimationBank::originalAngleForFacing(size_t clip,float ownerRadians)const{
    const auto& c=clips_.at(clip);if(c.originalAngles==1)return 0;if(facing_.size()!=clips_.size()||inverseAngleTable_.size()!=720)throw std::runtime_error("Original actor facing companion unavailable");
    if(!std::isfinite(ownerRadians)||std::abs(ownerRadians)>100000.f)throw std::runtime_error("Actor facing angle bound");
    const auto& f=facing_[clip];const int angles=static_cast<int>(c.originalAngles);if(angles==1)return 0;
    float angle=f.ownAngle+(f.ignoreOwner?0:ownerRadians);
    if(f.isometric){const volatile float scaled=angle*std::bit_cast<float>(0x42e52ee6u);int index=static_cast<int>(scaled+.5f)%720;if(index<0)index+=720;angle=inverseAngleTable_[index];}
    const float interval=std::bit_cast<float>(0x40c90fdbu)/float(angles);
    int native=static_cast<int>((angle-f.angleOffset+interval*.5f)/interval)+f.angleNumOffset;
    native%=angles;if(native<0)native+=angles;
    if(f.mirror&&native>angles/4&&native<angles*3/4)native=native>angles/2?angles-native+angles/2:angles/2-native;
    return static_cast<uint32_t>(native);
}
uint32_t ActorAnimationBank::selectedAngleForFacing(size_t clip,float ownerRadians)const{
    const auto& c=clips_.at(clip);const int angles=static_cast<int>(c.originalAngles),native=static_cast<int>(originalAngleForFacing(clip,ownerRadians));
    uint32_t selected=0;int best=angles;
    for(uint32_t i=0;i<c.originalAngleIndices.size();++i){int distance=std::abs(native-static_cast<int>(c.originalAngleIndices[i]));distance=std::min(distance,angles-distance);if(distance<best){best=distance;selected=i;}}
    return selected;
}
const ActorAnimationBank::Cached& ActorAnimationBank::cached(uint32_t frame,bool prefetch){
    for(auto& c:cache_)if(c.frame==frame){c.use=++use_;++stats_.hits;return c;}
    const auto& m=frames_.at(frame);
    while(!cache_.empty()&&(cache_.size()>=CacheFrames||stats_.cachedBytes+m.rawBytes>CacheBytes)){
        auto old=std::min_element(cache_.begin(),cache_.end(),[](const auto& a,const auto& b){return a.use<b.use;});stats_.cachedBytes-=old->rgba.size();cache_.erase(old);
    }
    compressed_.resize(m.packedBytes);if(!file_->seek(m.offset))throw std::runtime_error("HAA1 frame seek");read(*file_,compressed_.data(),compressed_.size());Cached c{frame,++use_,{}};c.rgba.resize(m.rawBytes);
    z_stream stream{};stream.next_in=compressed_.data();stream.avail_in=static_cast<uInt>(compressed_.size());stream.next_out=c.rgba.data();stream.avail_out=static_cast<uInt>(c.rgba.size());
    const auto start=std::chrono::steady_clock::now();if(inflateInit(&stream)!=Z_OK)throw std::runtime_error("HAA1 inflater init");const auto code=inflate(&stream,Z_FINISH);const bool valid=code==Z_STREAM_END&&stream.total_in==m.packedBytes&&stream.total_out==m.rawBytes;inflateEnd(&stream);
    if(!valid||crc32(0,c.rgba.data(),static_cast<uInt>(c.rgba.size()))!=m.crc)throw std::runtime_error("HAA1 frame decode/checksum failed");
    stats_.inflateMilliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();++stats_.misses;stats_.prefetches+=prefetch;stats_.inflateBytes+=m.rawBytes;stats_.compressedBytes+=m.packedBytes;stats_.cachedBytes+=c.rgba.size();cache_.push_back(std::move(c));stats_.cachedFrames=cache_.size();return cache_.back();
}
ActorAnimationImage ActorAnimationBank::sample(size_t clip,size_t slide,uint32_t angle,bool prefetchNext){
    const auto& c=clips_.at(clip);const auto& s=c.slides.at(slide);if(angle>=s.angleFrames.size())throw std::runtime_error("HAA1 selected angle bound");
    if(false){auto next=slide+1;if(next==c.slides.size()&&(c.flags&ActorLoop))next=0;if(next<c.slides.size())cached(c.slides[next].angleFrames[angle],true);else if(!c.chain.empty()){if(auto target=find(c.chain)){const auto& first=clips_[*target].slides[0];cached(first.angleFrames[std::min<size_t>(angle,first.angleFrames.size()-1)],true);}}}
    (void)prefetchNext;auto out=frames_.at(s.angleFrames[angle]).image;out.decoder=this;return out;
}
std::span<const uint8_t> ActorAnimationImage::pixels()const{
    if(!rgba.empty()||!decoder)return rgba;
    const auto decoded=decoder->decode(packetFrameIndex);if(decoded.size()!=byteCount())throw std::runtime_error("HAA1 lazy frame size mismatch");return decoded;
}
void ActorAnimationPlayback::reset(){bank_=nullptr;clip_=slide_=0;remaining_=0;playSpeedMultiplier_=1;active_=finished_=false;}
void ActorAnimationPlayback::play(const ActorAnimationBank& bank,std::string_view name,const SlideEvent& onSlide){
    auto index=bank.find(name);if(!index)throw std::runtime_error("Unsupported source actor animation: "+std::string(name));bank_=&bank;clip_=*index;slide_=0;active_=true;finished_=false;playSpeedMultiplier_=1;const auto& c=bank_->clips().at(clip_);remaining_=(c.flags&ActorBook)?c.secondsPerTick:float(c.slides[0].ticks)/60.f;if(onSlide)onSlide(c,c.slides[0]);
}
// SlideStart2b40e7..2b4136 replaces the current per-instance slide ticks
// with Deterministic.RandomInt(min,max), then adds ticks/60 to remaining.
// Call exactly once at the source slide-entry callback, before slide FX.
void ActorAnimationPlayback::replaceCurrentSlideTicks(uint32_t ticks){
    const auto* c=clip();if(!c||(c->flags&ActorBook)||ticks>100000)throw std::runtime_error("Actual random Slide duration outside source bounds");
    remaining_+=(float(ticks)-float(c->slides.at(slide_).ticks))/60.f;
}
void ActorAnimationPlayback::setPlaySpeedMultiplier(float multiplier){
    if(!std::isfinite(multiplier)||multiplier<0||multiplier>100)throw std::runtime_error("Actor animation play-speed multiplier bound");
    playSpeedMultiplier_=multiplier;
}
const ActorAnimationClip* ActorAnimationPlayback::clip()const{return active_&&bank_?&bank_->clips().at(clip_):nullptr;}
void ActorAnimationPlayback::restartSource(const SlideEvent& onSlide){
    if(!active_||!bank_)throw std::runtime_error("Source restart requires actual active animation lifetime");
    finished_=false;const auto& c=bank_->clips().at(clip_);if(!(c.flags&ActorAllowRestart))return;
    slide_=0;remaining_=(c.flags&ActorBook)?c.secondsPerTick:float(c.slides[0].ticks)/60.f;
    // Native Slide restart reruns CleanupSlideInfos/SetupSlideIndex. Book
    // SetFrame marks its source frame dirty; it does not itself run slide FX.
    if(!(c.flags&ActorBook)&&onSlide)onSlide(c,c.slides[0]);
}
uint32_t ActorAnimationPlayback::originalFrame()const{auto* c=clip();return c?c->slides.at(slide_).originalFrame:0;}
std::optional<bool> ActorAnimationPlayback::animationBlocksAttack()const{auto* c=clip();if(!c)return {};return bool(c->flags&ActorDisableWeapons);}
std::optional<std::string> ActorAnimationPlayback::advance(double seconds,float speed,const SlideEvent& onSlide){
    if(!std::isfinite(seconds)||seconds<0||seconds>1||!std::isfinite(speed)||speed<0||speed>100)throw std::runtime_error("Actor animation time bound");
    if(!active_||finished_)return {};const auto& c=bank_->clips().at(clip_);remaining_-=float(seconds)*speed;
    // Native Slide PostUpdate advances at most one positive-duration slide;
    // Book fractional frames can cross several source frames in one update.
    uint32_t transitions=0;
    do{
        const bool book=c.flags&ActorBook;
        if(book?remaining_>0.f:remaining_>=.0001f)break;
        if(++transitions>10000)throw std::runtime_error("Actor animation transition bound");
        if(slide_+1<c.slides.size())++slide_;
        else if(c.flags&ActorLoop)slide_=0;
        else{finished_=true;return c.chain.empty()?std::optional<std::string>{}:c.chain;}
        remaining_+=book?c.secondsPerTick:float(c.slides[slide_].ticks)/60.f;if(onSlide)onSlide(c,c.slides[slide_]);
        if(!book)break;
    }while(true);
    return {};
}
}
