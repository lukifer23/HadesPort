#include "lua_runtime.hpp"
#include "effect_arguments.hpp"
#include "recovered_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <cstdio>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace {
float effectNumber(const hades::WeaponEffectDefinition& data,const char* key){const auto i=data.fields.find(key);if(i==data.fields.end()||!std::holds_alternative<double>(i->second))throw std::runtime_error(std::string("Source effect numeric field unavailable: ")+key);const auto n=float(std::get<double>(i->second));if(!std::isfinite(n))throw std::runtime_error("Nonfinite source effect value");return n;}
bool effectFlag(const hades::WeaponEffectDefinition& data,const char* key){const auto i=data.fields.find(key);if(i==data.fields.end()||!std::holds_alternative<bool>(i->second))throw std::runtime_error(std::string("Source effect flag unavailable: ")+key);return std::get<bool>(i->second);}
}
bool ScriptRuntime::effectCallback(hades::WeaponSelfEffectInstance& instance,const hades::WeaponEffectDefinition& data,bool reapplied,bool cleared,std::string& error){
    const auto* status=instance.state.status.get();const auto* actor=status?actorWorld_.actor(status->unitId):nullptr;
    if(!actor||!actor->registered||!actor->object){error.clear();return true;} // Source UnitManager.Get -> null.
    const auto extra=[&](lua_State* L,std::string& detail){
        const auto* current=actorWorld_.actor(status->unitId);if(!current||!current->object){detail="Effect callback lost actual owner";return false;}
        if(cleared){
            lua_pushstring(L,hades::nativeEffectTypeName(data.type));lua_setfield(L,-2,"EffectType");
            lua_pushstring(L,data.name.c_str());lua_setfield(L,-2,"EffectName");
            lua_pushboolean(L,current->registered&&current->object->active);lua_setfield(L,-2,"Exists");
            lua_pushboolean(L,effectFlag(data,"IsVulnerabilityEffect"));lua_setfield(L,-2,"IsVulnerabilityEffect");
            lua_pushnumber(L,instance.state.appliedElapsedTimeMultiplier);lua_setfield(L,-2,"ElapsedTimeMultiplier");return true;
        }
        hades::EffectApplyArguments args;args.ownerId=current->id;args.ownerName=current->object->name;args.type=data.type;args.effectName=data.name;args.numStacks=instance.state.numStacks;args.reapplied=reapplied;
        args.modifier=effectNumber(data,"Modifier");args.duration=effectNumber(data,"Duration");args.isVulnerabilityEffect=effectFlag(data,"IsVulnerabilityEffect");args.elapsedTimeMultiplier=instance.state.appliedElapsedTimeMultiplier;args.unitElapsedTimeMultiplier=current->timeMultiplier;
        const int target=lua_gettop(L);pushObjectLuaTable(L,current->id);const int alias=lua_gettop(L);
        if(!hades::pushEffectApplyArguments(L,args,alias,detail)){lua_pop(L,1);return false;}
        lua_replace(L,target);lua_pop(L,1);return true;
    };
    if(!dispatchCallbacks(cleared?"OnEffectCleared":"OnEffectApply",actor->object->name.c_str(),actor->id,extra)){error=error_;return false;}return true;
}
hades::WeaponSelfEffectProviders ScriptRuntime::selfEffectProviders(){
    hades::WeaponSelfEffectProviders providers;
    providers.randomStream0=[this](double& value,std::string& error){if(nextRecoveredEngineRandom(state_,value))return true;error="Effect source RNG unavailable";return false;};
    providers.affect=[this](hades::EffectAffectOperation operation,hades::WeaponSelfEffectInstance& instance,hades::EffectAffectOwner& owner,const hades::WeaponEffectDefinition& data,bool reapplied,std::string& error){
        using K=hades::EffectAffectOperation;
        if(operation==K::RefreshAttachedAnimationTime){
            // Effect.AffectUnit5ce4c refreshes the current primary's
            // clock link after applying the real Thing elapsed multiplier.
            auto* actor=actorWorld_.actor(owner.id);if(!actor||!actor->registered){error="Effect animation clock owner unavailable";return false;}
            const auto handle=actor->primaryAnimationLifetime?actor->primaryAnimationLifetime:actor->primaryEffectAnimation;
            if(!handle){error="Effect animation clock requires actual primary lifetime";return false;}
            return effects_.registry()->linkTimeMultiplier(handle,actor->id,actor->entityHandle,error);
        }
        if(operation==K::OnEffectApply)return effectCallback(instance,data,reapplied,false,error);
        hades::WeaponEffectRuntimeData projected;if(!hades::projectWeaponEffectRuntimeData(data,instance.dataHandle,projected,error))return false;
        if(operation==K::StartAnimation)return setActorAnimation(owner.id,projected.base.startAnimation,error);
        if(operation==K::OnStun){
            // Unit.OnStun2f3b80 has only its positive-duration animation gate;
            // weapon RequestFire performs its own genuine arsenal gates.
            auto* unit=actorWorld_.actor(owner.id);if(!unit||!unit->registered||!unit->arsenal){error="Actual stunned Unit arsenal unavailable";return false;}
            if(projected.base.duration>0){const auto* animation=actorWorld_.currentProperty(owner.id,"OnStunAnimation");if(!animation){error="Actual Unit.OnStunAnimation unavailable";return false;}if(!setActorAnimation(owner.id,animation->text(),error))return false;}
            const auto* weapon=actorWorld_.currentProperty(owner.id,"OnStunWeapon");if(!weapon){error="Actual Unit.OnStunWeapon unavailable";return false;}
            if(unit->arsenal->hasWeapon(weapon->text())){
                float speed=0;if(!configurationFloat("SpeedMultiplier",speed,error))return false;
                hades::WeaponOwnerFrame frame;if(!actorWorld_.weaponOwnerFrame(owner.id,worldTime_,speed,false,inputAllowed(),frame,error))return false;
                if(!unit->arsenal->requestTouchdownFire(weapon->text(),frame,error)&&!error.empty())return false;
            }
            error.clear();return true;
        }
        if(operation==K::PlaySound){std::array<float,3> p{};if(!actorWorld_.location(owner.id,p[0],p[1],error)||!actorWorld_.zLocation(owner.id,p[2],error))return false;uint32_t id=0;if(!playAudioCue(projected.base.soundName,owner.id,p,id,error))return false;instance.state.soundId=int32_t(id);return true;}
        error="Source effect operation requires live consumer: effect="+data.name+" kind="+std::to_string(int(operation));return false;
    };
    providers.onCleared=[this](hades::WeaponSelfEffectInstance& effect,const hades::WeaponEffectDefinition& data,std::string& error){return effectCallback(effect,data,false,true,error);};
    providers.haltPhysics=[this](int32_t id,std::string& error){
        // PhysicsComponent::Halt2bcd20: mVelocity=mMoveShift=mZero, UpwardVelocity=0,
        // LastForceIsSelfApplied=false; a live ImpulsingCueId is stopped and zeroed.
        auto* a=actorWorld_.actor(id);if(!a||!a->physics){error="PhysicsComponent::Halt lost actual component";return false;}
        auto& p=*a->physics;p.velocity={};p.moveShift={};p.upVelocity=0;p.lastForceSelfApplied=false;
        if(p.impulsingSoundId!=0){error="PhysicsComponent::Halt impulsing cue stop requires actual audio channel consumer";return false;}
        error.clear();return true;};
    providers.invulnerable=[this](hades::WeaponSelfEffectInstance&,int32_t owner,const hades::WeaponEffectDefinition& data,bool clear,std::string& error){
        // InvulnerableEffect::AffectUnit5e580 / Clear5e7c0 (.local/reports/native-combat2-invuln-{affect,clear}-static.json).
        // EffectData (+0x68 = EffectDataDef): AngleCoverage 0x19c, AngleCoverageOffset 0x198, Modifier 0x180.
        auto* unit=actorWorld_.actor(owner);if(!unit||!unit->registered){error="InvulnerableEffect owner unavailable";return false;}
        const auto num=[&](const char* k,float& v){auto f=data.fields.find(k);if(f==data.fields.end()||!std::holds_alternative<double>(f->second)){error=std::string("InvulnerableEffect field unavailable: ")+k;return false;}v=float(std::get<double>(f->second));return std::isfinite(v);};
        float coverage=0,offset=0,modifier=0;if(!num("AngleCoverage",coverage)||!num("AngleCoverageOffset",offset)||!num("Modifier",modifier))return false;
        if(coverage>0&&coverage<360.0f){
            // Unit pLife(+0xd0) InvulnerableCoverage(+0x3c)/Offset(+0x40) += / -= ; no SetInvulnerable.
            if(!unit->hasLife){error="InvulnerableEffect coverage requires actual LifeComponent";return false;}
            if(!clear){unit->life.invulnerableCoverage+=coverage;unit->life.invulnerableCoverageOffset+=offset;}
            else{unit->life.invulnerableCoverage-=coverage;unit->life.invulnerableCoverageOffset-=offset;}
            error.clear();return true;
        }
        if(modifier==0)return clear?lifeSetInvulnerable(owner,error):lifeSetVulnerable(owner,error);
        const auto* player=actorWorld_.currentProperty(owner,"PlayerControlled");if(!player){error="InvulnerableEffect UnitData PlayerControlled(+0x69) unavailable";return false;}
        if(player->boolean()){
            // ScriptManager::CallFunction<char const*>5ecb0(SetPlayerInvulnerable|SetPlayerVulnerable, EffectData name).
            const char* fn=clear?"SetPlayerVulnerable":"SetPlayerInvulnerable";const int top=lua_gettop(state_);
            lua_getglobal(state_,fn);if(!lua_isfunction(state_,-1)){lua_settop(state_,top);error=std::string("Original Lua function unavailable: ")+fn;return false;}
            lua_pushstring(state_,data.name.c_str());
            if(lua_pcall(state_,1,0,0)!=LUA_OK){error=lua_tostring(state_,-1)?lua_tostring(state_,-1):"InvulnerableEffect Lua call failed";lua_settop(state_,top);return false;}
            lua_settop(state_,top);error.clear();return true;
        }
        return clear?lifeSetVulnerable(owner,error):lifeSetInvulnerable(owner,error);
    };
    // Source empty FX and ordinary first-Sword clear/reapply branches are
    // implemented inside the real effect world. Nonempty branches require
    // explicit consumers and fail there if not yet implemented.
    return providers;
}
bool ScriptRuntime::advanceUnitEffects(float dt,std::string& error){
    const auto providers=selfEffectProviders();
    for(auto id:actorWorld_.registrationOrder()){
        auto* actor=actorWorld_.actor(id);if(!actor||!actor->registered||!actor->status)continue;
        const auto* stun=actorWorld_.currentProperty(id,"StunBreakModifier");if(!stun){error="Unit status requires actual StunBreakModifier";return false;}
        // Unit.UpdateSelf2f1497 captures manager elapsed x live Thing+80
        // before its UnitStatus.Update call (2f1624). Effect's own fraction
        // then operates relative to that Unit multiplier inside UnitStatus.
        const float elapsed=dt*actor->timeMultiplier;
        if(!actor->status->advanceDisableTimers(elapsed,float(stun->number()),error)||!selfEffects_.updateUnit(id,elapsed,providers,error))return false;
    }
    return true;
}

