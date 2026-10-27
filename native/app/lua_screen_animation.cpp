#include "lua_runtime.hpp"
#include "scene_runtime.hpp"
#include "text_runtime.hpp"
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <cstdio>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
bool ScriptRuntime::loadScreenAnimationDefinitions(){try{
 auto file=assets_->open("screen-animations.lua");if(!file||file->size()>1024*1024)throw std::runtime_error("Original screen animation catalog unavailable");std::vector<char>bytes(file->size());size_t at=0;while(at<bytes.size()){auto n=file->read(bytes.data()+at,bytes.size()-at);if(!n)throw std::runtime_error("Original screen catalog truncated");at+=n;}
 const int top=lua_gettop(state_);int status=luaL_loadbufferx(state_,bytes.data(),bytes.size(),"@screen-animations.lua","t");if(status==LUA_OK)status=lua_pcall(state_,0,1,0);if(status!=LUA_OK){error_=lua_tostring(state_,-1);lua_settop(state_,top);return false;}hades::SourceValue data;const bool ok=hades::readSourceValue(state_,-1,data,error_);lua_settop(state_,top);if(!ok)return false;
 screenAnimationBank_=std::make_shared<hades::ActorAnimationBank>();screenAnimationBank_->load(*assets_,"screen-animation-frames.bin");if(!screenAnimations_.load(std::move(data),screenAnimationBank_,effects_.registry(),error_))return false;
 // Animation.Attach CancelAllAttached needs actual attachment owners across
 // managers. Screen layers are handled locally; foreign domains must be
 // recognized before cancellation rather than silently dropping their work.
 screenAnimations_.setCancelAllAttachedProvider([this](int32_t id,uint64_t lifetime,uint64_t primary,std::string& error){
  const auto* screen=screenAnimations_.thing(id);if(!screen||!screen->body||screen->body->entityHandle!=lifetime){error="CancelAllAttached screen owner lifetime unavailable";return false;}
  const auto registry=effects_.registry();
  for(const auto handle:registry->ownerAllocations(id,lifetime)){
   const auto* life=registry->find(handle);if(!life||!life->initialized||handle==primary)continue;
   if(std::any_of(screen->layers.begin(),screen->layers.end(),[&](const auto&layer){return layer.handle==handle;}))continue;
   if(const auto* action=actionEffects_.state(handle)){
    if(action->detached||action->guiLine)continue; // Native attachment pointer is null.
    if(life->ownership!=hades::AnimationOwnership::LightController)continue;
   }
   error="CancelAllAttached foreign animation attachment consumer not reconstructed: "+life->name;return false;
  }
  return actionEffects_.cancelAllAttachedForAttach(id,lifetime,primary,error);
 });
 screenAnimations_.setTextDimensionsProvider([this](int32_t id,std::array<float,2>&size,std::string&e){return attachedTextDimensions(state_,id,size,e);});
 ObjectScreenHooks hooks;hooks.animation=[this](int32_t id,const std::string&n,std::string&e){return setScreenAnimation(id,n,e);};hooks.fraction=[this](int32_t id,bool scale,float target,const hades::BodyTransitionOptions&o,std::string&e){return screenFraction(id,scale,target,o,e);};hooks.location=[this](int32_t id,float&x,float&y,std::string&e){const auto*s=screenAnimations_.thing(id);if(!s||s->dead){e="Actual screen location unavailable";return false;}x=s->body->x;y=s->body->y;return true;};hooks.destroy=[this](int32_t id,bool,std::string&e){return screenDestroy(id,e);};hooks.animationArgs=[this](int32_t id,const std::string&n,lua_State*L,std::string&e){return screenAnimationArgs(id,n,L,e);};setObjectScreenHooks(state_,std::move(hooks));
 // Registered before installDelayedActions so source Delay wrapping applies.
 if(mapRevision_==0){const char*names[]{"SetAnimationFrameTarget","SetScaleX","SetScaleY","FadeObstacleIn","HideObstacle","GetWeaponMaxAmmo","TeleportCursor"};for(int mode=0;mode<7;++mode){lua_pushlightuserdata(state_,this);lua_pushinteger(state_,mode);lua_pushcclosure(state_,screenAction,2);lua_setglobal(state_,names[mode]);}}
 return true;
 }catch(const std::exception&e){error_=e.what();return false;}}
