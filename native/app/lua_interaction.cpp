// Interact / Useable integration between the recovered InteractionWorld
// (interaction_runtime.cpp: UseableManager grid, IsUseableBy, Interact::Use
// stages) and the live actor world, input and original Lua triggers.
//
// Provenance (EngineWin64s.dll, image base 0x180000000):
//   Interact::Use                     RVA 0x2bfcd0
//   ScriptManager::OnUsed             RVA 0x28c360 (isSource(owner); args UserId, AttachedTable=owner table)
//   ScriptManager::OnActiveUseTarget  RVA 0x28bd00 (UserId, AttachedTable, AutoActivate)
//   ScriptManager::OnActiveUseTargetLost RVA 0x28c040 (same args)
//   ScriptManager::OnAutoUseFailed    RVA 0x28b9b0
//   ScriptManager::OnHealed           RVA 0x289550 (skips when amount <= 0)
//   Script::isSource                  RVA 0x24ea10 (ID, group, GetName, _UNIT/_OBSTACLE)
//   PlayerUnitInput use-target update FUN_18020f6c0, player find FUN_18020fa20,
//   use FUN_180210d30 (pseudocode: .local/reports/native-interaction-*-pseudocode-*.txt).
//
// Approximations (labelled; see native/analysis/interaction-integration-contract.json):
//   A1 useReach: UnitData+0x308 (added to manual range in FUN_18020fa20) is not
//      recovered; treated as 0, so manual selection range is Interact.Distance only.
//   A2 Thing::GetArea is the AABB of the live collision polygon (as ApplyShift's
//      mRectangle approximation); only consulted when Distance<=0 or UseExtents.
//   A3 Every live Thing is treated as having a Body; vertical body flip and the
//      primary-animation flip bit are not tracked (0/false).
//   A4 The use-target update and the manual Use press run in one stage right
//      after local player input (original: HandleInput manual branch, then the
//      PlayerUnitInput update); UseableManager::Update runs at the start of it.
//   A5 Gamepad/mouse: Android has no mouse cursor; useMouse=false.
#include "lua_runtime.hpp"
#include "object_runtime.hpp"
#include "scene_runtime.hpp"
#include "input_runtime.hpp"
#include "platform_log.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
extern "C" {
#include <lua.h>
}

namespace {
constexpr const char* tag="HadesLua";
std::string interactText(const hades::SourceValue& def,const char* key){const auto* v=def.get(key);return v&&std::holds_alternative<std::string>(v->value)?v->text():std::string();}
bool interactBool(const hades::SourceValue& def,const char* key){const auto* v=def.get(key);return v&&std::holds_alternative<bool>(v->value)&&v->boolean();}
float interactNumber(const hades::SourceValue& def,const char* key){const auto* v=def.get(key);return v&&std::holds_alternative<double>(v->value)?float(v->number()):0.f;}
}

bool ScriptRuntime::sourceTokenMatches(const std::string& token,const char* sourceName,int32_t actorId,std::string& error){
    error.clear();
    if(sourceName&&token==sourceName)return true; // virtual GetName (type name)
    const auto* object=findObjectSnapshot(state_,actorId);
    if(!object){error="Script::isSource source Thing unavailable";return false;}
    if(token=="_UNIT")return object->dataType==2;
    if(token=="_OBSTACLE")return object->dataType==1;
    if(!token.empty()&&std::all_of(token.begin(),token.end(),[](char c){return c>='0'&&c<='9';}))return std::stoll(token)==int64_t(actorId);
    if(object->groups.empty())return object->group==token;
    return std::find(object->groups.begin(),object->groups.end(),token)!=object->groups.end();
}

bool ScriptRuntime::dispatchThingCallbacks(const char* event,int32_t actorId,const TriggerArguments& extra){
    const auto* object=findObjectSnapshot(state_,actorId);
    if(!object){error_=std::string(event)+" source Thing unavailable";return false;}
    const std::string name=object->name;
    thingSourceMatch_=true;
    const bool ok=dispatchCallbacks(event,name.c_str(),actorId,extra);
    thingSourceMatch_=false;
    return ok;
}

bool ScriptRuntime::initializeInteraction(int32_t id,std::string& error){
    auto* a=actorWorld_.actor(id);
    if(!a||!a->registered||!a->object){error="Interact registration needs a registered Thing";return false;}
    const auto* interact=actorWorld_.currentProperty(id,"Thing.Interact");
    if(!interact||std::holds_alternative<std::monostate>(interact->value)){error.clear();return true;}
    if(!interactionGridReady_){interactions_.configureSparseGrid();interactionGridReady_=true;}
    if(!interactions_.add({id,a->entityHandle},*interact,{worldTime_,int32_t(worldFrame_)},error)){error="Interact "+a->object->name+": "+error;return false;}
    return true;
}