// LifeComponent::SetInvulnerable2b79c0 (.local/reports/native-combat4-life-SetInvulnerable-static.json):
// mIsInvulnerable(+0x31)=1; World+0x3c8 set insert (owner); cancel existing pInvulnerableFx(+0x68);
// LifeData(+0x60) -> pInvulnerableFx = AnimationManager::Create(InvulnerableFx, owner).
bool ScriptRuntime::lifeSetInvulnerable(int32_t id,std::string& error){
    auto* a=actorWorld_.actor(id);if(!a||!a->registered){error="SetInvulnerable requires actual owner";return false;}
    if(!a->hasLife){error.clear();return true;}
    a->invulnerable=true;
    const auto* fx=actorWorld_.currentProperty(id,"Life.InvulnerableFx");if(!fx){error="Life.InvulnerableFx unavailable";return false;}
    if(!fx->text().empty()&&fx->text()!="null"){error="LifeComponent::SetInvulnerable pInvulnerableFx ownership not reconstructed: "+fx->text();return false;}
    error.clear();return true;
}
// LifeComponent::SetVulnerable2b7a80: mIsInvulnerable=0; cancel pInvulnerableFx.
bool ScriptRuntime::lifeSetVulnerable(int32_t id,std::string& error){
    auto* a=actorWorld_.actor(id);if(!a||!a->registered){error="SetVulnerable requires actual owner";return false;}
    if(!a->hasLife){error.clear();return true;}
    a->invulnerable=false;error.clear();return true;
}
int ScriptRuntime::lifeInvulnerableAction(lua_State* L){
    // Thing::RunScriptAction SetInvulnerable/SetVulnerable -> LifeComponent methods (hud-*-Thing-RunScriptAction.json).
    char message[1024]{};bool ok=false;
    try{auto* self=runtime(L);if(!self)throw std::runtime_error("SetInvulnerable runtime unavailable");
        const bool invulnerable=lua_toboolean(L,lua_upvalueindex(2));
        hades::SourceValue args;std::string error;if(!lua_istable(L,1)||!hades::readSourceValue(L,1,args,error))throw std::runtime_error(error.empty()?"SetInvulnerable requires argument table":error);
        for(const char* k:{"Ids","DestinationId","DestinationIds","Name","Names"})if(args.get(k))throw std::runtime_error(std::string("SetInvulnerable selector not reconstructed: ")+k);
        const auto* id=args.get("Id");if(!id)throw std::runtime_error("SetInvulnerable requires Id");
        ok=invulnerable?self->lifeSetInvulnerable(int32_t(id->number()),error):self->lifeSetVulnerable(int32_t(id->number()),error);
        if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());
    }catch(const std::exception& e){std::snprintf(message,sizeof(message),"%s",e.what());}
    if(!ok)return luaL_error(L,"%s",message);return 0;
}
int ScriptRuntime::hasEffectAction(lua_State* L){
    // ScriptAction::HasEffect269b70 (.local/reports/native-combat4-script-has-effect-static.json): Id
    // (args[1].Id) -> live entity; a Unit (typeflags 0x221) -> UnitStatus::GetEffect2fdf60(EffectName)
    // (unit_status.cpp getEffect: toDelete veto, then active, pending) != null.
    char message[1024]{};bool ok=false,result=false;
    try{auto* self=runtime(L);if(!self)throw std::runtime_error("HasEffect runtime unavailable");
        hades::SourceValue args;std::string error;if(!lua_istable(L,1)||!hades::readSourceValue(L,1,args,error))throw std::runtime_error(error.empty()?"HasEffect requires argument table":error);
        const auto* id=args.get("Id");const auto* name=args.get("EffectName");
        if(id){const auto* a=self->actorWorld_.actor(int32_t(id->number()));
            if(a&&a->registered&&a->definition&&a->definition->kind=="Unit"&&a->status)result=bool(a->status->getEffect(name?name->text():std::string{}));}
        ok=true;
    }catch(const std::exception& e){std::snprintf(message,sizeof(message),"%s",e.what());}
    if(!ok)return luaL_error(L,"%s",message);lua_pushboolean(L,result);return 1;
}
int ScriptRuntime::swapWeaponAction(lua_State* L){
    // ScriptAction::SwapWeapon25bfe0 (.local/reports/native-combat4-script-swap-weapon-pseudocode.c):
    // for each Names entry, for each destination Unit (typeflags 0x221): w=Arsenal::GetWeapon(name);
    // w && (!RequireCurrentControl || w controllable(+0x448)) -> t=SwapWeaponControl2fed10(w,SwapWeaponName);
    // t: ClearFireRequest -> w current request cleared; StompOriginalWeapon -> t.pGainedControlFrom=0,
    // else a non-nil GainedControlFrom with an owned weapon -> t.pGainedControlFrom=it.
    // RequireCurrentControl && !controllable && ExtendControlIfSwapActive: controllable SwapWeaponName
    // gets ControlTime(+0x20)=ControlWindow.
    char message[1024]{};bool ok=false;
    try{auto* self=runtime(L);if(!self)throw std::runtime_error("SwapWeapon runtime unavailable");
        hades::SourceValue args;std::string error;if(!lua_istable(L,1)||!hades::readSourceValue(L,1,args,error))throw std::runtime_error(error.empty()?"SwapWeapon requires argument table":error);
        const auto boolean=[&](const char* k){const auto* v=args.get(k);return v&&v->boolean();};
        const auto text=[&](const char* k){const auto* v=args.get(k);return v?v->text():std::string{};};
        if(args.get("DestinationName")||args.get("DestinationNames"))throw std::runtime_error("SwapWeapon destination-name selection not reconstructed");
        if(boolean("LoadPackages"))throw std::runtime_error("SwapWeapon LoadPackages GameAssetManager::LoadPackageAsync not reconstructed");
        std::vector<int32_t> destinations;
        if(const auto* v=args.get("DestinationId"))destinations.push_back(int32_t(v->number()));
        if(const auto* v=args.get("DestinationIds"))for(const auto& e:v->array()){const int32_t id=int32_t(e.number());if(std::find(destinations.begin(),destinations.end(),id)==destinations.end())destinations.push_back(id);}
        if(destinations.size()>1)throw std::runtime_error("SwapWeapon multi-destination hash-set order not reconstructed");
        std::vector<std::string> names;if(const auto* v=args.get("Names")){for(const auto& e:v->array())names.push_back(e.text());}else if(const auto* v=args.get("Name"))names.push_back(v->text());
        const std::string to=text("SwapWeaponName");
        for(const auto& name:names)for(const auto id:destinations){
            auto* a=self->actorWorld_.actor(id);if(!a||!a->registered||!a->definition||a->definition->kind!="Unit"||!a->arsenal)continue;
            auto& arsenal=*a->arsenal;if(!arsenal.hasWeapon(name))continue;
            const auto order=arsenal.controllableWeapons();const auto controllable=[&](const std::string& n){return std::find(order.begin(),order.end(),n)!=order.end();};
            if(!boolean("RequireCurrentControl")||controllable(name)){
                float speed=0;if(!self->configurationFloat("SpeedMultiplier",speed,error))throw std::runtime_error(error);
                hades::WeaponOwnerFrame frame;if(!self->actorWorld_.weaponOwnerFrame(id,self->worldTime_,speed,false,self->inputAllowed(),frame,error))throw std::runtime_error(error);
                const bool swapped=arsenal.swapWeaponControl(name,to,frame,error);if(!swapped){if(!error.empty())throw std::runtime_error(error);continue;}
                if(boolean("ClearFireRequest")){if(auto* w=arsenal.scriptMutableWeapon(name))w->currentFireRequest.reset();}
                auto* target=arsenal.scriptMutableWeapon(to);if(!target)continue;
                if(boolean("StompOriginalWeapon"))target->gainedControlFrom.clear();
                else if(args.get("GainedControlFrom")){const auto from=text("GainedControlFrom");if(arsenal.hasWeapon(from))target->gainedControlFrom=from;}
            }else if(boolean("ExtendControlIfSwapActive")&&controllable(to)){
                if(auto* w=arsenal.scriptMutableWeapon(to))w->controlTime=w->definition.controlWindow;
            }
        }
        ok=true;
    }catch(const std::exception& e){std::snprintf(message,sizeof(message),"%s",e.what());}
    if(!ok)return luaL_error(L,"%s",message);return 0;
}
int ScriptRuntime::clearEffectAction(lua_State* L){
    // Action 144 ClearEffect -> per selected Thing RunScriptAction. Thing default:
    // no-op (non-Units). Unit::RunScriptAction case VA1802f410a..4320 (pseudocode:
    // native-script-motion-unit-action-static.json; UnitStatus::ClearEffect2fd9d0):
    //   All ? (HasKey ExcludeNames ? per active effect not excluded -> ClearEffect(name)
    //          : per active effect !cleared -> Clear, then UnitStatus::Reset2fd4c0);
    //         BlockAll -> Unit+0x4f1 (UnitStatus+0x91 mBlockAll) = 1
    //       : ScriptArgs::Names (Name/Names) -> UnitStatus::ClearEffect each.
    // BlockAll is read by UnitStatus::AddEffect2fdca0 (lua_projectiles.cpp admission).
    // Selector: Id plus Ids, deduplicated ascending like actorMotionAction
    // (approximation: native multi-ID iteration order not recovered).
    char message[1024]{};bool ok=false;
    try{auto* self=runtime(L);if(!self)throw std::runtime_error("ClearEffect runtime unavailable");
        hades::SourceValue args;std::string error;if(!lua_istable(L,1)||!hades::readSourceValue(L,1,args,error))throw std::runtime_error(error.empty()?"ClearEffect requires argument table":error);
        for(const char* k:{"DestinationId","DestinationIds","DestinationName","DestinationNames"})if(args.get(k))throw std::runtime_error(std::string("ClearEffect selector not reconstructed: ")+k);
        std::vector<int32_t> ids;const auto add=[&](const hades::SourceValue& v){const double n=v.number();if(!std::isfinite(n)||n!=std::trunc(n)||n<INT32_MIN||n>INT32_MAX)throw std::runtime_error("ClearEffect requires signed32 object IDs");ids.push_back(int32_t(n));};
        if(const auto* v=args.get("Id"))add(*v);
        if(const auto* v=args.get("Ids"))for(const auto& e:v->array())add(e);
        std::sort(ids.begin(),ids.end());ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
        const auto flag=[&](const char* k){const auto* v=args.get(k);return v&&v->boolean();};
        const bool all=flag("All"),blockAll=flag("BlockAll");
        std::vector<std::string> names;
        if(!all){if(const auto* v=args.get("Names")){for(const auto& e:v->array())names.push_back(e.text());}else if(const auto* v=args.get("Name"))names.push_back(v->text());}
        std::vector<std::string> exclude;const auto* excludeValue=all?args.get("ExcludeNames"):nullptr;
        if(excludeValue){if(const auto* o=std::get_if<hades::SourceValue::Object>(&excludeValue->value);o&&!o->empty())throw std::runtime_error("ClearEffect keyed ExcludeNames table iteration order not reconstructed");for(const auto& e:excludeValue->array())exclude.push_back(e.text());}
        const auto providers=self->selfEffectProviders();
        for(const auto id:ids){
            auto* a=self->actorWorld_.actor(id);
            if(!a||!a->registered||!a->object)continue;
            if(!a->definition||a->definition->kind!="Unit")continue; // Thing::RunScriptAction default.
            if(!a->status)throw std::runtime_error("ClearEffect Unit lacks actual UnitStatus");
            if(all){
                if(!self->selfEffects_.clearAllEffects(id,excludeValue?&exclude:nullptr,providers,
                    [](std::string& e){e="ClearEffect Reset of a traitor Unit requires native SetAsAlly2fe360 consumer";return false;},error))throw std::runtime_error(error);
                a=self->actorWorld_.actor(id);
                if(blockAll&&a&&a->status)a->status->blockAll=true;
            }else for(const auto& name:names)if(!self->selfEffects_.clearNamedEffect(id,name,providers,error))throw std::runtime_error(error);
        }
        ok=true;
    }catch(const std::exception& e){std::snprintf(message,sizeof(message),"%s",e.what());}
    if(!ok)return luaL_error(L,"%s",message);return 0;
}
void ScriptRuntime::registerLifeActions(){
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,clearEffectAction,1);lua_setglobal(state_,"ClearEffect");
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,swapWeaponAction,1);lua_setglobal(state_,"SwapWeapon");
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,hasEffectAction,1);lua_setglobal(state_,"HasEffect");
    lua_pushlightuserdata(state_,this);lua_pushboolean(state_,1);lua_pushcclosure(state_,lifeInvulnerableAction,2);lua_setglobal(state_,"SetInvulnerable");
    lua_pushlightuserdata(state_,this);lua_pushboolean(state_,0);lua_pushcclosure(state_,lifeInvulnerableAction,2);lua_setglobal(state_,"SetVulnerable");
}