hades::ScreenAnimationSound ScriptRuntime::screenAnimationSound(){hades::ScreenAnimationSound p;p.worldFrame=worldFrame_;
 // UI audio is adapted to non-spatial Android output; the original event graph
 // and its actual playback/finish fade remain owned by AudioWorld.
 p.play=[this](const std::string&cue,uint32_t&id,std::string&e){if(!audio_.createCue(cue,{},id,e)||!audio_.play(id,e))return false;++audioFramePlays_;return true;};p.stop=[this](uint32_t id,float fade,std::string&e){return audio_.setVolume(id,0,fade,true,false,e);};return p;}
bool ScriptRuntime::spawnScreenObstacleImpl(lua_State*L,int32_t&created,std::string&error){try{
 auto text=[&](const char*k,const char*fallback=""){lua_getfield(L,1,k);if(lua_isnil(L,-1)){lua_pop(L,1);return std::string(fallback);}if(lua_type(L,-1)!=LUA_TSTRING)throw std::runtime_error(std::string("Screen ")+k+" requires string");std::string v=lua_tostring(L,-1);lua_pop(L,1);return v;};auto number=[&](const char*k){lua_getfield(L,1,k);if(lua_isnil(L,-1)){lua_pop(L,1);return 0.f;}if(lua_type(L,-1)!=LUA_TNUMBER)throw std::runtime_error(std::string("Screen ")+k+" requires scalar");float v=float(lua_tonumber(L,-1));lua_pop(L,1);if(!std::isfinite(v))throw std::runtime_error("Nonfinite screen placement");return v;};
 for(const auto*k:{"DestinationId","DestinationIds","Names","Attach","ForceToValidLocation","SkipIfBlocked","DestroyIfBlocked","AttachedTable"}){lua_getfield(L,1,k);const bool unsupported=!lua_isnil(L,-1)&&!(lua_isboolean(L,-1)&&!lua_toboolean(L,-1));lua_pop(L,1);if(unsupported)throw std::runtime_error(std::string("Source screen spawn branch requires typed adapter: ")+k);}
 const auto name=text("Name");if(name.empty()){error="Actual screen Thing name required";return false;}
 const auto*def=actorWorld_.definition("Obstacles/"+name);if(!def){error="Original screen Thing source definition missing";return false;}const auto* needsLife=def->properties.path("NeedsLife");if(!needsLife||needsLife->boolean()){error="Screen Thing requiring actualLife unsupported: "+name;return false;}const auto scalar=[&](const char*key){auto*p=def->properties.path(key);if(!p)throw std::runtime_error(std::string("Screen source property unavailable: ")+key);return float(p->number());};auto*graphic=def->properties.path("Thing.Graphic");if(!graphic)throw std::runtime_error("Original screen graphic missing");
 if(!allocateObstacleObjectId(L,created,error))return false;ObjectSnapshot b;b.id=created;b.name=name;b.screenSpace=true;b.entityHandle=actorWorld_.allocateEntityLifetime(error);if(!b.entityHandle)return false;b.group=text("Group","Default");b.groups={b.group};b.x=number("LocationX")+number("OffsetX");b.y=number("LocationY")+number("OffsetY");b.z=number("OffsetZ");b.scale=scalar("Thing.Scale");b.animation=graphic->text();lua_getfield(L,1,"SortById");b.sortById=lua_toboolean(L,-1);lua_pop(L,1);if(const auto*mode=def->properties.path("Thing.SortMode");mode&&std::holds_alternative<std::string>(mode->value)&&mode->text()=="Id")b.sortById=true; /* ThingData SortMode Id (GUI.sjson TraitTray_*) */b.visible=!def->properties.path("Thing.Invisible")->boolean();b.active=true;b.alive=true;
 if(scalar("Thing.MinScale")!=0||scalar("Thing.MaxScale")!=0){error="Screen random body scale requires exact RNG adapter";return false;}
 std::array<float,4>color{};size_t channel=0;for(const auto*k:{"Thing.Color.Red","Thing.Color.Green","Thing.Color.Blue","Thing.Color.Alpha"})color[channel++]=float(uint8_t(std::clamp(int(scalar(k)*255),0,255)))/255;
 bool groupFound=false;for(const auto&g:snapshotSceneGroups(L))if(g.name==b.group)groupFound=true;if(!groupFound){lua_getglobal(L,"CreateGroup");lua_newtable(L);lua_pushlstring(L,b.group.data(),b.group.size());lua_setfield(L,-2,"Name");if(lua_pcall(L,1,0,0)!=LUA_OK)throw std::runtime_error(lua_tostring(L,-1));}
 if(!insertActorSnapshot(L,std::move(b),error))return false;auto*body=findObjectSnapshot(L,created);if(!body||!screenAnimations_.create(*body,color,screenAnimationSound(),error))return false;
 lua_getfield(L,1,"TriggerOnSpawn");const bool trigger=lua_isnil(L,-1)||lua_toboolean(L,-1);lua_pop(L,1);const auto* sourceTrigger=def->properties.path("Thing.TriggerOnSpawn");if(trigger&&sourceTrigger&&sourceTrigger->boolean()&&!dispatchCallbacks("OnSpawn",name.c_str(),created))return false;return true;
 }catch(const std::exception&e){error=e.what();return false;}}
