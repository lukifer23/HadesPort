#include "audio_chorus.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
namespace hades { namespace {
// create55000 generates this quarter-cosine table with float input to cosf.
const std::array<float,8192>& cosineTable(){static const auto values=[](){std::array<float,8192> a{};for(size_t i=0;i<a.size();++i)a[i]=std::cos(float(i)*0.0001917476038215682f);return a;}();return values;}
float oscillator(float phase){const int32_t signedIndex=int32_t(phase*32768.f);const uint32_t index=uint32_t(signedIndex<0?-signedIndex:signedIndex)&32767u;const auto& table=cosineTable();switch(index>>13){case 0:return table[index];case 1:return -table[16383-index];case 2:return -table[index-16384];default:return table[32767-index];}}
// read55862 truncates to signed32, then stores the low16 bits, without clipping.
int16_t quantize(float sample){const float scaled=sample*16384.f;const int32_t value=std::isfinite(scaled)&&scaled>=-2147483648.f&&scaled<2147483648.f?int32_t(scaled):std::numeric_limits<int32_t>::min();return std::bit_cast<int16_t>(uint16_t(uint32_t(value)));}
}
bool AudioChorus::configure(float sampleRate,const AudioChorusData& data,std::string& error){
 if(!std::isfinite(sampleRate)||sampleRate<8000||sampleRate>192000||!std::isfinite(data.mixPercent)||!std::isfinite(data.rateHz)||!std::isfinite(data.depthPercent)||data.mixPercent<0||data.mixPercent>100||data.rateHz<0||data.rateHz>20||data.depthPercent<0||data.depthPercent>100){error="Original Chorus parameters outside descriptor range";return false;}
 data_=data;sampleRate_=sampleRate;phaseStep_=data.rateHz/sampleRate;targetDepth_=(sampleRate*data.depthPercent)*(data.depthPercent*0.00001f);
 dry_=200.f/(data.mixPercent+200.f);wet_[0]=(std::clamp(data.mixPercent+data.mixPercent,0.f,100.f)*0.01f)*dry_;wet_[1]=(std::clamp(data.mixPercent*4.f-200.f,0.f,100.f)*0.01f)*dry_;wet_[2]=(std::clamp(data.mixPercent*4.f-300.f,0.f,100.f)*0.01f)*dry_;
 // Native554f2 rounds120ms to System DSP-buffer multiples. This portable
 // Processor consumes one frame at a time, so its allocation block is1.
 frames_=std::max(2u,uint32_t(int32_t(sampleRate*120.f)/1000)+1u);channels_=0;delay_.clear();reset();error.clear();return true;
}
void AudioChorus::reset(){std::fill(delay_.begin(),delay_.end(),int16_t{});write_=0;phase_=0;depth_=targetDepth_;}
bool AudioChorus::process(float* interleaved,size_t frameCount,size_t channels,std::string& error){
 if(!interleaved||!sampleRate_||channels<1||channels>32){error="Original Chorus source format unavailable";return false;}
 if(channels_!=channels){channels_=uint32_t(channels);delay_.assign(size_t(frames_)*channels_,int16_t{});reset();}
 // 555c9 stores float(frameCount), then decrements its representation: the
 // wrap boundary is the preceding float, not a subtraction of one sample.
 const float boundary=std::bit_cast<float>(std::bit_cast<uint32_t>(float(frames_))-1u);
 constexpr std::array<float,3> offsets{0,.25f,.5f};
 for(size_t frame=0;frame<frameCount;++frame){auto* input=interleaved+frame*channels;
  for(size_t c=0;c<channels;++c)delay_[size_t(write_)*channels+c]=quantize(input[c]);
  if(depth_<targetDepth_-.03125f)depth_+=.03125f;else if(depth_>targetDepth_+.03125f)depth_-=.03125f;else depth_=targetDepth_;
  for(size_t c=0;c<channels;++c){const size_t voice=c%3;float position=(oscillator(phase_+offsets[voice])*.5f-.5f)*depth_+float(write_);if(position<0)position+=boundary;else if(position>boundary)position-=boundary;
   const uint32_t at=uint32_t(position);if(at>=frames_){error="Original Chorus delayed sample outside live ring";return false;}const float fraction=position-float(at);const float first=float(delay_[size_t(at)*channels+c])*0.00006103515625f,second=float(delay_[size_t((at+1)%frames_)*channels+c])*0.00006103515625f;
   input[c]=(first*(1.f-fraction)+second*fraction)*wet_[voice]+input[c]*dry_;
  }
  phase_+=phaseStep_;if(phase_>1)phase_-=1;if(++write_==frames_)write_=0;
 }
 error.clear();return true;
}
}
