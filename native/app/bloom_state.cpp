#include "bloom_state.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace hades {
namespace {
std::string lower(std::string s){for(char&c:s)if(c>='A'&&c<='Z')c+=char('a'-'A');return s;}
float smooth(float a,float b,float amount){
 const float clamped=std::clamp(amount,0.f,1.f);if(clamped==0)return a;if(clamped==1)return b;
 const double x=clamped,aa=a,bb=b;
 // Native SmoothStep uses double Hermite intermediates and casts final result.
 return float(((2*aa-2*bb)*x*x*x+(3*bb-3*aa)*x*x)+aa);
}
bool finite(const BloomSettings&s){return std::isfinite(s.threshold)&&std::isfinite(s.blur)&&std::isfinite(s.bloomIntensity)&&std::isfinite(s.baseIntensity)&&std::isfinite(s.bloomSaturation)&&std::isfinite(s.baseSaturation);}
}
void FullscreenBloom::load(AssetStore&assets){
 if(!definitions_.empty())throw std::runtime_error("Original bloom definitions already loaded");
 auto file=assets.open("bloom-settings.bin");if(!file||file->size()<8||file->size()>65536)throw std::runtime_error("Original bloom packet missing/bounds");
 const auto read=[&](void*p,size_t n){size_t done=0;while(done<n){const auto got=file->read(static_cast<unsigned char*>(p)+done,n-done);if(!got)throw std::runtime_error("Truncated original bloom packet");done+=got;}};
 const auto u32=[&](){unsigned char b[4];read(b,4);return unsigned(b[0])|(unsigned(b[1])<<8)|(unsigned(b[2])<<16)|(unsigned(b[3])<<24);};
 const auto scalar=[&](){auto bits=u32();float f;std::memcpy(&f,&bits,4);return f;};
 char magic[4];read(magic,4);if(std::memcmp(magic,"HBL1",4))throw std::runtime_error("Original bloom packet version");const auto count=u32();if(!count||count>256)throw std::runtime_error("Original bloom count bounds");
 std::map<std::string,BloomSettings> loaded;
 for(unsigned i=0;i<count;++i){const auto length=u32();if(!length||length>256)throw std::runtime_error("Original bloom name bounds");BloomSettings s;s.name.resize(length);read(s.name.data(),length);if(s.name.find('\0')!=std::string::npos)throw std::runtime_error("Invalid original bloom name");s.threshold=scalar();s.blur=scalar();s.bloomIntensity=scalar();s.baseIntensity=scalar();s.bloomSaturation=scalar();s.baseSaturation=scalar();s.isNull=false;if(!finite(s)||!loaded.emplace(lower(s.name),s).second)throw std::runtime_error("Invalid/duplicate original bloom definition");}
 if(file->remaining()||!loaded.count("off"))throw std::runtime_error("Original bloom packet trailing data/missing Off");definitions_=std::move(loaded);std::string error;if(!resetOff(error))throw std::runtime_error(error);
}
bool FullscreenBloom::resetOff(std::string&error){const auto i=definitions_.find("off");if(i==definitions_.end()){error="Original bloom definitions unavailable";return false;}current_=i->second;previous_.isNull=true;target_.isNull=true;error.clear();return true;}
bool FullscreenBloom::adjust(const std::string&name,float duration,bool useWorldTime,float worldTime,float screenTime,std::string&error){
 if(definitions_.empty()){error="Original bloom definitions unavailable";return false;}
 if(!std::isfinite(duration)||!std::isfinite(worldTime)||!std::isfinite(screenTime)){error="Nonfinite native bloom transition";return false;}
 const auto found=definitions_.find(lower(name));if(found==definitions_.end()){error.clear();return true;} // Native null settings log and skip action.
 if(duration==0){current_=found->second;error.clear();return true;} // Does not cancel an earlier target.
 previous_=current_;target_=found->second;duration_=duration;worldTime_=useWorldTime;start_=useWorldTime?worldTime:screenTime;error.clear();return true;
}
bool FullscreenBloom::advance(float worldTime,float screenTime,bool drawBloom,std::string&error){
 if(!std::isfinite(worldTime)||!std::isfinite(screenTime)){error="Nonfinite native bloom clock";return false;}
 if(!drawBloom||target_.isNull){error.clear();return true;}
 const float fraction=((worldTime_?worldTime:screenTime)-start_)/duration_;
 current_.baseIntensity=smooth(previous_.baseIntensity,target_.baseIntensity,fraction);current_.baseSaturation=smooth(previous_.baseSaturation,target_.baseSaturation,fraction);current_.bloomIntensity=smooth(previous_.bloomIntensity,target_.bloomIntensity,fraction);current_.bloomSaturation=smooth(previous_.bloomSaturation,target_.bloomSaturation,fraction);current_.threshold=smooth(previous_.threshold,target_.threshold,fraction);current_.blur=smooth(previous_.blur,target_.blur,fraction);
 if(fraction>=1){current_=target_;target_.isNull=true;}error.clear();return true;
}
bool FullscreenBloom::shouldDraw(bool enabled)const{return enabled&&(current_.threshold!=0||current_.blur!=0||current_.bloomIntensity!=0||current_.baseIntensity!=1||current_.bloomSaturation!=1||current_.baseSaturation!=1);}
}
