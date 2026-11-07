#include "lua_runtime.hpp"
#include "cursor_interaction.hpp"
#include "object_runtime.hpp"
#include "input_runtime.hpp"
#include "scene_runtime.hpp"
#include "platform_log.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
extern "C" {
#include <lua.h>
}

namespace {
bool flag(const hades::SourceValue& d,const char* key){const auto* p=d.get(key);return p&&p->boolean();}
std::string name(const hades::SourceValue& d,const char* key){const auto* p=d.get(key);return p?p->text():std::string{};}
}

bool ScriptRuntime::initializeScreenInteraction(int32_t id,const hades::SourceValue& properties,std::string& error){
 const auto* object=findObjectSnapshot(state_,id);if(!object||!object->entityHandle||object->backend!=ObjectBackend::Screen){error="Actual source screen Interact owner unavailable";return false;}
 const auto* source=properties.path("Thing.Interact");if(!source||std::holds_alternative<std::monostate>(source->value)){error.clear();return true;}
 if(!interactionGridReady_){interactions_.configureSparseGrid();interactionGridReady_=true;}
 // Thing construction copies original InteractData before later script mutations.
 // Runtime counters/gates must not be replaced by default SetInteractProperty data.
 return interactions_.add({id,object->entityHandle},*source,{worldTime_,int32_t(worldFrame_)},error);
}
bool ScriptRuntime::setLocalCursor(float x,float y,bool down,std::string& error){
 if(!std::isfinite(x)||!std::isfinite(y)||std::abs(x)>100000||std::abs(y)>100000){error="Actual logical pointer outside supported bounds";return false;}
 screenCursor_.previousX=screenCursor_.x;screenCursor_.x=x;screenCursor_.y=y;
 if(down&&!screenCursor_.pointerDown)screenCursor_.pointerPressed=true;
 screenCursor_.pointerDown=down;screenCursor_.pointerInput=true;error.clear();return true;
}
bool ScriptRuntime::screenInputRequested()const{
 // Observe real original notification owners; gameplay input blocks alone do
 // not identify a menu. No callback name or game-specific button is consulted.
 for(const auto& pending:worldNotifies_)if(pending.interact)for(const auto id:pending.sources){
  const auto* screen=screenAnimations_.thing(id);if(!screen||screen->dead||!screen->body||!screen->body->entityHandle)continue;
  hades::InteractionSnapshot state;std::string error;if(interactions_.snapshot({id,screen->body->entityHandle},state,error)&&state.enabled&&flag(state.definition,"CursorOnly"))return true;
 }
 return false;
}
bool ScriptRuntime::cursorHighlight(hades::InteractionKey key,bool on,std::string& error){
 hades::InteractionSnapshot interaction;if(!interactions_.snapshot(key,interaction,error))return false;
 auto* object=findObjectSnapshot(state_,key.id);if(!object||object->entityHandle!=key.lifetime){error="Cursor highlight owner lifetime expired";return false;}
 const auto& d=interaction.definition;const auto animation=name(d,on?"HighlightOnAnimation":"HighlightOffAnimation");
 if(!flag(d,"FloatHighlightAnimation")){
  if(animation.empty()||animation=="null"){error.clear();return true;}
  return object->backend==ObjectBackend::Screen?setScreenAnimation(key.id,animation,error):setActorAnimation(key.id,animation,error);
 }
 if(on){
  if(cursorHighlights_.contains(key)){error.clear();return true;}
  if(animation.empty()||animation=="null"){error.clear();return true;}
  // Interact.HighlightOn2bfb80 retains an actual World manager0 Create29e350
  // allocation, independent of the owner's primary. No primary replacement.
  uint64_t handle=0;if(!actionEffects_.create(animation,key.id,std::nullopt,actionEffectProviders(),handle,error))return false;
  if(handle){if(object->screenSpace){hades::ActionAnimationSetup setup;setup.useScreenLocation=true;if(!actionEffects_.setup(handle,setup,error))return false;}cursorHighlights_.emplace(key,handle);}
 }else if(const auto at=cursorHighlights_.find(key);at!=cursorHighlights_.end()){
  // HighlightOff2bfc00 cancels the retained allocation. The real manager's
  // completion phase owns source stop-sound/fade; never destroy it here.
  if(effects_.registry()->find(at->second)&&!effects_.registry()->cancel(at->second,error))return false;
  cursorHighlights_.erase(at);object->graphicsDirty=true; // conservative dirty refresh adaptation
 }
 error.clear();return true;
}
bool ScriptRuntime::advanceScreenCursor(float elapsed,std::string& error){try{
 if(!std::isfinite(elapsed)||elapsed<0||elapsed>1)throw std::runtime_error("Invalid cursor input elapsed");
 // Portable phase/clock adaptation: screen input elapsed, without world/owner
 // speed modifiers. Native forwards InputHandler's caller elapsed unchanged.
 bool editing=false;if(!configOptionBool("EditingMode",editing,error))return false;if(editing)return true;
 const auto old=screenCursor_.target;const auto* oldObject=findObjectSnapshot(state_,old.id);
 const auto* oldScreen=screenAnimations_.thing(old.id);
 const bool missing=old.id>=0&&(!oldObject||oldObject->entityHandle!=old.lifetime||!interactions_.contains(old)||(oldScreen&&oldScreen->dead));
 const auto has=[&](const auto& controls,const char* control){return std::binary_search(controls.begin(),controls.end(),std::string(control));};
 const bool pressed=has(rawControlsPressed_,"Select"),held=has(rawControlsDown_,"Select");bool repeat=false;
 if(old.id>=0&&!missing&&!interactions_.advanceCursorRepeat(old,held,elapsed,repeat,error))return false;
 if(!missing&&!pressed&&!repeat&&screenCursor_.checked&&screenCursor_.x==screenCursor_.checkedX&&screenCursor_.y==screenCursor_.checkedY&&!screenCursor_.forceUseCheck)return true;
 screenCursor_.forceUseCheck=false;
 // Current source menus clear this native group restriction. Keep other
 // configurations explicit until native group membership filtering is wired.
 lua_getglobal(state_,"GetConfigOptionValue");lua_newtable(state_);lua_pushliteral(state_,"ExclusiveInteractGroup");lua_setfield(state_,-2,"Name");
 if(lua_pcall(state_,1,1,0)!=LUA_OK){const char* detail=lua_tostring(state_,-1);error=detail?detail:"Cursor exclusive group lookup failed";lua_pop(state_,1);return false;}
 const bool restricted=!lua_isnil(state_,-1)&&!(lua_type(state_,-1)==LUA_TSTRING&&lua_rawlen(state_,-1)==0);lua_pop(state_,1);
 if(restricted){error="Cursor ExclusiveInteractGroup membership filtering not reconstructed";return false;}

 hades::InteractionKey selected;const auto providers=interactionProviders();
 const hades::CursorInteractionGeometryProvider geometry=[this](hades::InteractionKey key,hades::CursorInteractionGeometry& out,std::string& detail){
  out={};out.key=key;const auto* screen=screenAnimations_.thing(key.id);
  if(!screen||screen->dead||!screen->body||screen->body->entityHandle!=key.lifetime){detail.clear();return true;}
  out.screenSpace=true;out.parallax=screen->ownParallax*screen->mapParallax;std::array<int32_t,4> rectangle;
  if(!screenAnimations_.interactionGeometry(key.id,out.polygon,rectangle,detail))return false;
  const auto groups=snapshotSceneGroups(state_);const auto& group=screen->body->groups.empty()?screen->body->group:screen->body->groups.front();
  for(size_t i=0;i<groups.size();++i)if(groups[i].name==group){out.drawGroup=int32_t(i);break;}
  if(out.drawGroup<0){detail="Actual cursor draw group unavailable";return false;}return true;
 };
 for(const auto& player:snapshotInputPlayers(state_))if(player.playerIndex==1&&player.activeUnitLifetime){
  hades::InteractionUserFrame user;if(!interactionUserFrame({player.activeUnitId,player.activeUnitLifetime},user,error))return false;
  if(!hades::selectCursorInteraction(interactions_,user,{worldTime_,int32_t(worldFrame_)},screenCursor_.pointerInput,screenCursor_.x,screenCursor_.y,providers,geometry,selected,error))return false;
  break;
 }
 if(selected.id>=0){
  hades::InteractionSnapshot state;if(!interactions_.snapshot(selected,state,error))return false;
  if(state.enabled){
   if(!cursorHighlight(selected,true,error))return false;
   if(flag(state.definition,"CursorOnly")&&(pressed||repeat||flag(state.definition,"AutoActivate")||(!screenCursor_.pointerInput&&flag(state.definition,"AutoActivateWithGamepad")))){
    if(!useInteraction(selected,{},error))return false;
   }
  }
 }
 const auto physicallyVisible=[&](hades::InteractionKey key,bool& result){return screenAnimations_.physicallyVisible(key.id,key.lifetime,result,error);};

 if(selected!=old){
  hades::InteractionSnapshot previous;std::string missingError;
  if(old.id>=0&&interactions_.snapshot(old,previous,missingError)&&oldObject&&oldObject->entityHandle==old.lifetime){
   const bool primary=oldScreen&&oldScreen->primary;
   if(primary&&previous.enabled&&(flag(previous.definition,"FloatHighlightAnimation")||oldObject->animation!=name(previous.definition,"HighlightOnAnimation")))if(!cursorHighlight(old,false,error))return false;
   bool visible=false;if(!physicallyVisible(old,visible))return false;if(visible&&!dispatchThingCallbacks("OnMouseOff",old.id,[](lua_State* L,std::string&){lua_pushnil(L);lua_setfield(L,-2,"name");return true;})){error=error_;return false;}
  }
  if(selected.id>=0){
   bool visible=false;if(!physicallyVisible(selected,visible))return false;
   if(visible){const auto* screen=screenAnimations_.thing(selected.id);const bool created=screen&&worldTime_<=screen->creationTime;
    if(!dispatchThingCallbacks("OnMouseOver",selected.id,[created](lua_State* L,std::string&){lua_pushnil(L);lua_setfield(L,-2,"name");lua_pushboolean(L,created);lua_setfield(L,-2,"CreatedThisFrame");return true;})){error=error_;return false;}}
  }
  screenCursor_.target=selected;
  hades::log(hades::Severity::Info,"HadesLua","NATIVE_CURSOR_TARGET frame=%llu owner=%d x=%.3f y=%.3f",(unsigned long long)worldFrame_,selected.id,screenCursor_.x,screenCursor_.y);
 }
 screenCursor_.checked=true;screenCursor_.checkedX=screenCursor_.x;screenCursor_.checkedY=screenCursor_.y;
 error.clear();return true;
 }catch(const std::exception& e){error=e.what();return false;}}
