#include "actor_marker.hpp"
#include <cmath>
#include <stdexcept>

#ifdef __clang__
#pragma clang fp contract(off)
#endif

namespace hades {
namespace {
void read(AssetFile& file,void* to,size_t bytes){if(bytes>file.remaining())throw std::runtime_error("Truncated HMR1");auto* p=static_cast<uint8_t*>(to);while(bytes){const auto n=file.read(p,bytes);if(!n)throw std::runtime_error("HMR1 read failed");bytes-=n;p+=n;}}
uint32_t integer(AssetFile& file){uint32_t n;read(file,&n,4);return n;}
float real(AssetFile& file){float n;read(file,&n,4);if(!std::isfinite(n))throw std::runtime_error("Non-finite original marker");return n;}
std::string text(AssetFile& file){const auto n=integer(file);if(n>4096)throw std::runtime_error("HMR1 string bound");std::string s(n,'\0');read(file,s.data(),n);if(s.empty()||s.find('\0')!=std::string::npos)throw std::runtime_error("Invalid HMR1 name");return s;}
}
void ActorMarkerBank::load(AssetStore& store,const ActorAnimationBank& animations,const char* filename){
 reset();try{
  auto file=store.open(filename);if(!file||file->size()>32*1024*1024||integer(*file)!=0x31524d48)throw std::runtime_error("Missing/invalid HMR1 original marker bank");
  std::array<uint8_t,32> bound{},expected{};read(*file,bound.data(),32);auto identity=store.open("actor-animations-sha256.bin");if(!identity||identity->size()!=32)throw std::runtime_error("HMR1 bank identity missing");read(*identity,expected.data(),32);if(bound!=expected)throw std::runtime_error("HMR1 actor bank hash mismatch");
  const auto sets=integer(*file),clips=integer(*file);if(!sets||sets>256||clips!=animations.clips().size())throw std::runtime_error("HMR1 counts mismatch");
  uint64_t total=0;for(uint32_t n=0;n<sets;++n){DataSet d;const auto tracks=integer(*file);if(!tracks||tracks>128)throw std::runtime_error("HMR1 track count bound");for(uint32_t i=0;i<tracks;++i){auto name=text(*file);Track t;t.first=integer(*file);t.last=integer(*file);t.width=integer(*file);t.height=integer(*file);const auto points=integer(*file);total+=points;if(!t.first||t.last<t.first||t.last-t.first+1!=points||points>1000000||total>2000000||!t.width||!t.height||t.width>16384||t.height>16384||d.tracks.contains(name))throw std::runtime_error("HMR1 track bounds");for(uint32_t p=0;p<points;++p){auto x=real(*file),y=real(*file);if(std::abs(x)>100||std::abs(y)>100)throw std::runtime_error("HMR1 normalized marker bound");t.points.push_back({x,y});}d.tracks.emplace(std::move(name),std::move(t));}data_.push_back(std::move(d));}
  for(uint32_t n=0;n<clips;++n){Clip c;c.name=text(*file);c.dataSet=integer(*file);c.frames=integer(*file);c.angles=integer(*file);c.source={real(*file),real(*file),real(*file)};const auto& source=animations.clips()[n];if(c.name!=source.name||c.dataSet>=data_.size()||!c.frames||c.frames>10000||c.angles!=source.originalAngles||c.source.ownScale<=0||c.source.ownScale>16)throw std::runtime_error("HMR1 clip binding mismatch");for(const auto& slide:source.slides)if(slide.originalFrame>c.frames)throw std::runtime_error("HMR1 source frame mismatch");clips_.push_back(std::move(c));}
  if(file->remaining())throw std::runtime_error("Unexpected HMR1 suffix");
 }catch(...){reset();throw;}
}
bool ActorMarkerBank::hasMarker(size_t clip,std::string_view name)const{const auto& c=clips_.at(clip);return data_.at(c.dataSet).tracks.contains(std::string(name));}
ActorMarkerPoint ActorMarkerBank::sample(size_t clip,uint32_t frame,uint32_t angle,std::string_view name)const{
 const auto& c=clips_.at(clip);if(!frame||frame>c.frames||angle>=c.angles)throw std::runtime_error("Original marker frame/angle outside source bounds");const auto& tracks=data_.at(c.dataSet).tracks;auto i=tracks.find(std::string(name));if(i==tracks.end())throw std::runtime_error("Missing actual original marker: "+std::string(name));const auto& t=i->second;const uint64_t index=uint64_t(angle)*c.frames+frame;if(index<t.first||index>t.last)return {0,0,t.width,t.height};const auto& p=t.points.at(size_t(index-t.first));return{p[0],p[1],t.width,t.height};
}
void ActorMarkerBank::applySourceTransform(size_t clip,float ownerScale,float ownMultiplier,ActorMarkerTransform& out)const{
 if(!std::isfinite(ownerScale)||!std::isfinite(ownMultiplier)||ownerScale<0||ownMultiplier<0)throw std::runtime_error("Actual native marker scale unresolved");const auto source=sourceTransform(clip);out.ownerBodyScale=ownerScale;out.nativeAnimationScale=source.ownScale*ownMultiplier*ownerScale;out.animationOffsetX=source.offsetX;out.animationOffsetY=source.offsetY;
}
std::array<float,2> ActorMarkerBank::location(const ActorMarkerPoint& p,const ActorMarkerTransform& t){
 if(!t.resolved)throw std::runtime_error("Actual native marker transform unresolved");for(float v:{t.locationX,t.locationY,t.attachedThingZ,t.drawAngle,t.nativeAnimationScale,t.ownerBodyScale,t.animationOffsetX,t.animationOffsetY,t.shakeX,t.shakeY})if(!std::isfinite(v))throw std::runtime_error("Non-finite actual marker transform");if(t.nativeAnimationScale<0||t.ownerBodyScale<0)throw std::runtime_error("Original marker scale unresolved");
 const float s=std::sin(t.drawAngle),c=std::cos(t.drawAngle);const float x=c*p.x-s*p.y,y=c*p.y+s*p.x;
 return {t.locationX+t.ownerBodyScale*t.animationOffsetX+float(p.width)*.5f*t.nativeAnimationScale*x+t.shakeX,t.locationY+t.ownerBodyScale*t.animationOffsetY-float(p.height)*.5f*t.nativeAnimationScale*y-t.attachedThingZ*0.8660253882408142f+t.shakeY};
}
}
