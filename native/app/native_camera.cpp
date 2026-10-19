#include "native_camera.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
// Source arithmetic is single-precision, unfused (x64 SSE mulss/addss).
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
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
bool NativeCamera::unlock(std::string&error){state_.targets.clear();state_.offsetX=state_.offsetY=0;error.clear();return true;}
bool NativeCamera::adjustZoom(float fraction,float lerpTime,std::string&error,const NativeCameraResolve&resolve){if(!finite(fraction)||fraction<=0||fraction>100||!finite(lerpTime)||lerpTime<0){error="Original camera zoom bounds";return false;}const float distance=std::abs(fraction-state_.zoom);if(distance>0){float weight=1;if(lerpTime<=0&&!weightedZoom(resolve,weight,error))return false;state_.goalZoom=fraction;if(lerpTime<=0)state_.zoom=fraction*weight;else state_.zoomSpeed=distance/lerpTime;}error.clear();return true;}
namespace {
// sgg::CameraSplineHelper::GenerateRandom 0x1801e6a40: lo=c-s, float(Random::RandomDouble)*((c+s)-lo)+lo.
float splineRandom(float center,float spread,const NativeCameraRandom& random){const float lo=center-spread,hi=center+spread;return float(random())*(hi-lo)+lo;}
// Point construction shared by the generators: dir=(cos a,-sin a);
// in=point-dir*inLength, out=point+dir*outLength (single-precision, source order).
NativeSplinePoint splinePoint(float value,float time,float c,float negS,float inLength,float outLength){
 NativeSplinePoint p;p.value=value;p.time=time;p.type=2;p.inValue=value-c*inLength;p.inTime=time-negS*inLength;p.outValue=c*outLength+value;p.outTime=negS*outLength+time;return p;}
// sgg::CameraSplineHelper::GenerateEase 0x1801e6bf0 (Camera::Focus passes 0.12,0.4).
void generateEase(NativeCameraSpline& s,float a,float b,const NativeCameraRandom& random){
 s.points.clear();s.index=0;
 const float v1=splineRandom(a*.1f,a*.04f,random),t1=splineRandom(a,a*.06f,random),v2=splineRandom(1.f-b*.25f,b*.04f,random),t2=splineRandom(1.f-b,b*.06f,random);
 const float c=std::cos(4.712389f),negS=-std::sin(4.712389f);
 s.points.push_back(splinePoint(0,0,c,negS,0,.3f));
 // Angles: float((double)(x*50+95+180)*(pi/180 double)) (mulsd by __real@3f91df46a2529d39).
 const auto angle=[](float x){return float(double(x*50.f+95.f+180.f)*0.017453292519943295);};
 if(t1>0){const float r=angle(a),c1=std::cos(r),n1=-std::sin(r);s.points.push_back(splinePoint(v1,t1,c1,n1,a*.6f,.2f));}
 if(t2<1){const float r=angle(b),c2=std::cos(r),n2=-std::sin(r);s.points.push_back(splinePoint(v2,t2,c2,n2,.2f,b*.5f));}
 s.points.push_back(splinePoint(1,1,c,negS,.1f,0));
}
// sgg::CameraSplineHelper::GenerateOvershootSpline 0x1801e9200 (deterministic).
void generateOvershoot(NativeCameraSpline& s,float duration){
 s.index=0;float p=duration>0?1.f/duration:std::numeric_limits<float>::infinity(); // 1/0 = +inf in source
 if(p<=.1f)p=.1f;if(1.f<=p)p=1.f;const float h=p*.08f;
 const float c=std::cos(4.712389f),negS=-std::sin(4.712389f),c2=std::cos(4.6251225f),n2=-std::sin(4.6251225f);
 s.points.push_back(splinePoint(0,0,c,negS,0,.1f));
 s.points.push_back(splinePoint(1.02f,1.f-p*.4f,c,negS,.3f,h));
 s.points.push_back(splinePoint(1,1.f-p*.18f,c2,n2,h,h+h));
 s.points.push_back(splinePoint(1,1,c,negS,.05f,0));
}
// sgg::CameraSplineHelper::GenerateUndershootSpline 0x1801e8bb0 (deterministic).
void generateUndershoot(NativeCameraSpline& s){
 s.points.clear();s.index=0;
 const float c=std::cos(4.712389f),negS=-std::sin(4.712389f),c1=std::cos(4.886922f),n1=-std::sin(4.886922f);
 s.points.push_back(splinePoint(0,0,c,negS,0,.1f));
 s.points.push_back(splinePoint(.95f,.65f,c1,n1,.3f,.03f));
 s.points.push_back(splinePoint(.99f,.82f,c1,n1,.03f,.03f));
 s.points.push_back(splinePoint(1,1,c,negS,.2f,0));
}
// sgg::CubicBezier 0x1801eaae0: float terms accumulated in double, source order.
float cubicBezier(float p0,float p1,float p2,float p3,float t){
 const float u=1.f-t,t2=t*t,u2=u*u;double sum=double(p1*3.f*t*u2);sum+=double(u2*u*p0);sum+=double(p2*3.f*t2*u);sum+=double(t2*t*p3);return float(sum);}
// sgg::AxisPath::Interpolate 0x1801e6740.
bool interpolate(const NativeCameraSpline& s,size_t i,float t,float& out,std::string& error){
 const auto& a=s.points[i];const auto& b=s.points[i+1];
 if(a.type==2){out=cubicBezier(a.value,a.outValue,b.type==2?b.inValue:b.value,b.value,t);return true;}
 if(a.type==1){error="AxisPath Catmull-Rom segment (Interpolate type 1) not reconstructed";return false;}
 out=(b.value-a.value)*t+a.value;return true;}
// sgg::SplineHelper::GetCurrent 0x1801e6920 (mutates: completion clears the
// point vector, index and duration).
bool splineCurrent(NativeCameraSpline& s,float now,float& value,std::string& error){
 // maxss/minss operand order: a NaN fraction clamps to 0.
 float f=(now-s.startTime)/s.duration;f=f>0.f?f:0.f;f=f<1.f?f:1.f;float x=0;
 if(f>=1.f){if(!s.points.empty())x=s.points.back().value;s.points.clear();s.duration=0;s.index=0;}
 else{if(s.points.size()<2){error="SplineHelper evaluated without segments";return false;}
  size_t i=s.index;while(i+2<s.points.size()&&f>s.points[i+1].time)++i;s.index=i;
  if(!interpolate(s,i,(f-s.points[i].time)/(s.points[i+1].time-s.points[i].time),x,error))return false;}
 value=(s.end-s.start)*x+s.start;return true;}
}
bool NativeCamera::focus(float fraction,float duration,bool relative,const std::string& zoomType,const NativeCameraRandom& random,std::string&error){
 if(!finite(fraction)||!finite(duration)||duration<0||!random){error="Original camera focus bounds";return false;}
 auto& s=state_.zoomSpline;
 if(relative)fraction*=state_.zoom;
 float start=state_.goalZoom;if(s.duration>0&&!splineCurrent(s,time_,start,error))return false;
 s.points.clear();s.duration=0;s.index=0;
 if(zoomType=="Ease")generateEase(s,.12f,.4f,random);
 else if(zoomType=="Undershoot")generateUndershoot(s);
 else generateOvershoot(s,duration);
 s.start=start;s.end=fraction;s.startTime=time_;s.duration=duration;state_.goalZoom=fraction;
 // Camera+0x14 = 2/duration; duration 0 gives +inf (zoom snaps on the next Update).
 state_.zoomSpeed=duration>0?2.f/duration:std::numeric_limits<float>::infinity();
 error.clear();return true;
}
// Camera::Pan 0x1803018b0. Arguments from ScriptAction PanCamera 0x180260bf0:
// getDuration, Float EaseIn/EaseOut (default -1), Bool Retarget (false),
// ResetWeights (true), FromCurrentLocation (false), OffsetX/OffsetY (0); the
// action calls Pan only for a nonempty active-Thing selection.
bool NativeCamera::pan(const std::vector<int32_t>&ids,float duration,float easeIn,float easeOut,float ox,float oy,bool retarget,bool resetWeights,bool fromCurrent,const NativeCameraRandom&random,const NativeCameraResolve&resolve,std::string&error){
 if(!finite(duration)||duration<0||!std::isfinite(easeIn)||!std::isfinite(easeOut)||!finite(ox)||!finite(oy)){error="Original camera pan bounds";return false;}
 std::vector<NativeCameraTarget>targets;if(!resolveTargets(ids,resolve,targets,error))return false;
 if(targets.empty()){error.clear();return true;}
 // 0x1803018d3: !Retarget && HasSameIds 0x1803017d0 -> return before any path work.
 if(!retarget&&same(targets,state_.targets)){error.clear();return true;}
 if(!random){error="Actual camera engine RNG unavailable for CameraSplineHelper::GenerateEase";return false;}
 // GetLocation-with-shake (Camera+0x78 plus OffsetHelper current +0x29c/+0x2a0
 // under the 0x18030193d visibility test, same as GetLocation 0x180304fe0).
 const auto location=[&](float&x,float&y){x=state_.x+state_.shakeOffsetX();y=state_.y+state_.shakeOffsetY();};
 float fromX=0,fromY=0;
 if(fromCurrent)location(fromX,fromY);
 else if(!state_.targets.empty()){
  // GetMoveGoalLocation 0x180301ee0 on the previous ids: rail branch (+0xa0/+0xa8)
  // is never armed natively (SetCameraRail unregistered) -> GetTetherLocation.
  size_t count=0;if(!tetherLocation(resolve,fromX,fromY,count,error))return false;
  if(!count){error="Camera::GetTetherLocation 0x1803021b0 with no live previous tether target not reconstructed";return false;}
 }
 // APPROXIMATION (labelled): with no previous ids GetMoveGoalLocation returns
 // Camera+0x90; the only native writers (Camera::Update 0x180302874/0x180302bf3)
 // store the +0xb8 zero vector and no location-goal MoveToOverDuration exists
 // natively, so it is modelled as (0,0), which then takes the fallback below.
 if(fromX==0&&fromY==0)location(fromX,fromY);
 // Inlined Camera::ClearTether 0x180305ca0: ids cleared, offset +0x50 and
 // look-ahead +0x98 = +0xb8 (zero). ResetWeights clears the tether-weight map
 // +0x2e0 (SetCameraTetherWeight), which is never populated natively.
 (void)resetWeights;
 state_.targets=std::move(targets);state_.offsetX=ox;state_.offsetY=oy; // +0x50 = *OffsetXY
 size_t count=0;float toX=0,toY=0;if(!tetherLocation(resolve,toX,toY,count,error))return false;
 if(!count){error="Camera::GetTetherLocation 0x1803021b0 with no live pan target";return false;}
 // CameraSplineHelper::SetPan 0x1801e6ab0 (Camera+0x1d0, world time World+0x1c).
 const float a=(0.f>easeIn)?.12f:easeIn/duration,b=(0.f>easeOut)?.5f:easeOut/duration; // comiss/jbe: NaN divides
 auto& px=state_.panX;auto& py=state_.panY;
 px.points.clear();px.index=0;px.duration=0;py.points.clear();py.index=0;py.duration=0;
 generateEase(px,a,b,random);generateEase(py,a,b,random);
 px.start=fromX;px.end=toX;px.startTime=time_;px.duration=duration;
 py.start=fromY;py.end=toY;py.startTime=duration/20.f+time_;py.duration=duration*.8f; // __real@41a00000, __real@3f4ccccd
 error.clear();return true;
}
bool NativeCamera::setClamps(const std::vector<int32_t>&ids,float soft,const NativeCameraResolve&resolve,std::string&error){if(!finite(soft)||soft<0){error="Original camera soft clamp bounds";return false;}std::vector<NativeCameraTarget>targets;if(!resolveTargets(ids,resolve,targets,error))return false;if(!targets.empty()&&targets.size()!=2){error="Original polygon camera clamp requires polygon provider";return false;}state_.clampTargets=std::move(targets);state_.softClamp=soft;error.clear();return true;}
bool NativeCamera::tetherLocation(const NativeCameraResolve&resolve,float&x,float&y,size_t&count,std::string&error)const{
 x=y=0;count=0;for(const auto&t:state_.targets){NativeCameraTarget live;std::string detail;if(!resolve(t.id,live,detail))continue;if(live.lifetime!=t.lifetime)continue;if(!finite(live.x)||!finite(live.y)){error="Nonfinite actual camera target";return false;}x+=live.x;y+=live.y;++count;}
 if(count){x=x/count+state_.offsetX;y=y/count+state_.offsetY;}return true;}
