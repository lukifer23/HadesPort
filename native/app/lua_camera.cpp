#include "lua_runtime.hpp"
#include "recovered_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace {
float number(const hades::SourceValue&v,const char*key,float fallback=0){const auto*p=v.get(key);if(!p)return fallback;const double n=p->number();if(!std::isfinite(n)||std::abs(n)>1e7)throw std::runtime_error(std::string("Native camera argument bounds: ")+key);return float(n);}
bool boolean(const hades::SourceValue&v,const char*key,bool fallback=false){const auto*p=v.get(key);return p?p->boolean():fallback;}
std::string text(const hades::SourceValue&v,const char*key){const auto*p=v.get(key);return p?p->text():"";}
float sourceRange(lua_State*L,const hades::SourceValue&v,const char*field,const char*minimum,const char*maximum,bool duration){const float lo=number(v,minimum),hi=number(v,maximum);if(duration?(lo!=0&&hi!=0):(lo!=0||hi!=0)){double random=0;if(!nextRecoveredEngineRandom(L,random))throw std::runtime_error("Actual camera engine RNG unavailable");return lo+float(random)*(hi-lo);}return number(v,field);}
}
hades::NativeCameraResolve ScriptRuntime::cameraTargets(){return [this](int32_t id,hades::NativeCameraTarget&out,std::string&error){const auto*a=actorWorld_.actor(id);if(!a||!a->registered||!a->object){error="Actual camera target is not registered";return false;}float x=0,y=0;if(!actorWorld_.location(id,x,y,error))return false;out={id,a->entityHandle,x,y};error.clear();return true;};}
void ScriptRuntime::registerCameraActions(){const char*names[]{"LockCamera","AdjustZoom","PanCamera","FocusCamera","UnlockCamera","SetCameraClamp","SetCameraZoomWeight","ShakeScreen"};for(int mode=0;mode<8;++mode){lua_pushlightuserdata(state_,this);lua_pushinteger(state_,mode);lua_pushcclosure(state_,cameraAction,2);lua_setglobal(state_,names[mode]);}}
int ScriptRuntime::cameraAction(lua_State*L){char message[2048]{};bool ok=false;{auto*self=runtime(L);std::string error;ok=self&&self->cameraActionImpl(L,int(lua_tointeger(L,lua_upvalueindex(2))),error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.empty()?"Native camera runtime unavailable":error.c_str());}if(!ok){lua_pushstring(L,message);return lua_error(L);}return 0;}
bool ScriptRuntime::cameraActionImpl(lua_State*L,int mode,std::string&error){try{
 hades::SourceValue args;if(!hades::readSourceValue(L,1,args,error))return false;auto resolve=cameraTargets();
 if(mode==7){
  // ScriptAction ShakeScreen (Execute VA180253e3d..efa): Float(Angle,-1e6)>=0
  // -> radians (x0.0174532925), else -1 (random); FalloffSpeed/Distance/Speed
  // default 0; ScriptArgs::getDuration; then Camera::Shake. Delay is handled
  // by the shared delayed-action wrapper.
  const auto num=[&](const char*k,double fallback){const auto*p=args.get(k);return p?p->number():fallback;};
  const double angle=num("Angle",-1000000.0);const float radians=angle>=0?float(angle*0.017453292519943295):-1.f;
  const float falloff=float(num("FalloffSpeed",0)),distance=float(num("Distance",0)),speed=float(num("Speed",0)),duration=float(num("Duration",0));
  for(float v:{radians,falloff,distance,speed,duration})if(!std::isfinite(v)){error="ShakeScreen requires finite arguments";return false;}
  bool option=true;if(!cameraShakeOption(option,error))return false;
  camera_.shake(speed,distance,duration,radians,falloff,option,[this]{double v=0;nextRecoveredEngineRandom(state_,v);return v;});error.clear();return true;
 }
 const auto ids=[&]{std::vector<int32_t>out;const auto add=[&](const hades::SourceValue&v){const double n=v.number();if(n<1||n>INT32_MAX||n!=std::trunc(n))throw std::runtime_error("Original camera target ID bounds");const int32_t id=int32_t(n);auto*a=actorWorld_.actor(id);if(a&&a->registered&&a->object)out.push_back(id);};if(const auto*p=args.get("Id"))add(*p);if(const auto*p=args.get("Ids"))for(const auto&v:p->array())add(v);if(args.get("Name")||args.get("Names"))throw std::runtime_error("Camera name selection requires actual native Thing name query");return out;};
 switch(mode){
 case 0:return camera_.lock(ids(),sourceRange(L,args,"Duration","MinDuration","MaxDuration",true),number(args,"Speed"),number(args,"Acceleration"),number(args,"OffsetX"),number(args,"OffsetY"),boolean(args,"LazyTether"),boolean(args,"DynamicZoom"),resolve,error);
 case 1:return camera_.adjustZoom(sourceRange(L,args,"Fraction","MinFraction","MaxFraction",false),number(args,"LerpTime"),error,resolve);
 // ScriptAction PanCamera 0x180260bf0: Float EaseIn/EaseOut (default -1,
 // __real@bf800000), Bool Retarget(false)/ResetWeights(true)/FromCurrentLocation(false),
 // Float OffsetY then OffsetX (default 0), ScriptArgs::getDuration, Camera::Pan.
 case 2:{const auto num=[&](const char*k,double fallback){const auto*p=args.get(k);const double v=p?p->number():fallback;if(!std::isfinite(v)||std::abs(v)>1e7)throw std::runtime_error(std::string("Native camera argument bounds: ")+k);return float(v);};
  const float easeIn=num("EaseIn",-1),easeOut=num("EaseOut",-1);const bool retarget=boolean(args,"Retarget"),resetWeights=boolean(args,"ResetWeights",true),fromCurrent=boolean(args,"FromCurrentLocation");
  const float offsetY=num("OffsetY",0),offsetX=num("OffsetX",0),duration=sourceRange(L,args,"Duration","MinDuration","MaxDuration",true);
  return camera_.pan(ids(),duration,easeIn,easeOut,offsetX,offsetY,retarget,resetWeights,fromCurrent,[this]{double v=0;if(!nextRecoveredEngineRandom(state_,v))throw std::runtime_error("Actual camera engine RNG unavailable");return v;},resolve,error);}
 // ScriptAction FocusCamera (Execute VA180252d66..e1c): String ZoomType
 // (default ""), getFraction, Bool Relative, getDuration, then Camera::Focus
 // unconditionally (also for Duration 0). The Ease spline draws the engine RNG.
 case 3:{const float fraction=sourceRange(L,args,"Fraction","MinFraction","MaxFraction",false),duration=sourceRange(L,args,"Duration","MinDuration","MaxDuration",true);
  return camera_.focus(fraction,duration,boolean(args,"Relative"),text(args,"ZoomType"),[this]{double v=0;if(!nextRecoveredEngineRandom(state_,v))throw std::runtime_error("Actual camera engine RNG unavailable");return v;},error);}
 case 4:return camera_.unlock(error);
 case 5:return camera_.setClamps(ids(),number(args,"SoftClamp"),resolve,error);
 case 6:return camera_.setZoomWeight(ids(),number(args,"Weight",1),number(args,"ZoomSpeed"),resolve,error);
 default:throw std::runtime_error("Unknown original camera action");}
 }catch(const std::exception&e){error=e.what();return false;}}
bool ScriptRuntime::cameraShakeOption(bool& option,std::string& error){
 const int top=lua_gettop(state_);lua_getglobal(state_,"GetConfigOptionValue");lua_newtable(state_);lua_pushliteral(state_,"CameraShake");lua_setfield(state_,-2,"Name");
 if(lua_pcall(state_,1,1,0)!=LUA_OK){error=lua_tostring(state_,-1);lua_settop(state_,top);return false;}option=lua_toboolean(state_,-1);lua_settop(state_,top);return true;}
bool ScriptRuntime::advanceCamera(float elapsed,std::string&error){
 if(!camera_.advance(elapsed,cameraTargets(),error))return false;
 bool option=true;if(!cameraShakeOption(option,error))return false;
 camera_.advanceShake(elapsed,option,[this]{double v=0;nextRecoveredEngineRandom(state_,v);return v;});return true;}
