#include "lua_runtime.hpp"
#include "recovered_runtime.hpp"
#include "platform_log.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
bool ScriptRuntime::loadActionEffectDefinitions(){try{
 auto file=assets_->open("action-effects.lua");if(!file||file->size()>64*1024*1024)throw std::runtime_error("Original action effect catalog unavailable");std::vector<char>bytes(file->size());size_t at=0;while(at<bytes.size()){auto n=file->read(bytes.data()+at,bytes.size()-at);if(!n)throw std::runtime_error("Original action effect catalog truncated");at+=n;}
 const int top=lua_gettop(state_);int status=luaL_loadbufferx(state_,bytes.data(),bytes.size(),"@action-effects.lua","t");if(status==LUA_OK)status=lua_pcall(state_,0,1,0);if(status!=LUA_OK){error_=lua_tostring(state_,-1);lua_settop(state_,top);return false;}hades::SourceValue catalog;const bool valid=hades::readSourceValue(state_,-1,catalog,error_);lua_settop(state_,top);if(!valid)return false;
 actionEffectBank_=std::make_shared<hades::ActorAnimationBank>();actionEffectBank_->load(*assets_,"action-effect-frames.bin");// AudioManager::StopCue(id, fade) as reconstructed for the screen/vfx
 // consumers: fade>0 ramps volume to 0 then stops; otherwise stop.
 actionEffects_.setSoundStop([this](uint32_t id,float fade,std::string&e){return fade>0?audio_.setVolume(id,0,fade,true,false,e):audio_.stop(id,false,e);});
 return actionEffects_.load(std::move(catalog),actionEffectBank_,effects_.registry(),error_);
 }catch(const std::exception&e){error_=e.what();return false;}}
