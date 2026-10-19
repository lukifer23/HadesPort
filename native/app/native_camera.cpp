#include "native_camera.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace hades {
namespace {
bool finite(float v){return std::isfinite(v)&&std::abs(v)<=1e7f;}
bool same(const std::vector<NativeCameraTarget>&a,const std::vector<NativeCameraTarget>&b){if(a.size()!=b.size())return false;for(size_t i=0;i<a.size();++i)if(a[i].id!=b[i].id)return false;return true;}
}
bool NativeCamera::resolveTargets(const std::vector<int32_t>&ids,const NativeCameraResolve&resolve,std::vector<NativeCameraTarget>&out,std::string&error)const{
 if(ids.size()>4096||!resolve){error="Original camera target provider/bound unavailable";return false;}
 auto sorted=ids;std::sort(sorted.begin(),sorted.end());sorted.erase(std::unique(sorted.begin(),sorted.end()),sorted.end());
 for(auto id:sorted){NativeCameraTarget t;if(!resolve(id,t,error))return false;if(!t.lifetime||t.id!=id||!finite(t.x)||!finite(t.y)){error="Invalid actual camera target";return false;}out.push_back(t);}return true;
}
bool NativeCamera::setViewport(uint32_t w,uint32_t h,std::string&error){if(!w||!h||w>16384||h>16384){error="Native camera viewport bounds";return false;}state_.viewportWidth=w;state_.viewportHeight=h;error.clear();return true;}
bool NativeCamera::weightedZoom(const NativeCameraResolve&resolve,float&result,std::string&error)const{if(state_.zoomWeights.empty()){result=1;return true;}if(!resolve){error="Actual weighted camera target provider unavailable";return false;}float sum=0,total=0;for(const auto&[id,binding]:state_.zoomWeights){NativeCameraTarget live;std::string detail;if(!resolve(id,live,detail)||live.lifetime!=binding.first.lifetime)continue;const float distance=std::hypot(live.x-state_.x,live.y-state_.y),weight=distance>1?1000.f/distance:1000.f;total+=weight;sum+=weight*binding.second;}if(total<=0){error="Native weighted camera has no surviving source targets";return false;}result=sum/total;return true;}
bool NativeCamera::setZoomWeight(const std::vector<int32_t>&ids,float weight,float zoomSpeed,const NativeCameraResolve&resolve,std::string&error){if(!finite(weight)||weight<=0||weight>100||!finite(zoomSpeed)){error="Native camera zoom weight bounds";return false;}std::vector<NativeCameraTarget>targets;if(!resolveTargets(ids,resolve,targets,error))return false;for(const auto&t:targets)state_.zoomWeights[t.id]={t,weight};if(zoomSpeed>0)state_.zoomSpeed=zoomSpeed;error.clear();return true;}
bool NativeCamera::applyClamp(const NativeCameraResolve&resolve,std::string&error){
 if(state_.clampTargets.empty())return true;if(state_.clampTargets.size()!=2){error="Original polygon camera clamp requires polygon provider";return false;}
 float minX=1e7f,minY=1e7f,maxX=-1e7f,maxY=-1e7f;size_t n=0;for(const auto&t:state_.clampTargets){NativeCameraTarget live;std::string detail;if(!resolve(t.id,live,detail)||live.lifetime!=t.lifetime)continue;minX=std::min(minX,live.x);minY=std::min(minY,live.y);maxX=std::max(maxX,live.x);maxY=std::max(maxY,live.y);++n;}
 if(n<2)return true;const int left=int(minX),top=int(minY),width=int(maxX-minX),height=int(maxY-minY);if(!width||!height)return true;
 // Source deliberately truncates viewport/zoom and half extent separately,
 // then executes max followed by min even for a view wider than the clamp.
 const int hx=int(float(int(float(state_.viewportWidth)/state_.zoom))*.5f),hy=int(float(int(float(state_.viewportHeight)/state_.zoom))*.5f);
 float x=std::min(float(left+width-hx),std::max(float(left+hx),state_.x)),y=std::min(float(top+height-hy),std::max(float(top+hy),state_.y));
 if(state_.softClamp>0){x=state_.x+(x-state_.x)*state_.softClamp;y=state_.y+(y-state_.y)*state_.softClamp;}if(x!=state_.x||y!=state_.y)speed_=0;state_.x=x;state_.y=y;return true;
}
bool NativeCamera::lock(const std::vector<int32_t>&ids,float duration,float speed,float acceleration,float ox,float oy,bool lazy,bool dynamic,const NativeCameraResolve&resolve,std::string&error){
 if(!finite(duration)||duration<0||!finite(speed)||speed<0||!finite(acceleration)||!finite(ox)||!finite(oy)){error="Original camera move bounds";return false;}std::vector<NativeCameraTarget>targets;if(!resolveTargets(ids,resolve,targets,error))return false;
 if(lazy){error="Native LazyTether requires source cursor/zone provider";return false;}if(dynamic&&targets.size()>1){error="Native multi-target DynamicZoom requires source weighted zoom provider";return false;}
 // The ScriptAction still changes these flags when its selected Thing set is
 // empty. MoveToOverDuration is called only for a nonempty source selection.
 if(!targets.empty()){
  speed_=speed;acceleration_=acceleration;
  if(!(duration_>0&&same(targets,state_.targets))){state_.targets=std::move(targets);state_.offsetX=ox;state_.offsetY=oy;startX_=state_.x;startY_=state_.y;startTime_=time_;duration_=duration;
   if(duration<=0&&speed<=0&&acceleration<=0){float x=0,y=0;for(const auto&t:state_.targets){x+=t.x;y+=t.y;}state_.x=x/state_.targets.size();state_.y=y/state_.targets.size();state_.initialized=true;if(!applyClamp(resolve,error))return false;}
  }
 }
 state_.lazyTether=lazy;state_.dynamicZoom=dynamic;error.clear();return true;
}
bool NativeCamera::pan(const std::vector<int32_t>&ids,bool retarget,const NativeCameraResolve&resolve,std::string&error){std::vector<NativeCameraTarget>targets;if(!resolveTargets(ids,resolve,targets,error))return false;if(!retarget&&same(targets,state_.targets)){error.clear();return true;}if(targets.empty()){error.clear();return true;}error="New-target native Pan requires original randomized CameraSplineHelper paths; existing-target Pan is supported";return false;}
bool NativeCamera::unlock(std::string&error){state_.targets.clear();state_.offsetX=state_.offsetY=0;error.clear();return true;}
bool NativeCamera::adjustZoom(float fraction,float lerpTime,std::string&error,const NativeCameraResolve&resolve){if(!finite(fraction)||fraction<=0||fraction>100||!finite(lerpTime)||lerpTime<0){error="Original camera zoom bounds";return false;}const float distance=std::abs(fraction-state_.zoom);if(distance>0){float weight=1;if(lerpTime<=0&&!weightedZoom(resolve,weight,error))return false;state_.goalZoom=fraction;if(lerpTime<=0)state_.zoom=fraction*weight;else state_.zoomSpeed=distance/lerpTime;}error.clear();return true;}
bool NativeCamera::focus(float fraction,float duration,bool relative,const std::string&,std::string&error,const NativeCameraResolve&resolve){if(duration>0){error="Timed native Focus requires original randomized zoom spline";return false;}return adjustZoom(relative?fraction*state_.zoom:fraction,0,error,resolve);}
bool NativeCamera::setClamps(const std::vector<int32_t>&ids,float soft,const NativeCameraResolve&resolve,std::string&error){if(!finite(soft)||soft<0){error="Original camera soft clamp bounds";return false;}std::vector<NativeCameraTarget>targets;if(!resolveTargets(ids,resolve,targets,error))return false;if(!targets.empty()&&targets.size()!=2){error="Original polygon camera clamp requires polygon provider";return false;}state_.clampTargets=std::move(targets);state_.softClamp=soft;error.clear();return true;}
bool NativeCamera::advance(float elapsed,const NativeCameraResolve&resolve,std::string&error){
 if(!finite(elapsed)||elapsed<0||elapsed>1){error="Original camera elapsed bounds";return false;}time_+=elapsed;
 float x=0,y=0;size_t count=0;for(const auto&t:state_.targets){NativeCameraTarget live;std::string detail;if(!resolve(t.id,live,detail))continue;if(live.lifetime!=t.lifetime)continue;if(!finite(live.x)||!finite(live.y)){error="Nonfinite actual camera target";return false;}x+=live.x;y+=live.y;++count;}
 if(count){x=x/count+state_.offsetX;y=y/count+state_.offsetY;if(x!=0||y!=0){if(speed_>0||acceleration_!=0){const float dx=x-state_.x,dy=y-state_.y,length=std::hypot(dx,dy),step=std::max(0.f,speed_*elapsed);if(length<=step){state_.x=x;state_.y=y;speed_=acceleration_=duration_=0;}else if(length>0){state_.x+=dx/length*step;state_.y+=dy/length*step;speed_+=acceleration_*elapsed;}}
   else if(duration_>0){const float f=(time_-startTime_)/duration_;if(f>1||(state_.x==x&&state_.y==y)){state_.x=x;state_.y=y;duration_=0;}else{const double t=std::clamp(f,0.f,1.f),t2=t*t,t3=t2*t;const auto smooth=[&](float a,float b){return float((2.0*a-2.0*b)*t3+(3.0*b-3.0*a)*t2+a);};state_.x=smooth(startX_,x);state_.y=smooth(startY_,y);}}
   else if(!state_.lazyTether){state_.x=x;state_.y=y;}state_.initialized=true;}}
 if(!applyClamp(resolve,error))return false;float weight=1;if(!weightedZoom(resolve,weight,error))return false;const float goal=state_.goalZoom*weight,difference=goal-state_.zoom,step=elapsed*state_.zoomSpeed;if(std::abs(difference)<=step)state_.zoom=goal;else state_.zoom+=std::copysign(step,difference);if(!applyClamp(resolve,error))return false;error.clear();return true;
}
namespace {
// OffsetHelper::pickAngle 0x1801520c0: reverse direction, +/-45 deg jitter
// unless fixed, wrap to [0,2pi), target = (cos a, -sin a) * distance.
void pickAngle(NativeCameraState::Shake& s,const NativeCameraRandom& random){
    float a=s.angle+3.1415927f;
    if(!s.fixedDirection)a=(float(random())*1.5707964f-0.7853982f)+a;
    a-=std::floor(a/6.2831855f)*6.2831855f;s.angle=a;
    s.targetX=std::cos(a)*s.distance+0.f;s.targetY=-(std::sin(a)*s.distance)+0.f;
}
}
void NativeCamera::shake(float speed,float distance,float duration,float angle,float falloff,bool option,const NativeCameraRandom& random){
    if(!option||!(speed>0))return;
    auto& s=state_.shake;
    // OffsetHelper::init 0x180291300 (distance, speed).
    s=NativeCameraState::Shake{};s.distance=distance;s.speed=speed;s.distanceSq=distance*distance;s.durationRemaining=3.4028235e38f;
    s.active=true;s.angle=float(random())*6.2831855f+0.f;
    if(angle<0)angle=float(random())*6.2831855f+0.f;else s.fixedDirection=true;
    s.angle=angle;state_.shaking=true;
    if(duration>0)s.durationRemaining=duration;
    s.falloffSpeed=falloff;
}
void NativeCamera::advanceShake(float dt,bool option,const NativeCameraRandom& random){
    if(!option){state_.shaking=false;return;}
    if(!state_.shaking)return;
    auto& s=state_.shake;
    // OffsetHelper::Update 0x180151f80.
    s.durationRemaining-=dt;
    if(s.durationRemaining<=0&&s.falloffSpeed>0)s.falling=true;
    if(s.falling){
        float v=s.speed;const float step=dt*s.falloffSpeed;
        if(step<std::abs(10.f-v)){if(v<10.f)v+=step;else if(v>10.f)v-=step;}else v=10.f;
        s.speed=v;if(v==10.f){s.distance=0;s.targetX=s.targetY=0;}
    }
    // Vector MoveTowards (FUN_1801ea990) by dt*speed.
    const float dx=s.targetX-s.currentX,dy=s.targetY-s.currentY,length=std::hypot(dx,dy),step=dt*s.speed;
    if(length<=step||length==0){s.currentX=s.targetX;s.currentY=s.targetY;}else{s.currentX+=dx/length*step;s.currentY+=dy/length*step;}
    if(s.currentX==s.targetX&&s.currentY==s.targetY)pickAngle(s,random);
    if(s.speedRate!=0)s.speed=s.speedRate*dt+s.speed;
    if(s.distanceRate!=0){s.distance=s.distanceRate*dt+s.distance;s.distanceSq=s.distance*s.distance;}
    // OffsetHelper::isDone 0x180151f30.
    const bool done=s.falling?(s.targetX==0&&s.targetY==0&&s.currentX==0&&s.currentY==0):!(s.durationRemaining>0);
    if(done)state_.shaking=false;
}
}
