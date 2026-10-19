#include "lua_runtime.hpp"
#include "text_runtime.hpp"
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
bool ScriptRuntime::loadTextDefinitions(){try{
 auto file=assets_->open("text-fonts.lua");if(!file||file->size()>4*1024*1024)throw std::runtime_error("Original font/localization catalog unavailable");std::vector<char>b(file->size());size_t at=0;while(at<b.size()){auto n=file->read(b.data()+at,b.size()-at);if(!n)throw std::runtime_error("Truncated original text catalog");at+=n;}
 const int top=lua_gettop(state_);int status=luaL_loadbufferx(state_,b.data(),b.size(),"@text-fonts.lua","t");if(status==LUA_OK)status=lua_pcall(state_,0,1,0);if(status!=LUA_OK){std::string e=lua_tostring(state_,-1);lua_settop(state_,top);throw std::runtime_error(e);}hades::SourceValue data;const bool ok=hades::readSourceValue(state_,-1,data,error_);lua_settop(state_,top);if(!ok)return false;auto bank=std::make_shared<hades::TextFontBank>();bank->load(*assets_,data);configureTextRuntime(state_,bank);textFonts_=std::move(bank);return true;
 }catch(const std::exception&e){error_=e.what();return false;}}
bool ScriptRuntime::advanceText(float world,float screen,std::string&error){return advanceTextRuntime(state_,world,screen,error);}
bool ScriptRuntime::consumeSpeechText(const std::vector<hades::SpeechPresentation>&events,std::string&error){if(events.empty())return true;try{if(!textFonts_)throw std::runtime_error("Actual caption original font bank unavailable");const int top=lua_gettop(state_);lua_getglobal(state_,"GetConfigOptionValue");lua_newtable(state_);lua_pushliteral(state_,"Subtitles");lua_setfield(state_,-2,"Name");if(lua_pcall(state_,1,1,0)!=LUA_OK){std::string e=lua_tostring(state_,-1);lua_settop(state_,top);throw std::runtime_error(e);}if(!lua_isboolean(state_,-1)){lua_settop(state_,top);throw std::runtime_error("Actual Subtitles config bool unavailable");}const bool enabled=lua_toboolean(state_,-1);lua_settop(state_,top);return captionText_.consume(events,*textFonts_,enabled,error);}catch(const std::exception&e){error=e.what();return false;}}
std::vector<hades::TextVisual> ScriptRuntime::textVisuals()const{auto out=snapshotTextVisuals(state_);auto captions=captionText_.visuals();out.insert(out.end(),captions.begin(),captions.end());return out;}
