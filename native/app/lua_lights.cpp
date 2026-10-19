#include "lua_runtime.hpp"
#include "recovered_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
bool ScriptRuntime::loadActorLightDefinitions(){
 try{
    auto file=assets_->open("actor-lights.lua");if(!file||file->size()>1024*1024)throw std::runtime_error("Missing/oversized original actor light catalog");
    std::vector<char> bytes(file->size());size_t at=0;while(at<bytes.size()){const auto n=file->read(bytes.data()+at,bytes.size()-at);if(!n)throw std::runtime_error("Truncated source actor light catalog");at+=n;}
    const int top=lua_gettop(state_);auto result=luaL_loadbufferx(state_,bytes.data(),bytes.size(),"@actor-lights.lua","t");if(result==LUA_OK)result=lua_pcall(state_,0,1,0);
    if(result!=LUA_OK){error_=lua_tostring(state_,-1);lua_settop(state_,top);return false;}
    hades::SourceValue catalog;const bool read=hades::readSourceValue(state_,-1,catalog,error_);lua_settop(state_,top);if(!read)return false;
    const auto* expected=catalog.get("ImageBankSha256");auto identity=assets_->open("actor-light-sha256.bin");if(!expected||!identity||identity->size()!=32)throw std::runtime_error("Actor light original image-bank identity unavailable");
    std::array<uint8_t,32> digest{};at=0;while(at<digest.size()){const auto n=identity->read(digest.data()+at,digest.size()-at);if(!n)throw std::runtime_error("Truncated actor light bank identity");at+=n;}
    constexpr char hex[]="0123456789abcdef";std::string actual;for(const auto byte:digest){actual+=hex[byte>>4];actual+=hex[byte&15];}if(actual!=expected->text())throw std::runtime_error("Actor light image-bank companion changed");
    auto bank=std::make_shared<hades::ActorAnimationBank>();bank->load(*assets_,"actor-light-frames.bin");return actorLights_.load(std::move(catalog),std::move(bank),effects_.registry(),error_);
 }catch(const std::exception& e){error_=e.what();return false;}
}
bool ScriptRuntime::initializeActorLight(int32_t id,std::string& error){
    const auto random=[this](double& value,std::string& detail){if(nextRecoveredEngineRandom(state_,value)){detail.clear();return true;}detail="Actual original actor-light stream0 unavailable";return false;};
    return actorLights_.admit(id,random,error);
}
bool ScriptRuntime::advanceActorLights(float dt,std::string& error){
    if(worldSpeedModifier_!=1){error="Nondefault source light world speed requires its actual modified-time projection";return false;}
    const auto random=[this](double& value,std::string& detail){if(nextRecoveredEngineRandom(state_,value)){detail.clear();return true;}detail="Actual original light-loop stream0 unavailable";return false;};
    return actorLights_.advanceControllers(dt,random,error);
}
std::vector<int32_t> ScriptRuntime::actorLightOwners()const{
    std::vector<int32_t> ids;for(const auto& light:actorLights_.snapshot()){const auto* actor=actorWorld_.actor(light.ownerId);if(actor&&actor->registered&&!actor->isDisposed&&actor->entityHandle==light.ownerLifetime)ids.push_back(light.ownerId);}return ids;
}
bool ScriptRuntime::sampleActorLight(int32_t id,hades::ActorLightVisual& out,std::string& error){
    const auto* actor=actorWorld_.actor(id);if(!actor||!actor->registered||!actor->object){error="Original light draw owner unavailable";return false;}
    std::array<uint8_t,4> drawColor{};
    for(size_t c=0;c<4;++c){const auto fraction=c<3?actor->tint[c]:actor->object->alpha;if(!std::isfinite(fraction)){error="Nonfinite native light owner color fraction";return false;}const auto f=uint32_t(std::clamp(int(fraction*255),0,255));drawColor[c]=uint8_t(uint32_t(actor->baseColorBytes[c])*f/255);}
    return actorLights_.visual(id,drawColor,out,error);
}