bool ScriptRuntime::removeInteraction(int32_t id,std::string& error){
    for(const auto& s:interactions_.snapshots())if(s.key.id==id){
        for(auto it=useTargets_.begin();it!=useTargets_.end();){if(it->second==s.key)it=useTargets_.erase(it);else ++it;}
        return interactions_.remove(s.key,error);
    }
    error.clear();return true;
}

bool ScriptRuntime::interactionOwnerFrame(hades::InteractionKey key,hades::InteractionOwnerFrame& f,std::string& error){
    f={};f.key=key;
    auto* a=actorWorld_.actor(key.id);
    if(!a||!a->registered||a->isDisposed||a->entityHandle!=key.lifetime||!a->object){error.clear();return true;} // present=false
    const auto& o=*a->object;
    f.present=true;f.hasBody=true; // A3
    f.typeName=o.name;
    if(!actorWorld_.location(key.id,f.x,f.y,error)||!actorWorld_.zLocation(key.id,f.z,error))return false;
    f.angle=o.angle;
    f.hasPhysics=bool(a->physics);f.upwardVelocity=a->physics?a->physics->upVelocity:0;
    f.alpha=uint8_t(std::clamp(std::lround(o.alpha*255.f),0L,255L));
    f.bodyFlips=o.flipHorizontal?1:0;f.primaryFlipped=false; // A3
    if(a->attachedParent){const auto* p=actorWorld_.actor(a->attachedParent);f.attachedToLiveEntity=p&&p->registered&&!p->isDisposed&&p->entityHandle==a->attachedParentLifetime&&p->lifeStatus==0;}
    // GroupManager lookup of the first GroupName (type name if none).
    const std::string group=!o.groups.empty()?o.groups.front():(!o.group.empty()?o.group:o.name);
    f.firstGroupVisible=false;
    for(const auto& g:snapshotSceneGroups(state_))if(g.name==group){f.firstGroupVisible=g.visible;break;}
    if(a->geometryAvailable&&!a->geometry.empty()){ // A2
        float x0=a->geometry[0].x,x1=x0,y0=a->geometry[0].y,y1=y0;
        for(const auto& p:a->geometry){x0=std::min(x0,p.x);x1=std::max(x1,p.x);y0=std::min(y0,p.y);y1=std::max(y1,p.y);}
        f.area={int32_t(x0),int32_t(y0),int32_t(x1-x0),int32_t(y1-y0)};
    }
    if(a->hasText){error="Interact UseTextBoxes owner text-box bounds not reconstructed: "+o.name;return false;}
    error.clear();return true;
}

bool ScriptRuntime::interactionUserFrame(hades::InteractionKey key,hades::InteractionUserFrame& u,std::string& error){
    u={};u.key=key;interactUserPrimaryUnknown_=false;
    auto* a=actorWorld_.actor(key.id);
    if(!a||!a->registered||a->entityHandle!=key.lifetime||!a->object){u.disposed=true;error.clear();return true;}
    u.alive=a->lifeStatus==0&&a->object->alive;u.disposed=a->isDisposed;
    if(!actorWorld_.location(key.id,u.x,u.y,error)||!actorWorld_.zLocation(key.id,u.totalZ,error))return false;
    const auto* maxUseZ=actorWorld_.currentProperty(key.id,"MaxUseZ");
    if(!maxUseZ){error="Unit MaxUseZ unavailable for Interact user";return false;}
    u.maxUseZ=float(maxUseZ->number());u.useReach=0; // A1
    u.hasPhysics=bool(a->physics);u.playerControlled=a->object->playerControlled;u.playerIndex=0;
    u.hasPrimary=a->animation.active();
    if(u.hasPrimary){
        if(!animationBank_){error="Interact user primary animation bank unavailable";return false;}
        const auto disable=animationBank_->exactDisableOwnerInteract(a->animation.clipIndex());
        // Unknown (pre-HAP4 asset) only fails once a use target is actually in play (advancePlayerUse).
        interactUserPrimaryUnknown_=!disable;
        u.disableOwnerInteract=disable.value_or(false);
        u.disableOwnerManualInteract=a->animation.clip()&&(a->animation.clip()->flags&hades::ActorDisableManualInteract);
    }
    bool airborne=false;if(a->arsenal&&!actorWorld_.unitAirborne(key.id,airborne,error))return false;u.airborneAny=airborne;
    error.clear();return true;
}

hades::InteractionProviders ScriptRuntime::interactionProviders(){
    hades::InteractionProviders p;
    p.owner=[this](hades::InteractionKey k,hades::InteractionOwnerFrame& f,std::string& e){return interactionOwnerFrame(k,f,e);};
    // hasAttached / weaponAmmo / touches stay empty: the library fails loudly
    // (RequiresAttached, RequiresMissingAmmoWeapon, RequiresTouch) when used.
    return p;
}

