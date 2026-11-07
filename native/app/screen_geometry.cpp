#include "screen_animation.hpp"
#include "animation_transitions.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

#ifdef __clang__
#pragma clang fp contract(off)
#endif

namespace hades {namespace {
float number(const SourceValue&d,const char*key,float fallback=0){
 const auto*p=d.get(key);if(!p)return fallback;const double value=p->number();
 if(!std::isfinite(value)||std::abs(value)>100000)throw std::runtime_error(std::string("Screen geometry source scalar unsupported: ")+key);
 return float(value);
}
std::string text(const SourceValue&d,const char*key){const auto*p=d.get(key);return p?p->text():std::string{};}
bool flag(const SourceValue&d,const char*key){const auto*p=d.get(key);return p&&p->boolean();}
int32_t integer(float value){
 // CVTTSS2SI's indefinite/overflow result is an explicit unsupported boundary.
 const double truncated=std::trunc(double(value));
 if(!std::isfinite(value)||truncated<INT32_MIN||truncated>INT32_MAX)throw std::runtime_error("Screen geometry outside native signed32 conversion");
 return int32_t(truncated);
}
int32_t sum(int32_t a,int32_t b){const int64_t result=int64_t(a)+b;if(result<INT32_MIN||result>INT32_MAX)throw std::runtime_error("Screen geometry rectangle addition overflow");return int32_t(result);}
std::array<float,2> xy(const SourceValue*value){
 if(!value)return {};
 if(const auto*object=std::get_if<SourceValue::Object>(&value->value)){(void)object;return {number(*value,"X"),number(*value,"Y")};}
 const auto&array=value->array();if(array.size()!=2)throw std::runtime_error("Screen geometry source vector requires XY");
 std::array<float,2> result;for(size_t i=0;i<2;++i){const double n=array[i].number();if(!std::isfinite(n)||std::abs(n)>100000)throw std::runtime_error("Screen geometry source vector outside bounds");result[i]=float(n);}return result;
}
float scaleHelper(const SourceValue&d,const char*a,const char*z,float fraction){return animationScaleTransition(number(d,a,1),number(d,z,1),fraction,number(d,"EaseIn"),number(d,"EaseOut"),flag(d,"PingPongScale"));}
bool maintain(const SourceValue&d,const char*key){const auto value=text(d,key);if(value.empty()||value=="Maintain")return true;if(value=="Ignore")return false;throw std::runtime_error(std::string("Screen geometry FromOwner Take unsupported: ")+key);}
}

bool ScreenAnimationWorld::setGeometryDefinition(int32_t id,const SourceValue&properties,std::string&error){
 auto*s=thing(id);if(!s||!s->body||!s->body->entityHandle){error="Screen geometry original owner lifetime unavailable";return false;}
 if(s->geometrySource.known){error="Screen geometry original definition already bound";return false;}
 ScreenGeometrySource source;source.known=true;
 try{
  const auto*data=properties.get("Thing");if(!data)throw std::runtime_error("Original ThingData geometry properties unavailable");
  // ThingData ctor0x1800eb72f clears full+174/+178 Size; Points starts empty.
  // Actual source values override these native zeros, never a menu-sized default.
  source.size=xy(data->get("Size"));source.offset=xy(data->get("Offset"));
  if(const auto*points=data->get("Points"))source.hasPoints=!points->array().empty();
  source.extentScale=number(*data,"ExtentScale");source.relativeExtentScale=number(*data,"RelativeExtentScale");source.rotate=flag(*data,"RotateGeometry");
 }catch(const std::exception&e){source.unavailable=e.what();}
 // Preserve a source limitation without breaking unrelated noninteractive
 // Things during spawn. The interaction consumer rejects it before admission.
 s->geometrySource=std::move(source);error.clear();return true;
}

bool ScreenAnimationWorld::interactionGeometry(int32_t id,std::vector<std::array<float,2>>&polygon,std::array<int32_t,4>&rectangle,std::string&error)const{try{
 const auto*s=thing(id);
 if(!s||!s->body||!s->body->entityHandle||s->body->queryProxy||!s->body->screenSpace||!bank_||!registry_||!s->primary||s->layers.empty())throw std::runtime_error("Actual screen geometry owner/primary lifetime unavailable");
 const auto&source=s->geometrySource;const auto&body=*s->body;
 if(!source.known)throw std::runtime_error("Original screen Thing geometry definition unavailable");
 if(!source.unavailable.empty())throw std::runtime_error(source.unavailable);
 // SetupGeometry2dc890 prefers map/source Points, then nonzero source Size.
 // This consumer rebuilds the primary-derived branch, not a replacement for
 // either explicit source shape. Actual screen spawn has no MapThing points.
 if(source.hasPoints||source.size[0]!=0||source.size[1]!=0)throw std::runtime_error("Screen explicit Points/Size geometry branch not reconstructed: "+body.name);
 if(source.offset[0]!=0||source.offset[1]!=0||source.extentScale>0||source.relativeExtentScale>0||body.fromMap||body.flipHorizontal||(source.rotate&&body.angle!=0))throw std::runtime_error("Screen source/body geometry transform not reconstructed: "+body.name);
 if(s->ownParallax!=1||s->mapParallax!=1)throw std::runtime_error("Screen geometry parallax transform not reconstructed: "+body.name);
 const auto&layer=s->layers.front();const auto*life=registry_->find(layer.handle);
 if(!life||!life->initialized||life->handle!=s->primary||life->ownerLifetime!=body.entityHandle||life->ownerId!=id||layer.legacy||layer.movie)throw std::runtime_error("Actual GUI Constant geometry allocation unavailable: "+body.name);
 const auto&d=definitions_.at(layer.name);if(text(d,"Type")!="Constant")throw std::runtime_error("Screen primary geometry class not reconstructed: "+layer.name);
 const auto&clip=bank_->clips().at(layer.clip);
 if(clip.slides.size()!=1||clip.originalAngles!=1||clip.originalAngleIndices!=std::vector<uint32_t>{0})throw std::runtime_error("Screen Constant geometry atlas frame ambiguous: "+layer.name);
 // Constant.GetDrawBounds2b2dba..2b2e1d uses a conservative padded square
 // whenever an own/maintained owner angle helper is active, even at angle0.
 // Keep that separate branch explicit rather than rotate a sprite AABB here.
 if(number(d,"Angle")!=0||number(d,"StartAngle")!=number(d,"EndAngle")||(maintain(d,"AngleFromOwner")&&body.angle!=0)||number(d,"AttachedTextScalarX")>0||number(d,"AttachedTextScalarY")>0||number(d,"StartOffsetX")!=number(d,"EndOffsetX")||number(d,"StartOffsetY")!=number(d,"EndOffsetY"))throw std::runtime_error("Screen Constant spatial helper bounds not reconstructed: "+layer.name);
 const auto image=bank_->sample(layer.clip,0,0,false); // Metadata only, no decode.
 if(image.cropX||image.cropY||image.width!=image.canvasWidth||image.height!=image.canvasHeight||clip.offsetX!=0||clip.offsetY!=0)throw std::runtime_error("Original GUI atlas rectangle metadata unavailable: "+layer.name);
 float scalar=number(d,"Scale",1)*scaleHelper(d,"StartScale","EndScale",layer.progress);
 const float ownerScale=body.scale*body.scaleFraction;
 if(maintain(d,"ScaleFromOwner")){scalar*=body.scale;scalar*=body.scaleFraction;} // GetScale298c12,298c28: preserve separate float multiplies.
 const float vx=number(d,"ScaleX",1)*scaleHelper(d,"StartScaleX","EndScaleX",layer.progress)*s->modifier(2),vy=number(d,"ScaleY",1)*scaleHelper(d,"StartScaleY","EndScaleY",layer.progress)*s->modifier(3);
 // Native2b2cf3..2b2d01 multiplies atlas scale, scalar and vector in order.
 const float sx=(clip.worldScale*scalar)*vx,sy=(clip.worldScale*scalar)*vy;
 if(!std::isfinite(sx)||!std::isfinite(sy)||sx<=0||sy<=0||!std::isfinite(ownerScale)||ownerScale<=0)throw std::runtime_error("Screen Constant geometry scale unsupported: "+layer.name);
 // GetDrawLocation297d60 applies source draw offsets using the actual owner
 // scalar. The geometry call passes owner XY, zero Z and unit world scale.
 const float x=body.x+number(d,"OffsetX")*ownerScale,y=body.y+number(d,"OffsetY")*ownerScale;
 const std::array<int32_t,4> initial{integer(x-image.originX*sx),integer(y-image.originY*sy),integer(float(image.canvasWidth)*sx),integer(float(image.canvasHeight)*sy)};
 if(initial[2]<=0||initial[3]<=0)throw std::runtime_error("Screen Constant geometry rectangle has no positive extent: "+layer.name);
 const int32_t right=sum(initial[0],initial[2]),bottom=sum(initial[1],initial[3]);
 // FromRectangle120620 inserts BL,BR,TR,TL. The first y is a float add;
 // later corners use integer addition before conversion, as in the original.
 std::vector<std::array<float,2>> result{{float(initial[0]),float(initial[1])+float(initial[3])},{float(right),float(bottom)},{float(right),float(initial[1])},{float(initial[0]),float(initial[1])}};
 // Primary-derived SetupGeometry2dcd48 has already applied draw scale and
 // clears its extra owner-scale gate. Do not multiply the polygon again.
 float minX=result[0][0],minY=result[0][1],maxX=minX,maxY=minY;for(const auto&p:result){minX=std::min(minX,p[0]);minY=std::min(minY,p[1]);maxX=std::max(maxX,p[0]);maxY=std::max(maxY,p[1]);}
 const std::array<int32_t,4> area{integer(std::round(minX)),integer(std::round(minY)),integer(std::abs(maxX-minX)),integer(std::abs(maxY-minY))}; // Polygon.AABB2c1730.
 polygon=std::move(result);rectangle=area;error.clear();return true;
 }catch(const std::exception&e){error=e.what();return false;}}
}
