#include "lua_runtime.hpp"
#include "text_runtime.hpp"
#include "input_runtime.hpp"
#include "platform_log.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
void ScriptRuntime::setControlLabelProvider(std::function<bool(const std::string&,std::string&,std::string&)> provider){controlLabelProvider_=std::move(provider);if(state_)configureTextControlLabels(state_,controlLabelProvider_);}
bool ScriptRuntime::loadTextDefinitions(){try{
 auto file=assets_->open("text-fonts.lua");if(!file||file->size()>4*1024*1024)throw std::runtime_error("Original font/localization catalog unavailable");std::vector<char>b(file->size());size_t at=0;while(at<b.size()){auto n=file->read(b.data()+at,b.size()-at);if(!n)throw std::runtime_error("Truncated original text catalog");at+=n;}
 const int top=lua_gettop(state_);int status=luaL_loadbufferx(state_,b.data(),b.size(),"@text-fonts.lua","t");if(status==LUA_OK)status=lua_pcall(state_,0,1,0);if(status!=LUA_OK){std::string e=lua_tostring(state_,-1);lua_settop(state_,top);throw std::runtime_error(e);}hades::SourceValue data;const bool ok=hades::readSourceValue(state_,-1,data,error_);lua_settop(state_,top);if(!ok)return false;auto bank=std::make_shared<hades::TextFontBank>();bank->load(*assets_,data);configureTextRuntime(state_,bank);textFonts_=std::move(bank);return true;
 }catch(const std::exception&e){error_=e.what();return false;}}
bool ScriptRuntime::advanceText(float world,float screen,std::string&error){return advanceTextRuntime(state_,world,screen,error);}
bool ScriptRuntime::consumeSpeechText(const std::vector<hades::SpeechPresentation>&events,std::string&error){if(events.empty())return true;try{if(!textFonts_)throw std::runtime_error("Actual caption original font bank unavailable");const int top=lua_gettop(state_);lua_getglobal(state_,"GetConfigOptionValue");lua_newtable(state_);lua_pushliteral(state_,"Subtitles");lua_setfield(state_,-2,"Name");if(lua_pcall(state_,1,1,0)!=LUA_OK){std::string e=lua_tostring(state_,-1);lua_settop(state_,top);throw std::runtime_error(e);}if(!lua_isboolean(state_,-1)){lua_settop(state_,top);throw std::runtime_error("Actual Subtitles config bool unavailable");}const bool enabled=lua_toboolean(state_,-1);lua_settop(state_,top);return captionText_.consume(events,*textFonts_,enabled,error);}catch(const std::exception&e){error=e.what();return false;}}
std::vector<hades::TextVisual> ScriptRuntime::textVisuals()const{auto out=snapshotTextVisuals(state_);
 // GUIComponent.GetLocation169a70 resolves the live Thing parent link. TextBox
 // Draw176368..381 subtracts owner GetZLocation only with data+cb=true.
 for(auto& v:out)if(const auto* owner=findObjectSnapshot(state_,v.ownerId);owner&&owner->backend==ObjectBackend::Actor){const auto* a=actorWorld_.actor(v.ownerId);if(!a||!a->registered)continue;std::string error;if(!actorWorld_.location(v.ownerId,v.ownerX,v.ownerY,error))throw std::runtime_error(error);if(v.locationZWithOwner&&!actorWorld_.zLocation(v.ownerId,v.ownerZ,error))throw std::runtime_error(error);}
 auto captions=captionText_.visuals();out.insert(out.end(),captions.begin(),captions.end());return out;}

bool ScriptRuntime::menuTooltipDelays(float&show,float&hide,std::string&error){
 if(!configurationFloat("TooltipShowDelay",show,error)||!configurationFloat("TooltipHideDelay",hide,error))return false;
 return true;
}
int ScriptRuntime::hideTopMenuScreenTooltips(lua_State*L){
 char message[1024]{};bool ok=false;
 {auto*self=runtime(L);std::string error;float show=0,hide=0;ok=self&&self->menuTooltipDelays(show,hide,error)&&hideMenuTooltips(L,show,error);if(!ok)std::snprintf(message,sizeof message,"%s",error.c_str());}
 if(!ok)return luaL_error(L,"%s",message);return 0;
}
bool ScriptRuntime::advanceMenuScreenTooltips(float elapsed,std::string&error){
 // InGameUI.UpdateTooltip1d99f0: the first configured Player's actual
 // IsInputAllowed, World.mRunning/mFrame, and Camera fade alpha gate it.
 const auto players=snapshotInputPlayers(state_);bool eligible=roomActivated_&&worldFrame_>=2&&!players.empty()&&players.front().inputAllowed;
 const bool canQueue=eligible&&int(std::clamp(cameraFade_.color()[3],0.f,1.f)*255)<255;
 float show=0,hide=0;if(!menuTooltipDelays(show,hide,error))return false;
 auto key=canQueue?screenCursor_.target:hades::InteractionKey{};std::array<float,2>anchor{};
 if(key.id>=0){
  const auto*owner=findObjectSnapshot(state_,key.id);const auto*screen=screenAnimations_.thing(key.id);
  bool visible=false;
  if(!owner||owner->entityHandle!=key.lifetime||!screen||screen->dead)key={};
  else{
   if(!screenAnimations_.physicallyVisible(key.id,key.lifetime,visible,error))return false;
   const auto group=owner->groups.empty()?owner->group:owner->groups.front();
   bool groupVisible=false;for(const auto&g:snapshotSceneGroups(state_))if(g.name==group){groupVisible=g.visible;break;}
   if(!visible||!groupVisible)key={};
   else{hades::InteractionSnapshot interaction;if(!interactions_.snapshot(key,interaction,error))return false;
    // Native currently proved screen-space branch: Thing.GetLocation + the
    // original InteractData TooltipOffset; no invented world projection.
    if(!owner->screenSpace){error="Native world tooltip camera projection not reconstructed";return false;}
    anchor={owner->x,owner->y};
    for(size_t i=0;i<2;++i)if(const auto*p=interaction.definition.get(i?"TooltipOffsetY":"TooltipOffsetX"))anchor[i]+=float(p->number());
   }
  }
 }
 return advanceMenuTooltips(state_,elapsed,eligible,key.id,key.lifetime,anchor,show,hide,error);
}
