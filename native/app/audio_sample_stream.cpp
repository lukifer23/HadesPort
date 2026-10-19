#include "audio_sample_stream.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <vector>
namespace hades {
namespace {
// One normalized 32-tap Hann-windowed sinc per exact rational source phase.
// Phases = 48000/gcd(rate,48000); identical tables to the former fixed
// 32k (3) / 44.1k (160) instantiations. Built once per rate at open(), never
// in the per-sample path; entries are never erased (stable pointers).
using Kernel=std::array<double,32>;
const std::vector<Kernel>* resamplerKernels(int rate,std::string& error){
 static std::mutex mutex;static std::map<int,std::unique_ptr<std::vector<Kernel>>> tables;
 const size_t Phases=size_t(48000/std::gcd(rate,48000));
 if(Phases>1024){error="Audio resampler source rate needs more than 1024 exact phases";return nullptr;}
 std::lock_guard lock(mutex);auto& slot=tables[rate];if(slot)return slot.get();
 auto made=std::make_unique<std::vector<Kernel>>(Phases);auto& table=*made;
 constexpr double pi=3.1415926535897932384626433832795;
 for(size_t phase=0;phase<Phases;++phase){const double fraction=double(phase)/Phases;double total=0;
  for(int tap=-15;tap<=16;++tap){const double distance=double(tap)-fraction;
   const double sinc=distance==0?1.0:std::sin(pi*distance)/(pi*distance);
   const double window=std::fabs(distance)>=16?0:0.5+0.5*std::cos(pi*distance/16);
   table[phase][size_t(tap+15)]=sinc*window;total+=sinc*window;
  }
  for(auto& weight:table[phase])weight/=total;
 }slot=std::move(made);return slot.get();
}
}

bool AudioSampleStream::open(const AudioBankSample& sample,std::string& error){
    // Upsampling only: a source above 48 kHz would need an anti-alias filter
    // this kernel does not provide, so it is refused rather than aliased.
    if((sample.channels!=1&&sample.channels!=2)||sample.sampleRate<1000||sample.sampleRate>48000){error="Selected streaming audio requires original mono/stereo source at 1000..48000Hz";return false;}
    kernels_=nullptr;phaseDivisor_=0;
    if(sample.sampleRate!=48000){kernels_=resamplerKernels(sample.sampleRate,error);if(!kernels_)return false;phaseDivisor_=uint64_t(std::gcd(sample.sampleRate,48000));}
    if(!decoder_.open(sample,error))return false;sample_=sample;cacheStart_=-1;cacheFrames_=0;return true;
}
bool AudioSampleStream::value(int64_t frame,size_t channel,float& out,std::string& error){
    if(frame<0||frame>=sample_.frames){out=0;return true;}
    if(cacheStart_<0||frame<cacheStart_||uint64_t(frame-cacheStart_)>=cacheFrames_){
        // Keep preceding16 samples in the cache so a sequential kernel does not
        // bounce backwards and forwards at every source-buffer boundary.
        const size_t capacity=cache_.size()/size_t(sample_.channels);
        const int64_t oldEnd=cacheStart_+int64_t(cacheFrames_);
        size_t preserved=0;
        int64_t begin=std::max<int64_t>(0,frame-32);
        if(cacheStart_>=0&&frame==oldEnd&&decoder_.position()==oldEnd){
            preserved=std::min<size_t>(32,cacheFrames_);
            std::memmove(cache_.data(),cache_.data()+(cacheFrames_-preserved)*size_t(sample_.channels),preserved*size_t(sample_.channels)*sizeof(float));
            begin=oldEnd-int64_t(preserved);
        }else if(decoder_.position()!=begin&&!decoder_.seek(begin,error))return false;
        cacheStart_=begin;cacheFrames_=preserved;size_t decoded=0;
        if(!decoder_.readFrames(cache_.data()+preserved*size_t(sample_.channels),capacity-preserved,decoded,error))return false;
        cacheFrames_+=decoded;
        if(frame-cacheStart_>=static_cast<int64_t>(cacheFrames_)){error="Original audio decoder returned an unexpected short source window";return false;}
    }
    out=cache_[size_t(frame-cacheStart_)*sample_.channels+channel];return true;
}
bool AudioSampleStream::stereoAtTick(uint64_t tick,float& left,float& right,std::string& error){
    if(sample_.frames<=0||!sample_.sampleRate||tick>uint64_t(std::numeric_limits<int64_t>::max())/uint64_t(sample_.sampleRate)){error="Audio resampler requires an opened bounded source clock";return false;}
    if(sample_.sampleRate==48000){
        if(!value(int64_t(tick),0,left,error))return false;
        if(sample_.channels==2){if(!value(int64_t(tick),1,right,error))return false;}else right=left;
        if(!std::isfinite(left)||!std::isfinite(right)){error="Original48k source returned nonfinite PCM";return false;}
        error.clear();return true;
    }
    const uint64_t scaled=tick*uint64_t(sample_.sampleRate);
    const int64_t center=static_cast<int64_t>(scaled/48000);
    // Exact rational source phase (e.g. 147/160 for 44.1k, 2/3 for 32k,
    // 147/320 for 22.05k, 1/6 for 8k): all upsampling with the same
    // normalized 32-tap windowed sinc. No trig or allocation per sample.
    const auto& weights=(*kernels_)[size_t((scaled%48000)/phaseDivisor_)];
    double sum[2]={0,0};
    const int64_t first=center-15,last=center+16;
    float probe=0;
    // Fast path: the whole in-range window is resident, so read the cache
    // directly. The edge/refill path below keeps the exact original order.
    const bool inRange=first>=0&&last<sample_.frames;
    if(inRange&&!value(last,0,probe,error))return false;
    if(inRange&&first>=cacheStart_&&last<cacheStart_+int64_t(cacheFrames_)){
        const size_t channels=size_t(sample_.channels);const float* base=cache_.data()+size_t(first-cacheStart_)*channels;
        if(channels==2){for(size_t t=0;t<32;++t){const double w=weights[t];sum[0]+=double(base[2*t])*w;sum[1]+=double(base[2*t+1])*w;}}
        else for(size_t t=0;t<32;++t)sum[0]+=double(base[t])*weights[t];
        left=float(sum[0]);right=channels==1?left:float(sum[1]);
        if(!std::isfinite(left)||!std::isfinite(right)){error="Audio resampler output is nonfinite";return false;}
        error.clear();return true;
    }
    for(int tap=-15;tap<=16;++tap){
        const double weight=weights[size_t(tap+15)];
        for(int c=0;c<sample_.channels;++c){float v=0;if(!value(center+tap,size_t(c),v,error))return false;sum[c]+=double(v)*weight;}
    }
    left=float(sum[0]);right=sample_.channels==1?left:float(sum[1]);
    if(!std::isfinite(left)||!std::isfinite(right)){error="Audio resampler output is nonfinite";return false;}
    error.clear();return true;
}
}