bool NativeCamera::advance(float elapsed,const NativeCameraResolve&resolve,std::string&error){
 if(!finite(elapsed)||elapsed<0||elapsed>1){error="Original camera elapsed bounds";return false;}time_+=elapsed;
 float x=0,y=0;size_t count=0;if(!tetherLocation(resolve,x,y,count,error))return false;
 if(count){if(x!=0||y!=0){
   // Camera::Update pan branch 0x180302c4b..d2a: while either pan SplineHelper
   // is active (+0x204>0 || +0x234>0) the goal becomes each helper's end value
   // (moving-hold flag false: +0x1fc/+0x22c = goal), the position is
   // SplineHelper::GetCurrent per axis (an expired axis takes the goal), and
   // the Lock speed/duration/tether branches are skipped for the frame.
   if(state_.panX.duration>0||state_.panY.duration>0){auto&px=state_.panX;auto&py=state_.panY;px.end=x;py.end=y;float cx=x,cy=y;
    if(px.duration>0&&!splineCurrent(px,time_,cx,error))return false;if(py.duration>0&&!splineCurrent(py,time_,cy,error))return false;state_.x=cx;state_.y=cy;}
   else if(speed_>0||acceleration_!=0){const float dx=x-state_.x,dy=y-state_.y,length=std::hypot(dx,dy),step=std::max(0.f,speed_*elapsed);if(length<=step){state_.x=x;state_.y=y;speed_=acceleration_=duration_=0;}else if(length>0){state_.x+=dx/length*step;state_.y+=dy/length*step;speed_+=acceleration_*elapsed;}}
   else if(duration_>0){const float f=(time_-startTime_)/duration_;if(f>1||(state_.x==x&&state_.y==y)){state_.x=x;state_.y=y;duration_=0;}else{const double t=std::clamp(f,0.f,1.f),t2=t*t,t3=t2*t;const auto smooth=[&](float a,float b){return float((2.0*a-2.0*b)*t3+(3.0*b-3.0*a)*t2+a);};state_.x=smooth(startX_,x);state_.y=smooth(startY_,y);}}
   else if(!state_.lazyTether){state_.x=x;state_.y=y;}state_.initialized=true;}}
 if(!applyClamp(resolve,error))return false;// Camera::GetGoalZoom 0x180305070: active zoom spline (Camera+0x264>0) else
 // base goal, times GetWeightedZoom. Camera::Update 0x18030310b: step at
 // Camera+0x14 only while step<|goal-zoom|, else snap (NaN/inf step snaps).
 float base=state_.goalZoom;if(state_.zoomSpline.duration>0&&!splineCurrent(state_.zoomSpline,time_,base,error))return false;
 float weight=1;if(!weightedZoom(resolve,weight,error))return false;const float goal=base*weight;
 if(goal!=state_.zoom){const float step=elapsed*state_.zoomSpeed;if(step<std::abs(goal-state_.zoom))state_.zoom+=goal>state_.zoom?step:-step;else state_.zoom=goal;}if(!applyClamp(resolve,error))return false;error.clear();return true;
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