bool ScriptRuntime::consumeInteractionOperation(const hades::InteractionUseOperation& op,std::string& error){
    using K=hades::InteractionUseKind;
    const auto text=[&]{return std::holds_alternative<std::string>(op.value.value)?op.value.text():std::string();};
    const auto number=[&]{return std::holds_alternative<double>(op.value.value)?op.value.number():0.0;};
    switch(op.kind){
    case K::UserAnimation:{const auto n=text();if(n.empty())break;if(!setActorAnimation(op.user.id,n,error))return false;break;}
    case K::OwnerAnimation:{const auto n=text();if(n.empty())break;if(!setActorAnimation(op.owner.id,n,error))return false;break;}
    case K::UserPlanarForce:case K::UserUpwardForce:
        if(number()!=0){error="Interact UserVelocity/UserUpwardVelocity force not reconstructed";return false;}
        break; // zero-magnitude PhysicsComponent::ApplyForce/ApplyUpwardForce adds nothing
    case K::UserFaceTarget:{auto* a=actorWorld_.actor(op.user.id);if(!a){error="UserFaceTarget user unavailable";return false;}
        error="Interact UserFaceTarget (Unit::SetGoalAngle 0x2f85f0) not wired";return false;}
    case K::UserFx:case K::VisualFx:case K::UsedFrontFx:case K::UsedBackFx:case K::UnuseableAnimation:
        if(!text().empty()){error="Interact FX/animation operation not reconstructed: "+text();return false;}
        break; // AnimationManager::Create with an empty name creates nothing
    case K::OnHeal:
        if(number()>0){error="Interact HealAmount (ScriptManager::OnHealed 0x289550) not wired";return false;}
        break; // OnHealed returns when amount <= 0
    case K::AttachOwnerToUser:error="Interact AttachOnUse not reconstructed";return false;
    case K::OnUsed:{
        const int32_t user=op.user.id;const int32_t owner=op.owner.id;
        if(!dispatchThingCallbacks("OnUsed",owner,[user,owner](lua_State* L,std::string&){
            if(user>=0){lua_pushinteger(L,user);lua_setfield(L,-2,"UserId");}
            pushObjectLuaTable(L,owner);lua_setfield(L,-2,"AttachedTable");return true;})){error=error_;return false;}
        break;}
    case K::UseSound:{
        const auto& v=op.value;
        if(std::holds_alternative<std::string>(v.value)&&!v.text().empty()){error="Interact UseSound cue not wired: "+v.text();return false;}
        if(const auto* valid=v.get("valid");valid&&std::holds_alternative<bool>(valid->value)&&valid->boolean()){error="Interact UseSound cue not wired";return false;}
        break;}
    case K::DestroyOwner:error="Interact DestroyOnUse not reconstructed";return false;
    case K::UserFireWeapon:error="Interact UserFireWeapon not reconstructed";return false;
    }
    error.clear();return true;
}

bool ScriptRuntime::useInteraction(hades::InteractionKey owner,hades::InteractionKey user,std::string& error){
    hades::log(hades::Severity::Info,tag,"NATIVE_INTERACT_USE owner=%d user=%d frame=%llu",owner.id,user.id,(unsigned long long)worldFrame_);
    hades::InteractionUseExecution use;
    if(!use.begin(interactions_,owner,user,{worldTime_,int32_t(worldFrame_)},error))return false;
    const auto providers=interactionProviders();
    const auto userFrame=[this](hades::InteractionKey k,hades::InteractionUserFrame& u,std::string& e){return interactionUserFrame(k,u,e);};
    for(int guard=0;guard<64;++guard){
        hades::InteractionUseOperation op;bool done=false;
        if(!use.next(providers,userFrame,op,done,error))return false;
        if(done){error.clear();return true;}
        if(!consumeInteractionOperation(op,error))return false;
        if(!use.acknowledge(error))return false;
    }
    error="Interact::Use did not finish";return false;
}