bool ScriptRuntime::setScreenAnimation(int32_t id,const std::string&name,std::string&e){return screenAnimations_.setAnimation(id,name,screenAnimationSound(),e);}
bool ScriptRuntime::advanceScreenAnimations(float dt,std::string&e){return screenAnimations_.advance(dt,screenAnimationSound(),e);}
bool ScriptRuntime::screenFraction(int32_t id,bool scale,float target,const hades::BodyTransitionOptions&o,std::string&e){return screenAnimations_.adjustFraction(id,scale,target,o,e);}
bool ScriptRuntime::screenColor(int32_t id,const std::array<float,4>&color,bool base,std::string&e){return screenAnimations_.setColor(id,color,base,e);}
bool ScriptRuntime::screenDestroy(int32_t id,std::string&e){return screenAnimations_.markDead(id,e);}

std::vector<int32_t> ScriptRuntime::screenPresentationIds()const{std::vector<int32_t>ids;for(const auto&[id,s]:screenAnimations_.things())if(!s.dead&&s.body&&s.body->alive&&s.body->visible&&s.primary)ids.push_back(id);return ids;}
bool ScriptRuntime::sampleScreenPresentation(int32_t id,hades::ActorVisual&out,std::string&e){if(!screenAnimations_.visual(id,out,e))return false;const auto*s=screenAnimations_.thing(id);const auto groups=snapshotSceneGroups(state_);auto g=std::find_if(groups.begin(),groups.end(),[&](const auto&v){return v.name==s->body->group;});if(g==groups.end()){e="Actual screen draw group unavailable: "+s->body->group;return false;}out.drawGroup=uint32_t(g-groups.begin());out.sourceSortY=float(id);return true;}

namespace {
float screenArg(const hades::SourceValue&v,const char*k,float fallback=0){const auto*p=v.get(k);if(!p)return fallback;const double n=p->number();if(!std::isfinite(n)||std::abs(n)>1e7)throw std::runtime_error(std::string("Screen action numeric bound: ")+k);return float(n);}
bool screenFlag(const hades::SourceValue&v,const char*k){const auto*p=v.get(k);return p&&p->boolean();}
std::vector<int32_t> screenIds(const hades::SourceValue&v,const char*one,const char*many){std::vector<int32_t>out;const auto add=[&](const hades::SourceValue&x){const double n=x.number();if(n<1||n>INT32_MAX||n!=std::trunc(n))throw std::runtime_error("Screen action Thing ID bound");out.push_back(int32_t(n));};if(const auto*p=v.get(one))add(*p);if(const auto*p=v.get(many))for(const auto&x:p->array())add(x);return out;}
hades::BodyTransitionOptions screenOptions(const hades::SourceValue&v){hades::BodyTransitionOptions o;o.duration=screenArg(v,"Duration");o.smoothStep=screenFlag(v,"SmoothStep");o.easeIn=screenArg(v,"EaseIn");o.easeOut=screenArg(v,"EaseOut");if(o.duration<0)throw std::runtime_error("Negative native screen transition duration");return o;}
}
bool ScriptRuntime::screenAnimationArgs(int32_t id,const std::string&name,lua_State*L,std::string&e){try{
 hades::SourceValue v;if(!hades::readSourceValue(L,1,v,e))return false;hades::ScreenAnimationRequest r;
 // SetAnimation 0x180261a10 keys without a screen consumer yet fail by name.
 for(const auto&[key,fallback]:std::initializer_list<std::pair<const char*,double>>{{"Scale",1},{"Duration",0},{"MinDuration",0},{"MaxDuration",0},{"OffsetX",0},{"OffsetY",0},{"SoundId",0}})if(const auto*x=v.get(key);x&&x->number()!=fallback)throw std::runtime_error(std::string("Screen SetAnimation option not reconstructed: ")+name+"."+key);
 if((v.get("SuppressSounds")&&v.get("SuppressSounds")->boolean())||(v.get("SuppressSoundsIfInvisible")&&!v.get("SuppressSoundsIfInvisible")->boolean()))throw std::runtime_error("Screen SetAnimation SuppressSounds options not reconstructed: "+name);
 if(v.get("FrameTarget"))r.frameTarget=screenArg(v,"FrameTarget",-1);if(v.get("StartFrameFraction"))r.startFrameFraction=screenArg(v,"StartFrameFraction");r.instant=screenFlag(v,"Instant");r.playSpeed=screenArg(v,"PlaySpeed",1);
 if(const auto*c=v.get("Color")){
  // Native SetAnimation forwards Color to the Thing SetColor action (0x6c):
  // source byte RGBA, SetBase/Duration absent => immediate body colour.
  const auto&bytes=c->array();if(bytes.size()!=4)throw std::runtime_error("Native SetAnimation Color requires source RGBA byte array");std::array<float,4>rgba{};for(size_t i=0;i<4;++i){const double n=bytes[i].number();if(n<0||n>255||n!=std::trunc(n))throw std::runtime_error("Native SetAnimation Color outside byte subset");rgba[i]=float(n)/255.f;}r.color=rgba;}
 return screenAnimations_.setAnimation(id,name,r,screenAnimationSound(),e);
 }catch(const std::exception&x){e=x.what();return false;}}
