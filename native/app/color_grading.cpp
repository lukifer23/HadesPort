#include "color_grading.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <zlib.h>
namespace hades {
namespace {
std::string lower(std::string v){for(char&c:v)if(c>='A'&&c<='Z')c+=char('a'-'A');return v;}
}
void ColorGrading::load(AssetStore&assets){
 if(!volumes_.empty())throw std::runtime_error("Original color grading already loaded");
 auto file=assets.open("color-grading.bin");if(!file||file->size()<8||file->size()>16*1024*1024)throw std::runtime_error("Original color grading packet missing/bounds");
 const auto read=[&](void*p,size_t n){if(n>file->remaining())throw std::runtime_error("Truncated original color grading");size_t offset=0;while(offset<n){const auto got=file->read(static_cast<uint8_t*>(p)+offset,n-offset);if(!got)throw std::runtime_error("Truncated original color grading read");offset+=got;}};
 const auto integer=[&](){uint8_t b[4];read(b,4);return uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24);};
 char magic[4];read(magic,4);if(std::memcmp(magic,"HCG1",4))throw std::runtime_error("Original color grading packet version");const auto count=integer();if(!count||count>256)throw std::runtime_error("Original LUT count bounds");
 std::map<std::string,ColorVolume> loaded;
 for(uint32_t i=0;i<count;++i){const auto length=integer();if(!length||length>256)throw std::runtime_error("Original LUT name bounds");std::string name(length,'\0');read(name.data(),length);if(name.find('\0')!=std::string::npos)throw std::runtime_error("Invalid original LUT name");const auto size=integer(),raw=integer(),packed=integer(),crc=integer();if(size<2||size>64||raw!=uint64_t(size)*size*size*4||!packed||packed>raw+1024)throw std::runtime_error("Original LUT dimensions/storage bounds");std::vector<uint8_t> compressed(packed);read(compressed.data(),compressed.size());ColorVolume v;v.size=size;v.rgba.resize(raw);uLongf bytes=raw;if(uncompress(v.rgba.data(),&bytes,compressed.data(),compressed.size())!=Z_OK||bytes!=raw||crc32(0,v.rgba.data(),v.rgba.size())!=crc)throw std::runtime_error("Original LUT integrity failure");if(!loaded.emplace(lower(name),std::move(v)).second)throw std::runtime_error("Duplicate original LUT");}
 if(file->remaining())throw std::runtime_error("Trailing original color grading packet");volumes_=std::move(loaded);
}
bool ColorGrading::set(const std::string&name,float duration,bool useWorldTime,float worldTime,float screenTime,std::string&error){
 if(!std::isfinite(duration)||!std::isfinite(worldTime)||!std::isfinite(screenTime)){error="Nonfinite native color grading transition";return false;}
 if(volumes_.empty()){error="Original color grading resources unavailable";return false;}
 const auto found=volumes_.find(lower(name));previous_=current_;previousName_=currentName_;current_=found==volumes_.end()?nullptr:&found->second;currentName_=found==volumes_.end()?"Off":name;duration_=duration;worldTime_=useWorldTime;start_=useWorldTime?worldTime:screenTime;lerp_=duration>0?0:1;error.clear();return true;
}
bool ColorGrading::advance(float worldTime,float screenTime,std::string&error){if(!std::isfinite(worldTime)||!std::isfinite(screenTime)){error="Nonfinite native color grading clock";return false;}lerp_=duration_>0?std::clamp(((worldTime_?worldTime:screenTime)-start_)/duration_,0.f,1.f):1.f;error.clear();return true;}
}