bool ScriptRuntime::advancePlayerUse(std::string& error){
    if(!interactionGridReady_){interactions_.configureSparseGrid();interactionGridReady_=true;}
    const auto providers=interactionProviders();
    if(!interactions_.update(providers,error))return false;
    const bool useDown=std::binary_search(localActionDown_.begin(),localActionDown_.end(),std::string("Use"));
    const bool usePressed=useDown&&!previousUseDown_;previousUseDown_=useDown;
    const hades::InteractionClock clock{worldTime_,int32_t(worldFrame_)};
    for(const auto& p:snapshotInputPlayers(state_)){
        if(p.playerIndex!=1)continue;
        auto* a=actorWorld_.actor(p.activeUnitId);
        hades::InteractionKey& current=useTargets_[p.playerIndex];
        const auto lose=[&](std::string& e)->bool{
            if(current.id>=0){const auto old=current;current={};
                hades::InteractionSnapshot s;const bool known=interactions_.snapshot(old,s,e);
                const bool autoActivate=known&&interactBool(s.definition,"AutoActivate");
                const int32_t user=a?a->id:-1;
                if(findObjectSnapshot(state_,old.id)&&!dispatchThingCallbacks("OnActiveUseTargetLost",old.id,[user,old,autoActivate](lua_State* L,std::string&){
                    if(user>=0){lua_pushinteger(L,user);lua_setfield(L,-2,"UserId");}
                    pushObjectLuaTable(L,old.id);lua_setfield(L,-2,"AttachedTable");lua_pushboolean(L,autoActivate);lua_setfield(L,-2,"AutoActivate");return true;})){e=error_;return false;}
            }
            e.clear();return true;};
        if(!a||!a->registered||a->entityHandle!=p.activeUnitLifetime||!a->object){if(!lose(error))return false;continue;}
        const hades::InteractionKey userKey{a->id,a->entityHandle};
        hades::InteractionUserFrame user;if(!interactionUserFrame(userKey,user,error))return false;
        const bool inputAllowed=p.inputAllowed&&a->allowInput;
        hades::InteractionKey selected;
        if(!interactions_.selectNormal(user,clock,false,providers,selected,error))return false;
        if(selected.id>=0&&interactUserPrimaryUnknown_){error="Interact user primary DisableOwnerInteract not recovered (stage hero-properties HAP4)";return false;}
        hades::InteractionSnapshot snap;
        if(selected.id<0||!interactions_.snapshot(selected,snap,error)){if(!lose(error))return false;continue;}
        const auto& def=snap.definition;
        if(!inputAllowed&&!interactBool(def,"UseableWhileInputDisabled")){if(!lose(error))return false;continue;}
        hades::InteractionOwnerFrame owner;if(!interactionOwnerFrame(selected,owner,error))return false;
        bool useable=false;if(!interactions_.isUseableBy(selected,owner,user,clock,false,providers,useable,error))return false;
        const bool autoActivate=interactBool(def,"AutoActivate");
        if(!(current==selected)){
            if(!lose(error))return false;
            const int32_t uid=a->id;const int32_t sid=selected.id;
            hades::log(hades::Severity::Info,tag,"NATIVE_USE_TARGET user=%d target=%d name=%s useable=%d",uid,sid,owner.typeName.c_str(),int(useable));
            if(!dispatchThingCallbacks("OnActiveUseTarget",sid,[uid,sid,autoActivate](lua_State* L,std::string&){
                lua_pushinteger(L,uid);lua_setfield(L,-2,"UserId");
                pushObjectLuaTable(L,sid);lua_setfield(L,-2,"AttachedTable");lua_pushboolean(L,autoActivate);lua_setfield(L,-2,"AutoActivate");return true;})){error=error_;return false;}
            if(!useable){
                const auto failed=interactText(def,"AutoUseFailedAnimation");if(!failed.empty()&&!setActorAnimation(sid,failed,error))return false;
                if(!dispatchThingCallbacks("OnAutoUseFailed",sid,[uid](lua_State* L,std::string&){lua_pushinteger(L,uid);lua_setfield(L,-2,"UserId");return true;})){error=error_;return false;}
            }
        }
        current=selected;
        if(!useable)continue;
        const bool automatic=autoActivate||interactBool(def,"AutoActivateWithGamepad"); // A5 useMouse=false
        if(automatic){
            const float autoDistance=interactNumber(def,"AutoUseDistance");bool inRange=true;
            if(autoDistance>0){float x=0,y=0;if(!interactions_.useLocation(selected,owner,x,y,error))return false;inRange=(user.x-x)*(user.x-x)+(user.y-y)*(user.y-y)<=autoDistance*autoDistance;}
            if(inRange){if(!useInteraction(selected,userKey,error))return false;continue;}
        }
        const auto highlight=interactText(def,"CurrentUseTargetAnimation");
        if(!highlight.empty()&&!setActorAnimation(selected.id,highlight,error))return false;
        // PlayerUnitInput::HandleInput manual-Use branch (Control.Use pressed flag 0x4).
        if(usePressed&&inputAllowed){
            bool accepted=false;if(!interactions_.manualRequest(selected,user,false,4u,accepted,error))return false;
            if(accepted&&!useInteraction(selected,userKey,error))return false;
        }
    }
    error.clear();return true;
}