// Screen branch of ScriptAction::Move 0x274230 -> Obstacle::MoveTo 0x2c5bf0.
// Screen locations are raw mLocation: no isometric/camera transform.
bool ScriptRuntime::screenMoveImpl(const hades::SourceValue&v,bool&handled,std::string&e){try{
 handled=false;std::vector<int32_t> sources;for(auto id:screenIds(v,"Id","Ids")){const auto*s=screenAnimations_.thing(id);if(s&&!s->dead&&s->body&&s->body->alive)sources.push_back(id);else if(s&&s->dead){handled=true;}}
 if(sources.empty())return true;handled=true;
 for(const auto*k:{"Name","Names","DestinationName","DestinationNames","LiveOffsetFromId","Speed","MinDuration","MaxDuration"})if(v.get(k)&&!(std::string(k)=="Speed"&&screenArg(v,"Speed")==0))throw std::runtime_error(std::string("Uncovered native screen Move argument: ")+k);
 if(screenFlag(v,"DestinationIsScreenRelative"))throw std::runtime_error("Native screen Move DestinationIsScreenRelative camera branch not reconstructed");
 if(const auto*t=v.get("TimeModifierFraction");t&&t->number()!=1)throw std::runtime_error("Native screen Move TimeModifierFraction requires world slow-motion clock");
 const auto dests=screenIds(v,"DestinationId","DestinationIds");const bool hasAngle=v.get("Angle")!=nullptr;const float distance=screenArg(v,"Distance");
 float offX=0,offY=0,destX=screenArg(v,"OffsetX"),destY=screenArg(v,"OffsetY");bool offsetMode=false;
 if(screenFlag(v,"ShiftThingsByOffset")){offX=destX;offY=destY;offsetMode=true;}
 else if(!dests.empty()){float sx=0,sy=0;size_t n=0;for(auto id:dests){const auto*d=screenAnimations_.thing(id);if(!d||d->dead||!d->body){if(findObjectSnapshot(state_,id))throw std::runtime_error("Native screen Move toward a world Thing not reconstructed");continue;}sx+=d->body->x;sy+=d->body->y;++n;}if(!n){e.clear();return true;}destX+=sx/n;destY+=sy/n;}
 else if(distance!=0&&hasAngle){const float r=float(double(screenArg(v,"Angle"))*3.141592653589793/180.0);offX=distance*std::cos(r);offY=-(distance*std::sin(r));offsetMode=true;}
 float cx=0,cy=0;for(auto id:sources){const auto*s=screenAnimations_.thing(id);cx+=s->body->x;cy+=s->body->y;}cx/=sources.size();cy/=sources.size();
 const auto o=screenOptions(v);
 for(auto id:sources){const auto*s=screenAnimations_.thing(id);float tx,ty;if(offsetMode&&(offX!=0||offY!=0)){tx=s->body->x+offX;ty=s->body->y+offY;}else if(offsetMode){tx=s->body->x;ty=s->body->y;}else{tx=s->body->x-cx+destX;ty=s->body->y-cy+destY;}
  if(!screenAnimations_.moveTo(id,tx,ty,o.duration,o,e))return false;}
 e.clear();return true;
 }catch(const std::exception&x){e=x.what();return false;}}