hades::ActionEffectProviders ScriptRuntime::actionEffectProviders(){hades::ActionEffectProviders p;
 p.owner=[this](int32_t id,hades::EffectOwnerState&o,std::string&e){return effectOwner(id,o,e);};p.marker=[this](int32_t id,const std::string&name,std::array<float,3>&point,std::string&e){return effectMarker(id,name,point,e);};
 p.random=[this](double&v,std::string&e){if(nextRecoveredEngineRandom(state_,v))return true;e="Actual action FX uniform RNG unavailable";return false;};p.bounded=[this](uint32_t bound,uint32_t&v,std::string&e){if(nextRecoveredEngineBoundedRandom(state_,bound,v))return true;e="Actual action FX bounded PCG unavailable";return false;};p.worldFrame=worldFrame_;
 p.sound=[this](const std::string&cue,int32_t owner,const std::array<float,3>&position,uint32_t cap,uint32_t&id,std::string&e){return owner==0?playLocationAudioCue(cue,position,int32_t(cap),id,e):playAudioCue(cue,owner,position,id,e,int32_t(cap));};
 p.elapsed=[this](int32_t owner,float fraction,bool attachedTime,float dt,float&result,std::string&e){
  // InitData2927a1 adds TimeModifierFraction only for authored values>=0;
  // PreferUnmodifiedTime supplies fraction0 for a formerly absent component.
  // TimeModifierSystem188e02..188eba divides by undo+(1-undo)*fraction,
  // then188ff0..189061 multiplies Thing+80 ONLY for AttachedToTimeMultiplier.
  // Attach299fd3 gates that link on source UseAttachedTimeMultiplier(+60).
  // The owner's separate TimeModifierFraction is not the animation's fraction.
  float multiplier=1;
  if(attachedTime){
   if(owner<0){const auto it=projectileFxOwners_.find(owner);const auto* q=it==projectileFxOwners_.end()?nullptr:projectileWorld_.actor(it->second);if(!q||q->disposed){e="Actual action FX elapsed projectile owner unavailable";return false;}multiplier=q->timeMultiplier;}
   else{const auto* a=actorWorld_.actor(owner);if(!a||!a->registered){e="Actual action FX elapsed owner unavailable";return false;}multiplier=a->timeMultiplier;}
  }
  return hades::physicsComponentElapsed(dt,worldSpeedModifier_,fraction>=0?fraction:1,multiplier,result,e);};
 p.locationBlocked=[this](float x,float y,bool&blocked,std::string&e){return gridLocationBlocked(x,y,blocked,e);};return p;
}
// GridManager::IsLocationBlocked 0x180307f70 as called by CreateAndInit
// 0x18029fd6b (stack args units=1, PathfindersPreferAvoid=0, BlockSpawns=1):
// the clamped GridSquare holding the point; loop 1 over mObstacles(+0x18):
// Obstacle::StopsUnits 0x2c55b0 && PointInPolygon 0x2ceaf0(Thing+0x1d8, p);
// loop 2 over mUnits(+0x0): UnitData(pData Unit+0x3c0).mDef(+0x50)
// mBlockSpawns(+0x2cc) && PointInPolygon(Thing+0x1d8, p). A point inside a
// polygon lies inside its registered bounds, so the Android sparse grid
// candidates for the point's bounds are a superset of that GridSquare's.
bool ScriptRuntime::gridLocationBlocked(float x,float y,bool&blocked,std::string&error){
 try{
  blocked=false;
  const auto inside=[x,y](const std::vector<hades::ActorPoint>&v){bool yes=false;for(size_t i=0,j=v.size()-1;i<v.size();j=i++)if((v[i].y>y)!=(v[j].y>y)&&x<(v[j].x-v[i].x)*(y-v[i].y)/(v[j].y-v[i].y)+v[i].x)yes=!yes;return yes;};
  std::vector<int32_t> candidates;if(!actorWorld_.spatialObstacleCandidates(hades::SpatialBounds{x,y,x,y},hades::ObstacleGridCategory::StopsUnits,candidates,error))return false;
  for(auto o:candidates){const auto* obstacle=actorWorld_.actor(o);if(!obstacle||!obstacle->registered||!obstacle->object||!obstacle->object->alive)continue;
   bool stops=false;if(!actorWorld_.obstacleStops(o,false,stops,error))return false;if(!stops)continue;
   if(obstacle->geometry.size()<3)throw std::runtime_error("IsLocationBlocked StopsUnits obstacle polygon unavailable");
   if(inside(obstacle->geometry)){blocked=true;error.clear();return true;}}
  for(auto id:actorWorld_.registrationOrder()){const auto* unit=actorWorld_.actor(id);
   if(!unit||!unit->registered||!unit->object||!unit->object->alive||!unit->definition||unit->definition->kind!="Unit")continue;
   const auto* flag=unit->definition->properties.get("BlockSpawns");if(!flag||!flag->boolean())continue;
   if(unit->geometry.size()<3)throw std::runtime_error("IsLocationBlocked BlockSpawns unit polygon unavailable: "+unit->object->name);
   if(inside(unit->geometry)){blocked=true;break;}}
  error.clear();return true;
 }catch(const std::exception&e){error=e.what();return false;}
}
bool ScriptRuntime::prepareActionEffect(const std::string&name,int32_t owner,std::string&error){return actionEffects_.prepare(name,owner,actionEffectProviders(),error);}
bool ScriptRuntime::createActionEffect(const std::string&name,int32_t owner,std::optional<std::array<float,3>>position,uint64_t&handle,std::string&error){return actionEffects_.create(name,owner,position,actionEffectProviders(),handle,error);}
namespace {
constexpr const char* tag="HadesLua";
// Lua number field or fallback (ScriptArgs::Float semantics for absent keys).
float actionNumber(lua_State*L,const char*key,float fallback){lua_getfield(L,1,key);int ok=0;const double v=lua_tonumberx(L,-1,&ok);lua_pop(L,1);if(!ok)return fallback;if(!std::isfinite(v)||std::abs(v)>1e7)throw std::runtime_error(std::string("Script animation scalar bound: ")+key);return float(v);}
bool actionHas(lua_State*L,const char*key){lua_getfield(L,1,key);const bool v=!lua_isnil(L,-1);lua_pop(L,1);return v;}
// LuaTableExtensions::GetColor 0x1802496f0: array {r,g,b[,a]}; missing r/g/b
// are 0 and a is 1; when r,g,b all lie in [0,1] every channel is clamped to
// [0,1] and scaled by 255 (truncated), otherwise each channel is truncated as
// a byte without rescaling alpha. Non-table gives White.
std::array<uint8_t,4> actionColor(lua_State*L,const char*key){
 std::array<uint8_t,4> out{255,255,255,255};lua_getfield(L,1,key);
 if(lua_istable(L,-1)){double c[4]{0,0,0,1};for(int i=0;i<4;++i){lua_rawgeti(L,-1,i+1);int ok=0;const double v=lua_tonumberx(L,-1,&ok);if(ok){if(!std::isfinite(v))throw std::runtime_error("Script animation Color channel");c[i]=v;}lua_pop(L,1);}
  const bool unit=c[0]>=0&&c[0]<=1&&c[1]>=0&&c[1]<=1&&c[2]>=0&&c[2]<=1;
  for(int i=0;i<4;++i)out[i]=uint8_t(unit?int(std::clamp(c[i],0.,1.)*255.):int(std::clamp(c[i],0.,255.)));}
 lua_pop(L,1);return out;}
}
bool ScriptRuntime::createAnimationImpl(lua_State* L,std::string& error){
    // ScriptAction::CreateAnimation 0x180261ff0 then SetupAnimation 0x180262c50
    // (.local/reports/anim-setup/). Delay keys are consumed by the generic
    // delayed-action wrapper; Id/Ids/DestinationName(s) probes have no effect.
    if(!lua_istable(L,1)){error="CreateAnimation requires table";return false;}
    lua_getfield(L,1,"Name");if(lua_type(L,-1)!=LUA_TSTRING){lua_pop(L,1);return true;}
    const std::string name=lua_tostring(L,-1);lua_pop(L,1);if(name=="null")return true;
    static const std::unordered_set<std::string> keys{"Name","DestinationId","Id","Ids","DestinationName","DestinationNames","Delay","DelaySeconds","DelayMin","DelaySecondsMin","DelayMax","DelaySecondsMax",
        "Scale","ScaleRadius","Duration","MinDuration","MaxDuration","OffsetX","OffsetY","OffsetZ","UseScreenLocation","Angle","Color","Group"};
    lua_pushnil(L);while(lua_next(L,1)){if(lua_type(L,-2)!=LUA_TSTRING||!keys.contains(lua_tostring(L,-2)))throw std::runtime_error("CreateAnimation key outside native ScriptAction::CreateAnimation/SetupAnimation: "+name+"."+(lua_type(L,-2)==LUA_TSTRING?std::string(lua_tostring(L,-2)):std::string("<non-string>")));lua_pop(L,1);}
    lua_getfield(L,1,"DestinationId");int numeric=0;const double n=lua_tonumberx(L,-1,&numeric);lua_pop(L,1);
    if(!numeric||!std::isfinite(n)||n<1||n>INT32_MAX){error="CreateAnimation table/absent DestinationId (CreateCustom) branch not reconstructed";return false;}
    const int32_t id=int32_t(n);const auto* o=findObjectSnapshot(L,id);
    if(!o||!o->active||!o->alive)return true; // World::GetActiveThing null
    hades::ActionAnimationSetup setup;
    setup.scale=actionNumber(L,"Scale",1);setup.scaleRadius=actionNumber(L,"ScaleRadius",0);
    // ScriptArgs::getDuration 0x180281bc0 (SetupAnimation 0x180262ccd): both
    // MinDuration and MaxDuration nonzero -> float(Random::RandomDouble 0x180054f80)
    // x (max-min) + min, the same engine RandomDouble provider Animation
    // GetValue uses; otherwise Duration.
    if(const float lo=actionNumber(L,"MinDuration",0),hi=actionNumber(L,"MaxDuration",0);lo!=0&&hi!=0){double r=0;if(!nextRecoveredEngineRandom(state_,r)||!std::isfinite(r)||r<0||r>=1){error="SetupAnimation Min/MaxDuration engine RandomDouble unavailable";return false;}const volatile float product=float(r)*(hi-lo);setup.duration=product+lo;}
    else setup.duration=actionNumber(L,"Duration",0);setup.offsetX=actionNumber(L,"OffsetX",0);setup.offsetY=actionNumber(L,"OffsetY",0);setup.offsetZ=actionNumber(L,"OffsetZ",0);
    if(const float a=actionNumber(L,"Angle",-999999.f);a!=-999999.f)setup.angleDegrees=a;
    if(actionHas(L,"Color"))setup.color=actionColor(L,"Color");
    if(actionHas(L,"Group")){lua_getfield(L,1,"Group");const char*g=lua_tostring(L,-1);setup.group=g?g:"";lua_pop(L,1);}
    lua_getfield(L,1,"UseScreenLocation");const bool screenLocation=lua_toboolean(L,-1)||o->screenSpace;lua_pop(L,1);
    if(screenLocation){error="CreateAnimation UseScreenLocation action animation not reconstructed: "+name;return false;}
    const auto* actor=actorWorld_.actor(id);
    if(actor)setup.parallax=actor->ownParallax*actor->mapParallax;
    // OffsetX is negated when the destination primary ShouldMirror()
    // (Animation 0x1802977a0: requires definition Mirror).
    if(setup.offsetX!=0&&actor){const hades::SourceValue* primary=nullptr;
        if(actor->animation.active()){auto it=actorAnimationDefinitions_.find(actor->animation.clip()->name);if(it!=actorAnimationDefinitions_.end())primary=it->second.get("definition");}
        else if(const auto* s=actionEffects_.state(actor->primaryAnimationLifetime))primary=&s->definition;
        else if(const auto* s=scenePrimaries_.state(actor->primaryAnimationLifetime))primary=&s->definition;
        if(primary&&primary->get("Mirror")&&primary->get("Mirror")->boolean()){error="CreateAnimation OffsetX on a Mirror primary (ShouldMirror angle test) not reconstructed: "+name;return false;}}
    const auto owners=[this](int32_t selected,hades::EffectOwnerState& state,std::string& detail){return effectOwner(selected,state,detail);};
    const auto random=[&](double& value,std::string& detail){if(nextRecoveredEngineRandom(state_,value))return true;detail="Native animation RNG unavailable";return false;};
    // One AnimationManager::Create path in the engine; this port keeps invisible
    // Constant spawners and exported visible animations in separate worlds over
    // the shared registry, routed by the owning definition.
    uint64_t handle=0;
    if(!effects_.admits(name)&&actionEffects_.admits(name)){
        if(!createActionEffect(name,id,std::nullopt,handle,error))return false;
        if(handle&&!actionEffects_.setup(handle,setup,error))return false;
        if(handle)if(const auto* pending=actionEffects_.pendingFields(name)){std::string list;for(const auto& f:*pending)list+=(list.empty()?"":",")+f;hades::log(hades::Severity::Info,tag,"NATIVE_ACTION_FX_PENDING_PRESENTATION name=%s fields=%s",name.c_str(),list.c_str());}
    }else if(effects_.admits(name)){
        if(setup.scale!=1||setup.scaleRadius>0||setup.duration>0||setup.offsetX!=0||setup.offsetY!=0||setup.offsetZ!=0||setup.angleDegrees||setup.color||setup.group){error="SetupAnimation options on an invisible spawner animation not reconstructed: "+name;return false;}
        if(!effects_.create(name,id,owners,random,handle,error))return false;
    }else{const auto* why=actionEffects_.rejection(name);error="CreateAnimation original animation not exported: "+name+(why?" ("+*why+")":" (outside animation coverage)");return false;}
    hades::log(hades::Severity::Info,tag,"NATIVE_EFFECT_CREATED owner=%d name=%s handle=%llu",id,name.c_str(),static_cast<unsigned long long>(handle));return true;
}