int ScriptRuntime::screenAction(lua_State*L){char message[2048]{};bool ok=false;{auto*self=runtime(L);std::string error;try{ok=self&&self->screenActionImpl(L,int(lua_tointeger(L,lua_upvalueindex(2))),error);}catch(const std::exception&x){error=x.what();ok=false;}if(!ok)std::snprintf(message,sizeof(message),"%s",error.empty()?"Native screen action runtime unavailable":error.c_str());}if(!ok){lua_pushstring(L,message);return lua_error(L);}return int(lua_tointeger(L,lua_upvalueindex(2)))==5?1:0;}
bool ScriptRuntime::screenActionImpl(lua_State*L,int mode,std::string&e){
 if(mode==6){
  // ScriptAction::TeleportCursor 0x25e3d0 (.local/reports/hud-teleport-cursor.json).
  hades::SourceValue v;if(!hades::readSourceValue(L,1,v,e))return false;
  if(int(screenArg(v,"PlayerIndex",1))!=1){e.clear();return true;} // Only the recovered local player exists.
  const auto dests=screenIds(v,"DestinationId","DestinationIds");if(v.get("DestinationName")||v.get("DestinationNames")){e="TeleportCursor destination-name selection not reconstructed";return false;}
  if(dests.empty()){screenCursor_.previousX=screenCursor_.x;screenCursor_.x=screenArg(v,"OffsetX");screenCursor_.y=screenArg(v,"OffsetY");}
  else{float best=3.4028234663852886e38f;const hades::ScreenThing*chosen=nullptr;for(auto id:dests){const auto*t=screenAnimations_.thing(id);if(!t||t->dead||!t->body){if(findObjectSnapshot(L,id))throw std::runtime_error("TeleportCursor to world Thing location not reconstructed");continue;}const float dx=t->body->x-screenCursor_.x,dy=t->body->y-screenCursor_.y,dist=dx*dx+dy*dy;if(!chosen||dist<best){best=dist;chosen=t;}}
   if(chosen){screenCursor_.previousX=screenCursor_.x;screenCursor_.x=chosen->body->x;screenCursor_.y=chosen->body->y;}}
  screenCursor_.forceUseCheck=screenFlag(v,"ForceUseCheck");e.clear();return true;
 }
 if(mode==5){
  // ScriptAction::GetWeaponMaxAmmo 0x267760 (.local/reports/hud-get-weapon-max-ammo.json):
  // GetActiveMapThings(Ids,Names); first Unit whose WeaponArsenal::GetWeapon(WeaponName)
  // exists returns its live WeaponData mMaxAmmo (data+0x6e8 = def+0x698); else -1.
  std::vector<int32_t> selected;if(!resolveObjectTargets(L,false,selected,e))return false;
  lua_getfield(L,1,"WeaponName");const std::string name=lua_type(L,-1)==LUA_TSTRING?lua_tostring(L,-1):"";lua_pop(L,1);
  double result=-1;
  for(auto id:selected){const auto*o=findObjectSnapshot(L,id);auto*a=actorWorld_.actor(id);if(!o||!o->active||!o->alive||o->queryProxy||!a||!a->registered||!a->arsenal||!a->arsenal->hasWeapon(name))continue;
   hades::WeaponScalar value;if(!a->arsenal->dataProperty(name,"MaxAmmo",value,e))return false;const auto*n=std::get_if<double>(&value);if(!n){e="Native WeaponData MaxAmmo is not numeric";return false;}result=std::trunc(*n);break;}
  lua_pushnumber(L,result);e.clear();return true;
 }
 hades::SourceValue v;if(!lua_istable(L,1)){e="Screen action requires argument table";return false;}if(!hades::readSourceValue(L,1,v,e))return false;
 const auto live=[&](int32_t id){const auto*s=screenAnimations_.thing(id);return s&&!s->dead&&s->body&&s->body->alive;};
 const auto worldThing=[&](int32_t id){const auto*o=findObjectSnapshot(L,id);return o&&o->active&&o->alive&&o->backend!=ObjectBackend::Screen;};
 if(mode==0){
  // SetAnimationFrameTarget 0x263770: single integer DestinationId, Name, Fraction, Instant.
  const auto*d=v.get("DestinationId");if(!d)return true;const double n=d->number();if(n<1||n>INT32_MAX||n!=std::trunc(n))return true;const int32_t id=int32_t(n);
  const auto*name=v.get("Name");if(!name)return true;
  if(worldThing(id)){
   const auto* actor=actorWorld_.actor(id);if(!actor||!actor->registered)return true;
   if(actor->animation.active()&&actor->object->animation==name->text()){e="SetAnimationFrameTarget on HAA primary not reconstructed";return false;}
   const auto fraction=screenArg(v,"Fraction");const auto instant=screenFlag(v,"Instant");
   return actionEffects_.setFrameTarget(id,name->text(),fraction,instant,e)&&scenePrimaries_.setFrameTarget(id,name->text(),fraction,instant,e);
  }
  if(!live(id))return true;return screenAnimations_.setFrameTarget(id,name->text(),screenArg(v,"Fraction"),screenFlag(v,"Instant"),e);
 }
 if(mode==1||mode==2){
  // Execute case 111/112 -> Thing::RunScriptAction -> BodyComponent::Adjust(scaleX/Y).
  if(screenArg(v,"MinFraction")!=0||screenArg(v,"MaxFraction")!=0||(screenArg(v,"MinDuration")!=0&&screenArg(v,"MaxDuration")!=0)){e="Native random SetScaleX/Y fraction/duration requires engine RNG adapter";return false;}
  std::vector<int32_t> ids;if(const auto*p=v.get("Id")){ids=screenIds(v,"Id","");}else ids=screenIds(v,"","Ids");
  const auto o=screenOptions(v);const float f=screenArg(v,"Fraction");
  for(auto id:ids){if(worldThing(id)){if(!actorWorld_.adjustScaleAxis(id,mode==1?0:1,f,o,e))return false;continue;}if(!live(id))continue;if(!screenAnimations_.adjustChannel(id,mode==1?2:3,f,o,e))return false;}
  return true;
 }
 // FadeObstacleIn 0x258600 / HideObstacle 0x258990.
 std::vector<int32_t> ids=screenIds(v,"Id","Ids");if(v.get("Names")||v.get("Name")){e="Native Fade/HideObstacle group-name selection not reconstructed";return false;}
 const float duration=screenArg(v,"Duration");if(duration<0){e="Negative native fade duration";return false;}const bool includeText=screenFlag(v,"IncludeText");
 for(auto id:ids)if(worldThing(id)){e="FadeObstacleIn/HideObstacle on world Thing not reconstructed";return false;}
 if(mode==3){
  const float direction=screenArg(v,"Direction",-1e6f),distance=screenArg(v,"Distance");
  if(direction!=-1e6f&&distance!=0){hades::SourceValue m=v;auto&obj=std::get<hades::SourceValue::Object>(m.value);obj["SmoothStep"].value=true;obj["Angle"].value=double(direction);obj.erase("Direction");obj.erase("IncludeText");bool handled=false;if(!screenMoveImpl(m,handled,e))return false;}
 }else{
  const float angle=screenArg(v,"Angle",-1e6f),distance=screenArg(v,"Distance");
  if(angle!=-1e6f&&distance!=0){hades::SourceValue m=v;auto&obj=std::get<hades::SourceValue::Object>(m.value);obj.erase("IncludeText");obj.erase("FadeTarget");bool handled=false;if(!screenMoveImpl(m,handled,e))return false;}
 }
 for(auto id:ids){if(!live(id))continue;hades::BodyTransitionOptions o;
  if(mode==3){if(!screenAnimations_.adjustChannel(id,1,0,o,e))return false;o.duration=duration;if(!screenAnimations_.adjustChannel(id,1,1,o,e))return false;}
  else{o.duration=duration;if(!screenAnimations_.adjustChannel(id,1,0,o,e))return false;}
  if(includeText){
   // Native writes the owned GUI text fade target (tb+0x60); HideObstacle also
   // sets speed 1/Duration. Routed through the original ModifyTextBox action.
   lua_getglobal(L,"ModifyTextBox");lua_newtable(L);lua_pushinteger(L,id);lua_setfield(L,-2,"Id");lua_pushnumber(L,mode==3?1:0);lua_setfield(L,-2,"FadeTarget");lua_pushnumber(L,mode==3?0:duration);lua_setfield(L,-2,"FadeDuration");
   if(lua_pcall(L,1,0,0)!=LUA_OK){e=lua_tostring(L,-1);lua_pop(L,1);return false;}
  }
 }
 return true;
}
