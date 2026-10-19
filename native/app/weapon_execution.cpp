#include "weapon_execution.hpp"
#include <algorithm>
#include <cmath>
#include <utility>
#include <limits>

namespace hades {
WeaponExecution::WeaponExecution(WeaponArsenal& arsenal,WeaponExecutionRequest request)
    :arsenal_(&arsenal),request_(std::move(request)){}
bool WeaponExecution::advance(const WeaponOwnerFrame& owner,const WeaponOutputSupport& support,
                             WeaponExecutionStep& output,std::string& error){
    // Native Weapon::Charge RVA87cd0, REA ev_fd634f86d733699906e102efacfc3053a26b4d809a2194c032f83de7cf4ec6d6.
    error.clear();WeaponExecutionStep next;
    if(owner.ownerId!=arsenal_->ownerId_){error="Weapon execution owner mismatch";return false;}
    if(!owner.exists){next.progress=WeaponExecutionProgress::OwnerMissing;output=next;finished_=true;return true;}
    if(finished_){error="Weapon charge cursor already finished";return false;}
    if(!std::isfinite(request_.dt)||request_.dt<0||!std::isfinite(request_.angle)){error="Invalid native weapon charge request";return false;}
    WeaponSnapshot* w=nullptr;
    for(auto& candidate:arsenal_->weapons_)if(candidate.definition.name==request_.weaponName){w=&candidate;break;}
    if(!w){error="Charge cursor weapon no longer owned";return false;}
    const auto& d=w->definition;
    auto number=[&](const char* key,float& out){auto f=d.fields.find(key);if(f==d.fields.end()||!std::holds_alternative<double>(f->second)){error=std::string("Unrecovered charge numeric field ")+key;return false;}out=static_cast<float>(std::get<double>(f->second));return std::isfinite(out);};
    auto flag=[&](const char* key,bool& out){auto f=d.fields.find(key);if(f==d.fields.end()||!std::holds_alternative<bool>(f->second)){error=std::string("Unrecovered charge flag ")+key;return false;}out=std::get<bool>(f->second);return true;};
    auto text=[&](const char* key,std::string& out){auto f=d.fields.find(key);if(f==d.fields.end()||!std::holds_alternative<std::string>(f->second)){error=std::string("Unrecovered charge text ")+key;return false;}out=std::get<std::string>(f->second);return true;};
    auto yield=[&](WeaponOutputKind kind,const std::string& name,float value=0,float x=0)->bool{
        WeaponOutput op;op.kind=kind;op.ownerId=owner.ownerId;op.weaponName=request_.weaponName;op.name=name;op.value=value;op.angle=request_.angle;op.x=x;op.chargeFxRole=chargeFxRole_;chargeFxRole_=0;
        if(!support){error="Charge operation has no synchronous consumer: "+name;return false;}
        if(!support(op,error)){if(error.empty())error="Charge operation unsupported: "+name;return false;}
        if(!chargeTimerCommitted_){w->chargeTimeRemaining=pendingRemaining_;chargeTimerCommitted_=true;}
        next.progress=WeaponExecutionProgress::Yielded;next.operation=std::move(op);output=std::move(next);++phase_;return true;
    };
    // Every skipped branch is checked against actual current source data. This
    // bounded path excludes FX lifetime/speed and nonzero perfect-charge state
    // until those real animation components are represented by the parent.
    while(phase_<19){
        bool b=false;float f=0;std::string s;
        switch(phase_){
        case 0:{
            std::string gateError;
            if(!arsenal_->canFire(request_.weaponName,owner,gateError)){
                if(!gateError.empty()){error=gateError;return false;}
                next.progress=WeaponExecutionProgress::Blocked;output=next;finished_=true;return true;
            }
            if(d.chargeTime<=0){next.progress=WeaponExecutionProgress::ChargeFinished;next.chargeReady=true;output=next;finished_=true;return true;}
            for(const auto* key:{"ChargeRumbleLeft","ChargeRumbleRight","PerfectChargeWindowDuration"}){
                if(!number(key,f))return false;
                if(f!=0){error=std::string("Native charge requires unimplemented nonzero ")+key;return false;}
            }
            // ChargeStartFx/ChargeFx are yielded with their role (phases 4/5); the
            // parent resolves null lookups and fails loudly on a live speed/duration need.
            for(const auto* key:{"MinChargeStartFx"}){
                if(!text(key,s))return false;
                if(!s.empty()&&s!="null"){error=std::string("Charge attached FX requires duration/speed component: ")+s;return false;}
            }
            for(const auto* key:{"ChargeSound","OnWeaponChargingSound","ChargeFullSound","ChargeFinishSound"}){
                auto cue=d.cues.find(key);
                if(cue==d.cues.end()||!cue->second.validityKnown||cue->second.valid){error=std::string("Charge audio validity/playback unresolved: ")+key;return false;}
            }
            oldFraction_=std::clamp(1.0f-w->chargeTimeRemaining/d.chargeTime,0.0f,1.0f);
            const float remaining=w->chargeTimeRemaining-request_.dt;pendingRemaining_=remaining;
            newFraction_=std::clamp(1.0f-remaining/d.chargeTime,0.0f,1.0f);
            startedPartial_=newFraction_>0.0001f&&newFraction_<1.0f&&oldFraction_<=0;
            // Pure timer mutation precedes native synchronous callbacks. All
            // unsupported initial-source branches above were checked first.
            ++phase_;continue;
        }
        case 1:
            if(!flag("SetCompleteAngleOnCharge",b))return false;
            if(request_.hasCompleteAngle&&b)return yield(WeaponOutputKind::SetOwnerCompleteAngle,"complete-angle");
            ++phase_;continue;
        case 2:
            if(startedPartial_){if(!flag("ChargeCancelMovement",b))return false;if(b)return yield(WeaponOutputKind::CancelOwnerMovement,"charge");}
            ++phase_;continue;
        case 3:
            if(startedPartial_){if(!text("ChargeStartAnimation",s))return false;return yield(WeaponOutputKind::SetOwnerAnimation,s);}
            ++phase_;continue;
        case 4:
            // Zero/"null" attached FX are still actual lookup attempts. Parent
            // must resolve their absence from the source animation namespace.
            if(startedPartial_){if(!text("ChargeStartFx",s))return false;if(!s.empty()){chargeFxRole_=1;return yield(WeaponOutputKind::CreateAttachedAnimation,s,d.chargeTime,w->chargeTimeRemaining);}}
            ++phase_;continue;
        case 5:
            if(startedPartial_){if(!text("ChargeFx",s))return false;if(!s.empty()){chargeFxRole_=2;return yield(WeaponOutputKind::CreateAttachedAnimation,s,0,w->chargeTimeRemaining);}}
            ++phase_;continue;
        case 6:
            if(startedPartial_)return yield(WeaponOutputKind::ScriptCallback,"OnWeaponCharging",newFraction_);
            ++phase_;continue;
        case 7:
            if(startedPartial_){if(!number("OnWeaponChargingSoundChance",f))return false;return yield(WeaponOutputKind::ConsumeRandomChance,"OnWeaponChargingSoundChance",f);}
            ++phase_;continue;
        case 8:
            if(startedPartial_){if(!flag("CanCancelDisables",b))return false;if(b)return yield(WeaponOutputKind::CancelOwnerDisables,"charge");}
            ++phase_;continue;
        case 9:
            if(startedPartial_){if(!flag("CanCancelDisables",b))return false;if(!b){if(!flag("CanCancelRotateDisables",b))return false;if(b)return yield(WeaponOutputKind::CancelOwnerRotateDisables,"charge");}}
            ++phase_;continue;
        case 10:
            if(startedPartial_){if(!flag("CancelRamWeaponOnCharge",b))return false;if(b){error="Native charge Ram cancellation not implemented";return false;}}
            ++phase_;continue;
        case 11:case 12:case 13:case 14:case 15:
            if(startedPartial_){const char* keys[]{"RemoveControlOnCharge","RemoveControlOnCharge2","RemoveControlOnCharge3","RemoveControlOnCharge4","RemoveControlOnCharge5"};if(!text(keys[phase_-11],s))return false;if(!s.empty())return yield(WeaponOutputKind::RemoveWeaponControl,s);}
            ++phase_;continue;
        case 16:
            if(newFraction_>0.0001f)return yield(WeaponOutputKind::ApplySelfEffects,"Charging",3);
            ++phase_;continue;
        case 17:
            if(oldFraction_<1&&newFraction_>=1){if(!text("ChargeFinishFx",s))return false;if(!s.empty())return yield(WeaponOutputKind::CreateAttachedAnimation,s);}
            ++phase_;continue;
        case 18:
            if(!number("MinChargeToFire",f))return false;
            if(!chargeTimerCommitted_){w->chargeTimeRemaining=pendingRemaining_;chargeTimerCommitted_=true;}
            if(oldFraction_>=1||newFraction_<1)w->perfectChargeWindowRemaining-=request_.dt;
            w->charging=true;next.progress=WeaponExecutionProgress::ChargeFinished;next.chargeReady=f<=newFraction_;output=next;finished_=true;return true;
        }
    }
    error="Invalid native charge cursor phase";return false;
}
}

namespace hades {
WeaponFirePrefix::WeaponFirePrefix(WeaponArsenal& arsenal,std::string name,WeaponFireRequest request)
    :arsenal_(&arsenal),name_(std::move(name)),request_(request){}
bool WeaponFirePrefix::advance(const WeaponOwnerFrame& owner,const WeaponOutputSupport& support,
                              WeaponFirePrefixStep& output,std::string& error){
    return advance(owner,WeaponBlinkReply{},support,output,error);
}
bool WeaponFirePrefix::advance(const WeaponOwnerFrame& owner,const WeaponBlinkReply& reply,const WeaponOutputSupport& support,
                              WeaponFirePrefixStep& output,std::string& error){
    // HandleTriggerDown RVA863f0 -> Gun::Fire RVA7fb20 -> Weapon::Fire RVA88f00.
    // Native Fire's first animation is PreFireAnimation. FireGraphic belongs
    // to HandleTriggerDown BEFORE the virtual Fire call for GUN weapons.
    error.clear();if(owner.ownerId!=arsenal_->ownerId_){error="Fire prefix owner mismatch";return false;}
    if(finished_){error="Fire prefix already finished";return false;}
    if(!owner.exists){output.progress=WeaponFirePrefixProgress::OwnerMissing;finished_=true;return true;}
    WeaponSnapshot* w=nullptr;for(auto& candidate:arsenal_->weapons_)if(candidate.definition.name==name_){w=&candidate;break;}
    if(!w){error="Fire prefix weapon is not owned";return false;}
    const auto& d=w->definition;
    auto flag=[&](const char* key,bool& b){auto i=d.fields.find(key);if(i==d.fields.end()||!std::holds_alternative<bool>(i->second)){error=std::string("Unrecovered Fire flag ")+key;return false;}b=std::get<bool>(i->second);return true;};
    auto text=[&](const char* key,std::string& s){auto i=d.fields.find(key);if(i==d.fields.end()||!std::holds_alternative<std::string>(i->second)){error=std::string("Unrecovered Fire text ")+key;return false;}s=std::get<std::string>(i->second);return true;};
    auto yield=[&](WeaponOutputKind kind,const std::string& name,float value=0)->bool{
        WeaponOutput op;op.kind=kind;op.ownerId=owner.ownerId;op.weaponName=name_;op.name=name;op.value=value;op.angle=request_.angle;op.x=origin_.x;op.y=origin_.y;op.z=owner.z;
        if(kind==WeaponOutputKind::ApplyOwnerPhysics)op.angle=owner.angle; // common Fire uses owner's current body angle
        if(!support){error="Common Fire requires synchronous consumer: "+name;return false;}
        if(!support(op,error)){if(error.empty())error="Unsupported common Fire operation: "+name;return false;}
        if(phase_>=3&&phase_<20&&!lastFireCommitted_){w->lastFireTime=owner.worldTime;lastFireCommitted_=true;}
        WeaponFirePrefixStep next;next.operation=std::move(op);output=std::move(next);++phase_;return true;
    };
    // Replies belong only to the BlinkWeapon::Fire pre-step children.
    if(!blinkProjectile_&&!blinkAwait_&&(reply.randomDouble||reply.createdProjectile||reply.fireContinues)){error="Unexpected common Fire reply";return false;}
    while(phase_<11||(phase_>=20&&phase_<24)){
        bool b=false;std::string s;
        switch(phase_){
        case 0:
            // Virtual Fire dispatch: GUN (type 0) GunWeapon::Fire7fb20 and BLINK (type 1)
            // BlinkWeapon::Fire7d7d0 both reach common Weapon::Fire88f00.
            if(d.type!=0&&d.type!=1){error="Common Fire prefix requires native GUN/BLINK subclass";return false;}
            if(!std::isfinite(owner.worldTime)||!std::isfinite(owner.angle)||!std::isfinite(request_.angle)){error="Nonfinite common Fire frame";return false;}
            if(request_.targetId!=-1||request_.fromX!=0||request_.fromY!=0){error="Common Fire explicit entity/from-location branch not recovered";return false;}
            if(!flag("FireOnRelease",b))return false;if(b){error="Trigger-release path required before common Fire";return false;}
            if(!flag("RandomFireAngle",b))return false;if(b){error="RandomFireAngle needs synchronous random result continuation";return false;}
            if(!flag("SpendAmmoPerProjectile",b))return false;if(b){error="Ammo-per-projectile path belongs to Gun continuation";return false;}
            if(!d.shareAmmoWeapon.empty()){error="Recursive shared-ammo callbacks require a separate resumable depletion path";return false;}
            for(const auto* key:{"FireFromObstacle","FireToObstacle"}){if(!text(key,s))return false;if(!s.empty()){error="Common Fire actual targeted obstacle destruction/search required";return false;}}
            {auto cue=d.cues.find("FireSound");if(cue==d.cues.end()||!cue->second.validityKnown||cue->second.valid){error="Common Fire real audio cue unresolved";return false;}}
            // HandleTriggerDown VA180086480: FireGraphic only when WeaponData mType != BLINK.
            if(d.type==1){phase_=20;continue;}
            ++phase_;continue;
        case 20:
            // BlinkWeapon::Fire VA18007d81f..869 (native-combat4-blink-fire-pseudocode.c):
            // SetCompleteAngleOnFire && !Arsenal::IsStrafing -> Thing::SetAngle (vslot 0x90),
            // Unit::SetGoalAngle2f85f0, CosmeticAngle/Target = angle (Unit.SetCompleteAngle consumer).
            if(!flag("SetCompleteAngleOnFire",b))return false;
            if(b){const bool strafing=arsenal_->isStrafing(name_,error);if(!error.empty())return false;if(!strafing)return yield(WeaponOutputKind::SetOwnerCompleteAngle,"blink-fire");}
            ++phase_;continue;
        case 21:
            // BlinkDetonateAtOrigin (+0x920): FireProjectile7e4f0(owner GetLocation, angle, request).
            if(!blinkProjectile_){
                if(!flag("BlinkDetonateAtOrigin",b))return false;
                if(!b){phase_=23;continue;}
                blinkProjectile_=std::make_unique<WeaponBlinkProjectile>(*arsenal_,name_,owner.x,owner.y,request_.angle,request_);
            }
            ++phase_;continue;
        case 22:{
            WeaponFirePrefixStep next;bool done=false;
            if(!blinkProjectile_->advance(owner,reply,support,next.operation,done,next.expectsRandomReply,next.expectsProjectileReply,error))return false;
            if(done){blinkProjectile_.reset();++phase_;continue;}
            output=std::move(next);return true;
        }
        case 23:{
            if(!blinkAwait_){
                // GetTeleportTarget/IsLocationValid/CalcValidLocationOnVector/ApplyFullShift/
                // camera lerp/UnblockedBlinkFx and the member stores (VA18007d8e2..7dec1)
                // are one World-side consumer with no Lua callback between them.
                WeaponOutput op;op.kind=WeaponOutputKind::BlinkTeleport;op.ownerId=owner.ownerId;op.weaponName=name_;op.name="BlinkWeapon::Fire teleport";
                op.angle=request_.angle;op.fireRequest=request_;
                if(!support||!support(op,error)){if(error.empty())error="BlinkWeapon::Fire teleport unsupported";return false;}
                WeaponFirePrefixStep next;next.operation=std::move(op);next.expectsBlinkReply=true;output=std::move(next);blinkAwait_=true;return true;
            }
            if(!reply.fireContinues||reply.randomDouble||reply.createdProjectile){error="BlinkWeapon::Fire awaits teleport consumer result";return false;}
            blinkAwait_=false;
            // FireResult 0 (VA18007dafd) skips Weapon::Fire88f00 entirely.
            if(!*reply.fireContinues){output=WeaponFirePrefixStep{};output.progress=WeaponFirePrefixProgress::CommonFireFinished;finished_=true;return true;}
            phase_=2;continue;
        }
        case 1:
            if(!text("FireGraphic",s))return false;if(!s.empty())return yield(WeaponOutputKind::SetOwnerAnimation,s);
            ++phase_;continue;
        case 2:
            if(!arsenal_->fireOrigin(name_,owner,request_.angle,origin_,error))return false;
            if(!text("PreFireAnimation",s))return false;if(!s.empty())return yield(WeaponOutputKind::SetOwnerAnimation,s);
            ++phase_;continue;
        case 3:
            if(request_.applySelfEffects)return yield(WeaponOutputKind::ApplySelfEffects,"Fire",1);
            ++phase_;continue;
        case 4:
            // Even zero source forces invoke native PhysicsComponent methods;
            // parent must provide real body/force semantics, never drop this.
            return yield(WeaponOutputKind::ApplyOwnerPhysics,"Weapon::ApplyPhysics");
        case 5:{
            if(!d.shareAmmoWeapon.empty()){error="Shared-ammo branch changed during synchronous Fire callback";return false;}
            std::vector<WeaponOutput> events;
            if(!arsenal_->spendAmmo(name_,events,support,error))return false;
            ++phase_;
            if(!events.empty()){
                if(events.size()!=1){error="Common Fire requires nested ammo callback continuation";return false;}
                WeaponFirePrefixStep next;next.operation=std::move(events.front());output=std::move(next);return true;
            }continue;
        }
        case 6:
            if(w->clipShotsRemaining==d.clipSize){if(!text("ClipStartFx",s))return false;if(!s.empty())return yield(WeaponOutputKind::CreateWeaponWorldAnimation,s);}
            ++phase_;continue;
        case 7:
            // FireSound is proven invalid for this bounded branch. It creates
            // no event ID. A property setter cannot silently enable playback.
            ++phase_;continue;
        case 8:
            if(!flag("CancelMovement",b))return false;if(b)return yield(WeaponOutputKind::CancelOwnerMovement,"fire");
            ++phase_;continue;
        case 9:
            if(!flag("SetCompleteAngleOnFire",b))return false;
            if(b){const bool strafing=arsenal_->isStrafing(name_,error);if(!error.empty())return false;if(!strafing)return yield(WeaponOutputKind::SetOwnerCompleteAngle,"fire");}
            ++phase_;continue;
        case 10:
            if(!lastFireCommitted_){w->lastFireTime=owner.worldTime;lastFireCommitted_=true;}
            output.progress=WeaponFirePrefixProgress::CommonFireFinished;finished_=true;return true;
        }
    }
    error="Invalid common Fire cursor phase";return false;
}
}

namespace hades {
WeaponGunExecution::WeaponGunExecution(WeaponArsenal& arsenal,std::string name,WeaponFireRequest request)
    :arsenal_(&arsenal),name_(std::move(name)),request_(request){}
bool WeaponGunExecution::advance(const WeaponOwnerFrame& owner,const WeaponGunReply& reply,
        const WeaponOutputSupport& support,WeaponGunStep& output,std::string& error){
    // GunWeapon::Fire RVA7fb20; retained original decompilation and matching
    // PDB-bounded instructions in player-firing-barriers-contract.json.
    error.clear();WeaponGunStep next;
    if(finished_){error="Native GUN continuation already finished";return false;}
    if(owner.ownerId!=arsenal_->ownerId_){error="Native GUN owner identity mismatch";return false;}
    if(!owner.exists){next.progress=WeaponGunProgress::OwnerMissing;output=next;finished_=true;return true;}
    WeaponSnapshot* w=nullptr;for(auto& candidate:arsenal_->weapons_)if(candidate.definition.name==name_){w=&candidate;break;}
    if(!w||w->definition.type!=0){error="Native GUN continuation lacks actual owned GUN";return false;}
    if(!std::isfinite(request_.angle)||request_.targetId!=-1||request_.fromX!=0||request_.fromY!=0){error="Native GUN continuation target/from-location branch not reconstructed";return false;}
    auto number=[&](const auto& fields,const char* key,float& value){auto f=fields.find(key);
        if(f==fields.end()||!std::holds_alternative<double>(f->second)){error=std::string("Missing native GUN number ")+key;return false;}
        value=static_cast<float>(std::get<double>(f->second));if(!std::isfinite(value)){error="Nonfinite native GUN data";return false;}return true;};
    auto flag=[&](const auto& fields,const char* key,bool& value){auto f=fields.find(key);
        if(f==fields.end()||!std::holds_alternative<bool>(f->second)){error=std::string("Missing native GUN flag ")+key;return false;}
        value=std::get<bool>(f->second);return true;};
    auto emit=[&](WeaponOutput op){
        op.ownerId=owner.ownerId;op.weaponName=name_;
        if(!support){error="Native GUN operation has no synchronous consumer";return false;}
        if(!support(op,error)){if(error.empty())error="Unsupported native GUN operation "+op.name;return false;}
        next.progress=WeaponGunProgress::Yielded;next.operation=std::move(op);output=std::move(next);++phase_;return true;
    };
    auto commitVolley=[&](){if(!volleyCommitted_){w->volley=pendingVolley_;const unsigned slot=static_cast<unsigned>(w->volley)%4;
        w->volleyHitTimes[slot].clear();w->volleyFirstHitId[slot]=-1;w->volleySlotInitialized[slot]=true;volleyCommitted_=true;}};
    constexpr float tau=6.283185482025146484375f;
    if(await_==Await::None){if(reply.randomDouble||reply.createdProjectile){error="Unrequested native GUN reply";return false;}}
    else if(await_==Await::Projectile){
        if(!reply.createdProjectile||!*reply.createdProjectile||reply.randomDouble){error="Native GUN awaits actual completed projectile creation handle";return false;}
        createdProjectile_=*reply.createdProjectile;await_=Await::None;
    }else{
        if(!reply.randomDouble||reply.createdProjectile||!std::isfinite(*reply.randomDouble)||*reply.randomDouble<0||*reply.randomDouble>=1){error="Native GUN awaits one valid synchronous RandomDouble result";return false;}
        if(await_==Await::SpreadRandom){
            waveAngle_=(static_cast<float>(*reply.randomDouble)*(spreadHalf_-(-spreadHalf_))+(-spreadHalf_))+capturedAngle_;
            waveAngle_-=std::floor(waveAngle_/tau)*tau;
        }else projectileCount_=baseCount_+(*reply.randomDouble<=static_cast<double>(bonusChance_)?1:0);
        await_=Await::None;
    }
    while(phase_<12){
        auto& d=w->definition;float f=0,g=0;bool b=false;
        switch(phase_){
        case 0:
            if(!w->validProjectileData){next.progress=WeaponGunProgress::GunBodyFinished;next.nativeVirtualFireResult=true;output=next;finished_=true;return true;}
            // Recomputed after common Fire and its synchronous Lua/physics.
            if(!arsenal_->fireOrigin(name_,owner,request_.angle,origin_,error))return false;
            if(!number(d.fields,"ActiveProjectileCap",f)||!flag(d.fields,"RefreshProjectileOnFire",b))return false;
            if(f>0||b){error="Native GUN cap/refresh path requires actual projectile-manager query";return false;}
            if(w->volley==INT32_MAX){error="Native GUN volley counter overflow outside portable subset";return false;}
            pendingVolley_=w->volley+1;
            ++phase_;continue;
        case 1:
            if(!number(d.fields,"NumProjectileWaves",f))return false;
            if(f<0||f>128||std::floor(f)!=f){error="Native GUN wave count outside bounded implementation";return false;}
            if(wave_>=static_cast<int32_t>(f)){commitVolley();next.progress=WeaponGunProgress::GunBodyFinished;next.nativeVirtualFireResult=true;output=next;finished_=true;return true;}
            if(!flag(d.fields,"RandomFireAngle",b))return false;
            if(b){error="Native GUN random-full-angle branch not reconstructed by this cursor";return false;}
            if(!number(d.fields,"Spread",f)||!number(d.fields,"FullChargeSpread",g))return false;
            if(f!=0||g!=0){error="Native GUN nonzero source spread needs PostRead angle normalization provenance";return false;}
            spreadHalf_=(f+w->scatter)*0.5f;capturedAngle_=request_.angle;
            {WeaponOutput op;op.kind=WeaponOutputKind::RequestRandomDouble;op.name="Gun wave spread";
                if(!emit(std::move(op)))return false;commitVolley();await_=Await::SpreadRandom;return true;}
        case 2:
            if(!number(w->projectileData.fields,"InvulnerableImpactOwnerVelocity",f)||!flag(w->projectileData.fields,"CheckObstacleImpact",b))return false;
            if(f>0&&b){
                if(!number(d.fields,"InvulnerableImpactCheckOffset",g))return false;
                WeaponOutput op;op.kind=WeaponOutputKind::CheckInvulnerableBarrelImpact;op.name="native barrel obstacle query/force";
                op.projectile=w->projectileSource;op.angle=waveAngle_;op.value=f;op.x=origin_.x;op.y=origin_.y;op.z=owner.z;op.waveIndex=wave_;
                if(g>0){op.x+=std::cos(waveAngle_)*g;op.y-=std::sin(waveAngle_)*g;}
                return emit(std::move(op));
            }
            ++phase_;continue;
        case 3:
            if(!flag(d.fields,"NumProjectilesIsAmmo",b)||!number(d.fields,"NumProjectiles",f)||!number(d.fields,"BonusProjectileChance",g))return false;
            if(b||f<0||f>4096||std::floor(f)!=f){error="Native GUN ammo-derived or excessive projectile count unsupported";return false;}
            baseCount_=static_cast<int32_t>(f);bonusChance_=g;
            {WeaponOutput op;op.kind=WeaponOutputKind::RequestRandomDouble;op.name="Gun bonus projectile chance";
                if(!emit(std::move(op)))return false;await_=Await::BonusRandom;return true;}
        case 4:
            index_=0;++phase_;continue;
        case 5:{
            if(index_>=projectileCount_){++wave_;phase_=1;continue;}
            // Genuine source default sword/cast geometry. Other fields remain
            // checked fresh after each callback rather than silently ignored.
            for(const char* key:{"ProjectileAngleOffset","ProjectileAngleOffsetMin","ProjectileAngleOffsetMax","ProjectileSpacing","ProjectileInterval","ProjectileWaveInterval","ProjectileAngleOffsetScaleY"}){
                if(!number(d.fields,key,f))return false;if(f!=0){error=std::string("Native GUN unsupported projectile geometry/delay ")+key;return false;}
            }
            for(const char* key:{"SpendAmmoPerProjectile","OwnerFaceProjectile"}){if(!flag(d.fields,key,b))return false;if(b){error=std::string("Native GUN unsupported per-projectile side effect ")+key;return false;}}
            if(!flag(w->projectileData.fields,"ManuallySetTarget",b))return false;
            if(b){error="Native GUN manual target rewrite requires native target-link seam";return false;}
            if(!number(d.fields,"ProjectileAngleStartOffset",f))return false;
            WeaponOutput op;op.kind=WeaponOutputKind::ProjectileSpawn;op.name=w->projectileData.name;
            op.x=origin_.x;op.y=origin_.y;op.z=owner.z;op.angle=waveAngle_+f;op.projectile=w->projectileSource;
            op.fireRequest=request_;
            op.waveIndex=wave_;op.projectileIndex=index_+wave_*projectileCount_;op.volleyIndex=w->volley;
            op.value=d.chargeTime==0?1:std::clamp(1.0f-w->chargeTimeRemaining/d.chargeTime,0.0f,1.0f);
            if(!emit(std::move(op)))return false;
            const unsigned slot=static_cast<unsigned>(w->volley)%4;w->volleyHitTimes[slot].push_back(std::numeric_limits<float>::max());
            await_=Await::Projectile;return true;
        }
        case 6:{
            // Native reads these current fields AFTER manager Create/Init and
            // its callbacks. Never pre-plan this post-create adjustment.
            if(!number(d.fields,"ChargeRadiusMultiplier",f)||!number(d.fields,"PerfectChargeRadiusMultiplier",g))return false;
            if(f>0||g>0){error="Native GUN charge/perfect blast multiplier continuation not reconstructed";return false;}
            for(const char* key:{"ProjectileAngleOffsetScaleY","ProjectileInterval","ProjectileWaveInterval"}){
                if(!number(d.fields,key,f))return false;if(f!=0){error=std::string("Native GUN post-create mutation requires side-effect handler ")+key;return false;}
            }
            for(const char* key:{"SpendAmmoPerProjectile","OwnerFaceProjectile"}){if(!flag(d.fields,key,b))return false;if(b){error=std::string("Native GUN post-create mutation requires side-effect handler ")+key;return false;}}
            if(!number(w->projectileData.fields,"StartDelay",f))return false;
            WeaponOutput op;op.kind=WeaponOutputKind::FinalizeGunProjectile;op.name="native post-create wave/crit/damage/blast";
            op.projectileLifetimeHandle=createdProjectile_;op.waveIndex=wave_;op.projectileIndex=index_+wave_*projectileCount_;op.volleyIndex=w->volley;
            op.addCritBonus=w->critBonusRemaining>0;op.critBonus=w->critBonusChance;
            op.setAdditiveDamageMultiplier=w->additiveDamageBonusRemaining>0;op.additiveDamageMultiplier=w->additiveDamageMultiplier;
            op.blastRadiusMultiplier=w->blastRadiusModifier;
            op.delayedProjectile=f>0; // consumer links Weapon.pDelayedProjectile only for a Unit with pPlayer
            return emit(std::move(op));
        }
        case 7:
            ++index_;createdProjectile_=0;phase_=5;continue;
        default:error="Invalid native GUN cursor phase";return false;
        }
    }
    error="Invalid native GUN continuation";return false;
}
}

namespace hades {
WeaponFireEffects::WeaponFireEffects(WeaponArsenal& arsenal,std::string name,WeaponFireRequest request)
    :arsenal_(&arsenal),name_(std::move(name)),request_(request){}
bool WeaponFireEffects::advance(const WeaponOwnerFrame& owner,std::optional<uint64_t> animationReply,
        const WeaponOutputSupport& support,WeaponFireEffectsStep& output,std::string& error){
    // Native HandleTriggerDown VA1800864e1..86616, not a separate native method.
    // Fx local position is retained across both creates; radius data is read
    // AFTER each Create returns and may reflect real synchronous changes.
    error.clear();WeaponFireEffectsStep next;
    if(finished_){error="Native trigger FireFx continuation already finished";return false;}
    if(owner.ownerId!=arsenal_->ownerId_){error="Native trigger FireFx owner mismatch";return false;}
    if(!owner.exists){next.progress=WeaponFireEffectsProgress::OwnerMissing;output=next;finished_=true;return true;}
    WeaponSnapshot* w=nullptr;for(auto& value:arsenal_->weapons_)if(value.definition.name==name_){w=&value;break;}
    if(!w||(w->definition.type!=0&&w->definition.type!=1)){error="Native trigger FireFx lacks actual owned GUN/BLINK";return false;}
    if(waiting_){if(!animationReply){error="Native trigger FireFx awaits actual animation lookup/create result";return false;}animation_=*animationReply;waiting_=false;}
    else if(animationReply){error="Unrequested native trigger FireFx creation reply";return false;}
    auto number=[&](const auto& fields,const char* key,float& value){auto f=fields.find(key);
        if(f==fields.end()||!std::holds_alternative<double>(f->second)){error=std::string("Missing trigger FireFx number ")+key;return false;}
        value=static_cast<float>(std::get<double>(f->second));if(!std::isfinite(value)){error="Nonfinite trigger FireFx data";return false;}return true;};
    auto emit=[&](WeaponOutput op){op.ownerId=owner.ownerId;op.weaponName=name_;op.x=origin_.x;op.y=origin_.y;
        if(!support){error="Native trigger FireFx has no synchronous consumer";return false;}
        if(!support(op,error)){if(error.empty())error="Native trigger FireFx operation unsupported";return false;}
        next.operation=std::move(op);next.progress=WeaponFireEffectsProgress::Yielded;output=std::move(next);++phase_;return true;};
    while(phase_<6){
        switch(phase_){
        case 0:{
            if(!arsenal_->fireOrigin(name_,owner,request_.angle,origin_,error))return false;
            float offset=0;if(!number(w->definition.fields,"FireFxOffset",offset))return false;
            if(offset>0){origin_.x+=std::cos(request_.angle)*offset;origin_.y-=std::sin(request_.angle)*offset;}
            ++phase_;continue;
        }
        case 1:case 3:{
            const char* key=phase_==1?"FireFx":"FireFx2";auto f=w->definition.fields.find(key);
            if(f==w->definition.fields.end()||!std::holds_alternative<std::string>(f->second)){error=std::string("Missing native trigger animation field ")+key;return false;}
            WeaponOutput op;op.kind=WeaponOutputKind::CreateWeaponWorldAnimation;op.name=std::get<std::string>(f->second);
            if(!emit(std::move(op)))return false;waiting_=true;return true;
        }
        case 2:case 4:
            if(animation_&&w->validProjectileData){
                float radius=0,scale=0;
                if(!number(w->projectileData.fields,"DamageRadius",radius)||!number(w->projectileData.fields,"DamageRadiusScaleX",scale))return false;
                WeaponOutput op;op.kind=WeaponOutputKind::SetWeaponWorldAnimationRadius;op.name="native trigger FireFx radius";
                op.animationLifetimeHandle=animation_;op.value=(radius*scale)*w->blastRadiusModifier;
                return emit(std::move(op));
            }
            ++phase_;continue;
        case 5:next.progress=WeaponFireEffectsProgress::BeforePostFire;output=next;finished_=true;return true;
        }
    }
    error="Invalid native trigger FireFx continuation";return false;
}
}

namespace hades {
WeaponArsenalExecution::WeaponArsenalExecution(WeaponArsenal& arsenal,float dt):arsenal_(&arsenal),dt_(dt){}
bool WeaponArsenalExecution::advance(const WeaponArsenalFrame& frame,bool completed,
        WeaponArsenalStep& output,std::string& error){
    // Arsenal::Update RVA2fe520. Continuous RequestFire -> priority -> fixed
    // creation-vector count -> current fixed controllable-vector count. Native
    // manager/source ID sorting is not involved. Consumer owns Weapon::Update.
    error.clear();WeaponArsenalStep next;next.dt=dt_;
    if(finished_){error="Native Arsenal frame cursor already finished";return false;}
    if(frame.owner.ownerId!=arsenal_->ownerId_||!std::isfinite(dt_)||dt_<0){error="Native Arsenal frame owner/delta mismatch";return false;}
    if(!frame.owner.exists){next.progress=WeaponArsenalProgress::OwnerMissing;output=next;finished_=true;return true;}
    if(waiting_){if(!completed){error="Arsenal awaits complete native per-weapon update before advancing";return false;}waiting_=false;}
    else if(completed){error="Unexpected Arsenal per-weapon completion acknowledgement";return false;}
    while(phase_<5){
        switch(phase_){
        case 0:{
            if(frame.editingMode){next.progress=WeaponArsenalProgress::FrameFinished;output=next;finished_=true;return true;}
            // Same UnitStatus::CanAttack(false) gate: native time + pending AND
            // active DisableAttack/ImmuneToStun/SelfApplied checks. canAttack is
            // explicitly the parent's recovered status(false), not Data.CanAttack.
            if(frame.owner.alive&&frame.owner.canAttack&&!frame.continuousWeapon.empty()&&arsenal_->hasWeapon(frame.continuousWeapon)){
                WeaponFireRequest request;request.angle=-1;request.applySelfEffects=true;
                std::string gate;
                if(!arsenal_->requestFire(frame.continuousWeapon,request,frame.owner,gate)&&!gate.empty()){error=gate;return false;}
            }
            ++phase_;continue;
        }
        case 1:
            priority_=arsenal_->hasWeapon(frame.priorityWeapon)?frame.priorityWeapon:std::string{};
            ++phase_;
            if(!priority_.empty()){next.progress=WeaponArsenalProgress::WeaponUpdateRequired;next.weaponName=priority_;next.priority=true;output=next;waiting_=true;return true;}
            continue;
        case 2:
            capturedOrder_.clear();capturedOrder_.reserve(arsenal_->weapons_.size());
            for(const auto& w:arsenal_->weapons_)capturedOrder_.push_back(w.definition.name);
            index_=0;++phase_;continue;
        case 3:
            while(index_<capturedOrder_.size()){
                const auto name=capturedOrder_[index_++];if(name==priority_)continue;
                if(!arsenal_->hasWeapon(name)){error="Weapon removal/replacement during captured native Arsenal iteration requires lifetime reconstruction";return false;}
                next.progress=WeaponArsenalProgress::WeaponUpdateRequired;next.weaponName=name;next.priority=false;output=next;waiting_=true;return true;
            }
            ++phase_;continue;
        case 4:{
            // No callback in this loop. Current ordered membership after all
            // per-weapon callbacks is captured; decrement even below zero.
            const auto controlled=arsenal_->controllableOrder_;
            for(const auto& name:controlled)for(auto& w:arsenal_->weapons_)if(w.definition.name==name){
                if(w.definition.controlWindow>0)w.controlTime-=dt_;break;
            }
            next.progress=WeaponArsenalProgress::FrameFinished;output=next;finished_=true;++phase_;return true;
        }
        }
    }
    error="Invalid native Arsenal frame continuation";return false;
}
}

namespace hades {
WeaponCancelCharge::WeaponCancelCharge(WeaponArsenal& arsenal,std::string name):arsenal_(&arsenal),name_(std::move(name)){}
bool WeaponCancelCharge::advance(const WeaponOwnerFrame& owner,const WeaponOutputSupport& support,
        WeaponCancelChargeStep& output,std::string& error){
    // cancelChargeOnUnit RVA85960, native stores charge reset before any owner
    // animation/cancellation callback. It clears charging AFTER CancelSelfEffects
    // returns, then dispatches OnWeaponChargeCanceled with charging already false.
    error.clear();WeaponCancelChargeStep next;
    if(finished_){error="Native charge-cancel cursor already finished";return false;}
    if(owner.ownerId!=arsenal_->ownerId_){error="Native charge-cancel owner mismatch";return false;}
    WeaponSnapshot* w=nullptr;for(auto& value:arsenal_->weapons_)if(value.definition.name==name_){w=&value;break;}
    if(!w){error="Native charge-cancel weapon no longer owned";return false;}
    auto text=[&](const char* key,std::string& value){auto f=w->definition.fields.find(key);
        if(f==w->definition.fields.end()||!std::holds_alternative<std::string>(f->second)){error=std::string("Missing native charge-cancel string ")+key;return false;}
        value=std::get<std::string>(f->second);return true;};
    auto number=[&](const char* key,float& value){auto f=w->definition.fields.find(key);
        if(f==w->definition.fields.end()||!std::holds_alternative<double>(f->second)){error=std::string("Missing native charge-cancel number ")+key;return false;}
        value=static_cast<float>(std::get<double>(f->second));if(!std::isfinite(value)){error="Nonfinite charge-cancel data";return false;}return true;};
    auto commitReset=[&](){if(!resetCommitted_){w->chargeTimeRemaining=resetChargeTime_;if(clearRequest_)w->currentFireRequest.reset();resetCommitted_=true;}};
    auto emit=[&](WeaponOutput op){op.ownerId=owner.ownerId;op.weaponName=name_;
        if(!support){error="Native charge-cancel has no synchronous operation consumer";return false;}
        if(!support(op,error)){if(error.empty())error="Native charge-cancel operation unsupported "+op.name;return false;}
        commitReset();next.operation=std::move(op);next.progress=WeaponCancelChargeProgress::Yielded;output=std::move(next);++phase_;return true;};
    while(phase_<8){
        float f=0;std::string s;
        switch(phase_){
        case 0:{
            if(!number("ChargeTime",resetChargeTime_))return false;
            auto b=w->definition.fields.find("ClearFireRequestOnChargeCancel");
            if(b==w->definition.fields.end()||!std::holds_alternative<bool>(b->second)){error="Missing native ClearFireRequestOnChargeCancel";return false;}
            clearRequest_=std::get<bool>(b->second);wasCharging_=w->charging;
            fraction_=resetChargeTime_==0?1:std::clamp(1.0f-w->chargeTimeRemaining/resetChargeTime_,0.0f,1.0f);
            if(!wasCharging_){commitReset();next.progress=WeaponCancelChargeProgress::Finished;output=next;finished_=true;return true;}
            if(!owner.exists){error="Native charge-cancel live owner lifetime not available";return false;}
            if(w->chargeSoundId!=0||w->chargeFullSoundId!=0){error="Native charge-cancel actual audio-channel stop/fade consumer unresolved";return false;}
            ++phase_;continue;
        }
        case 1:
            if(!text("ChargeCancelGraphic",s)||!number("MinChargeToFire",f))return false;
            if(!s.empty()&&fraction_>0&&fraction_<f){WeaponOutput op;op.kind=WeaponOutputKind::SetOwnerAnimation;op.name=s;return emit(std::move(op));}
            ++phase_;continue;
        case 2:case 3:
            if(!text(phase_==2?"AddControlOnChargeCancel":"AddControlOnChargeCancel2",s))return false;
            if(!s.empty()&&arsenal_->hasWeapon(s)){
                // Native AddWeaponControl appends once, no timer reset and no
                // force-release flags. It is not SwapWeaponControl.
                commitReset();if(std::find(arsenal_->controllableOrder_.begin(),arsenal_->controllableOrder_.end(),s)==arsenal_->controllableOrder_.end())arsenal_->controllableOrder_.push_back(s);
                for(auto& candidate:arsenal_->weapons_)if(candidate.definition.name==s)candidate.controllable=true;
            }
            ++phase_;continue;
        case 4:
            // PDB mChargeSound+170/mChargeFullSound+174. AudioManager::StopCue
            // RVA4b740 explicitly skips ID0 (VA4b769/4b7db); full-sound direct
            // FMOD stop also has a nonzero-ID gate. No animation ID is stored
            // here and this routine does not directly stop charge FX.
            ++phase_;continue;
        case 5:{WeaponOutput op;op.kind=WeaponOutputKind::CancelWeaponSelfEffects;op.name="Charging";op.value=3;
            return emit(std::move(op));}
        case 6:{
            WeaponOutput op;op.kind=WeaponOutputKind::ScriptCallback;op.name="OnWeaponChargeCanceled";
            if(!emit(std::move(op)))return false;w->charging=false;return true;
        }
        case 7:commitReset();next.progress=WeaponCancelChargeProgress::Finished;output=next;finished_=true;return true;
        }
    }
    error="Invalid native charge-cancel continuation";return false;
}
}

namespace hades {
WeaponUpdateExecution::WeaponUpdateExecution(WeaponArsenal& arsenal,std::string name,float dt)
    :arsenal_(&arsenal),name_(std::move(name)),dt_(dt){}
bool WeaponUpdateExecution::advance(const WeaponOwnerFrame& owner,std::optional<bool> childResult,
        const WeaponOutputSupport& support,WeaponUpdateStep& output,std::string& error){
    // Weapon::Update RVA83b60, REA retained native-player-fire-rea-update.json.
    error.clear();WeaponUpdateStep next;next.dt=dt_;
    if(finished_){error="Native weapon update cursor already finished";return false;}
    if(owner.ownerId!=arsenal_->ownerId_||!std::isfinite(dt_)||dt_<0){error="Native weapon update owner/delta mismatch";return false;}
    if(!owner.exists){next.progress=WeaponUpdateProgress::OwnerMissing;output=next;finished_=true;return true;}
    WeaponSnapshot* w=nullptr;for(auto& candidate:arsenal_->weapons_)if(candidate.definition.name==name_){w=&candidate;break;}
    if(!w){error="Native weapon update has no current owned instance";return false;}
    if(waiting_){
        if(!childResult){error="Native weapon update awaits complete child routine result";return false;}
        if(child_==WeaponUpdateChild::HandleTriggerDown||child_==WeaponUpdateChild::HandleTriggerRelease)fired_=fired_||*childResult;
        else if(!*childResult){error="Native void reload child cannot acknowledge incomplete/false result";return false;}
        waiting_=false;
    }else if(childResult){error="Unexpected native weapon-update child reply";return false;}
    auto flag=[&](const char* key,bool& value){auto f=w->definition.fields.find(key);
        if(f==w->definition.fields.end()||!std::holds_alternative<bool>(f->second)){error=std::string("Missing native weapon-update flag ")+key;return false;}
        value=std::get<bool>(f->second);return true;};
    auto number=[&](const char* key,float& value){auto f=w->definition.fields.find(key);
        if(f==w->definition.fields.end()||!std::holds_alternative<double>(f->second)){error=std::string("Missing native weapon-update number ")+key;return false;}
        value=static_cast<float>(std::get<double>(f->second));if(!std::isfinite(value)){error="Nonfinite native weapon update field";return false;}return true;};
    auto text=[&](const char* key,std::string& value){auto f=w->definition.fields.find(key);
        if(f==w->definition.fields.end()||!std::holds_alternative<std::string>(f->second)){error=std::string("Missing native weapon-update string ")+key;return false;}
        value=std::get<std::string>(f->second);return true;};
    auto emit=[&](WeaponOutput op){op.ownerId=owner.ownerId;op.weaponName=name_;
        if(!support){error="Native weapon update has no synchronous operation consumer";return false;}
        if(!support(op,error)){if(error.empty())error="Native weapon update operation unsupported "+op.name;return false;}
        next.operation=std::move(op);next.progress=WeaponUpdateProgress::Yielded;output=std::move(next);++phase_;return true;};
    auto requireChild=[&](WeaponUpdateChild kind,const std::optional<WeaponFireRequest>& request={}){
        next.progress=WeaponUpdateProgress::ChildRequired;next.child=kind;if(request)next.request=*request;
        output=next;child_=kind;waiting_=true;++phase_;return true;
    };
    while(phase_<13){
        float f=0,g=0;bool b=false;std::string s;
        switch(phase_){
        case 0:
            if(!w->previousFireRequest&&w->currentFireRequest){
                if(!flag("TriggerTapIgnoresCooldown",b))return false;
                const float before=w->cooldownTimeRemaining;if(b)w->cooldownTimeRemaining=0;
                std::string gate;
                if(!arsenal_->canFire(name_,owner,gate)){
                    if(!gate.empty()){w->cooldownTimeRemaining=before;error=gate;return false;}
                    WeaponOutput op;op.kind=WeaponOutputKind::ScriptCallback;op.name="OnWeaponFailedToFire";op.value=1;op.controllable=true;op.freshInput=true; // Update183bd1: dl=r8b=1
                    if(!emit(std::move(op))){w->cooldownTimeRemaining=before;return false;}return true;
                }
            }
            ++phase_;continue;
        case 1:
            if(w->currentFireRequest){
                std::string gate;
                if(arsenal_->canFire(name_,owner,gate)){phase_=4;return requireChild(WeaponUpdateChild::HandleTriggerDown,w->currentFireRequest);}
                if(!gate.empty()){error=gate;return false;}
                if(!text("FailedToFireCooldownAnimation",s)||!flag("CanCancelDisables",b))return false;
                const bool status=b?owner.canAttackAllowCancellation:owner.canAttack;
                if(!s.empty()&&w->cooldownTimeRemaining<=0&&w->definition.initialCooldown>0&&!owner.animationBlocksAttack&&status){
                    WeaponOutput op;op.kind=WeaponOutputKind::SetOwnerAnimation;op.name=s;return emit(std::move(op));
                }
                phase_=3;continue;
            }
            phase_=5;continue;
        case 2:
            if(!number("FailedToFireCooldownDuration",f))return false;
            if(w->definition.cooldownMin!=0||w->definition.cooldownMax!=0){error="Native failed-fire cooldown requires original RNG child";return false;}
            w->cooldownTimeRemaining=std::max(w->cooldownTimeRemaining,f>0?f:w->definition.cooldown);
            failedCooldownAnimation_=true;++phase_;continue;
        case 3:
            if(!flag("ForceReleaseOnFail",b))return false;if(b)w->requireControlRelease=true;
            phase_=5;continue;
        case 4:error="Invalid native trigger-down continuation phase";return false;
        case 5:
            // Native if(current still present) goto timers; otherwise previous
            // request may run release in this same frame after PostFire cleared it.
            if(!w->currentFireRequest&&w->previousFireRequest)return requireChild(WeaponUpdateChild::HandleTriggerRelease,w->previousFireRequest);
            ++phase_;continue;
        case 6:
            if(!flag("ClipRegenOnRelease",b))return false;
            if(w->definition.clipRegenInterval>0&&w->clipShotsRemaining!=w->definition.clipSize&&!(b&&w->currentFireRequest)){
                w->nextClipRegenTime-=dt_*(1.0f/w->definition.clipRegenInterval);
                if(w->nextClipRegenTime<=0){if(!flag("FullClipRegen",b))return false;w->clipShotsRemaining=b?w->definition.clipSize:w->clipShotsRemaining+1;w->nextClipRegenTime=w->definition.clipRegenInterval;}
            }
            ++phase_;continue;
        case 7:
            if(!number("ScatterSubside",f)||!number("ScatterCap",g))return false;
            if(f>0){w->scatter-=static_cast<float>(static_cast<double>(f)*0.017453292519943295)*dt_;w->scatter=std::min(std::max(w->scatter,0.0f),static_cast<float>(static_cast<double>(g)*0.017453292519943295));}
            if(w->cooldownTimeRemaining>0)w->cooldownTimeRemaining-=dt_;
            ++phase_;continue;
        case 8:
            if(w->reloadTimeRemaining>0){
                if(!w->reloadPaused){w->reloadTimeRemaining-=dt_;if(w->reloadTimeRemaining<=0)return requireChild(WeaponUpdateChild::ReloadFinished);}
            }else{
                if(!flag("ReloadOnRelease",b))return false;
                if(b&&!w->currentFireRequest&&w->clipShotsRemaining!=w->definition.clipSize)return requireChild(WeaponUpdateChild::Reload);
            }
            ++phase_;continue;
        case 9:
            if(w->impactCueId!=0){error="Native weapon update impact cue timer/stop requires actual channel lifetime consumer";return false;}
            // pDelayedProjectile release (VA180083e80) is evaluated by the parent
            // before this frame (WeaponArsenal::clearDelayedProjectiles): only a
            // projectile created by this Update could change, and it is unstarted.
            ++phase_;continue;
        case 10:
            if(!flag("FireOnRelease",b))return false;
            w->previousFireRequest=w->currentFireRequest;
            if(fired_||failedCooldownAnimation_||b)w->currentFireRequest.reset();
            ++phase_;continue;
        case 11:
            // BlinkWeapon::Update7d1c0 runs its blink body after Weapon::Update83b60 returns.
            if(w->definition.type==1){WeaponOutput op;op.kind=WeaponOutputKind::BlinkUpdate;op.name="BlinkWeapon::Update";op.value=dt_;return emit(std::move(op));}
            ++phase_;continue;
        case 12:
            next.progress=WeaponUpdateProgress::FrameFinished;output=next;finished_=true;return true;
        default:error="Invalid native weapon-update phase";return false;
        }
    }
    error="Invalid native weapon-update continuation";return false;
}
}

namespace hades {
WeaponPostFire::WeaponPostFire(WeaponArsenal& arsenal,std::string name):arsenal_(&arsenal),name_(std::move(name)){}
bool WeaponPostFire::advance(const WeaponOwnerFrame& owner,std::optional<double> randomReply,
        const WeaponOutputSupport& support,WeaponPostFireStep& output,std::string& error){
    // Weapon::PostFire RVA89500, REA native-player-fire-rea-post-fire.json.
    // This is NOT a generic success stub: required real operations yield and
    // unsupported source branches fail at their actual continuation boundary.
    error.clear();WeaponPostFireStep next;
    if(finished_){error="Native PostFire continuation already finished";return false;}
    if(owner.ownerId!=arsenal_->ownerId_||!owner.exists){error="Native PostFire requires actual current owner lifetime";return false;}
    WeaponSnapshot* w=nullptr;for(auto& value:arsenal_->weapons_)if(value.definition.name==name_){w=&value;break;}
    if(!w){error="Native PostFire has no owned weapon instance";return false;}
    auto flag=[&](const char* key,bool& value){auto f=w->definition.fields.find(key);
        if(f==w->definition.fields.end()||!std::holds_alternative<bool>(f->second)){error=std::string("Missing native PostFire flag ")+key;return false;}
        value=std::get<bool>(f->second);return true;};
    auto number=[&](const char* key,float& value){auto f=w->definition.fields.find(key);
        if(f==w->definition.fields.end()||!std::holds_alternative<double>(f->second)){error=std::string("Missing native PostFire number ")+key;return false;}
        value=static_cast<float>(std::get<double>(f->second));if(!std::isfinite(value)){error="Nonfinite native PostFire data";return false;}return true;};
    auto text=[&](const char* key,std::string& value){auto f=w->definition.fields.find(key);
        if(f==w->definition.fields.end()||!std::holds_alternative<std::string>(f->second)){error=std::string("Missing native PostFire text ")+key;return false;}
        value=std::get<std::string>(f->second);return true;};
    auto emit=[&](WeaponOutput op){op.ownerId=owner.ownerId;op.weaponName=name_;
        if(!support){error="Native PostFire operation has no synchronous consumer";return false;}
        if(!support(op,error)){if(error.empty())error="Unsupported native PostFire operation "+op.name;return false;}
        next.operation=std::move(op);next.progress=WeaponPostFireProgress::Yielded;output=std::move(next);++phase_;return true;};
    if(await_!=Await::None){
        if(!randomReply||!std::isfinite(*randomReply)||*randomReply<0||*randomReply>=1){error="Native PostFire awaits one original RandomDouble result";return false;}
        pendingRandom_=static_cast<float>(*randomReply)*(randomMax_-randomMin_)+randomMin_;await_=Await::None;
    }else if(randomReply){error="Unrequested native PostFire random reply";return false;}
    while(phase_<30){
        bool b=false;float f=0,g=0;std::string s;
        switch(phase_){
        case 0:
            if(!flag("CanCancelDisables",b))return false;
            if(b){WeaponOutput op;op.kind=WeaponOutputKind::CancelOwnerDisables;op.name="PostFire";return emit(std::move(op));}
            if(!flag("CanCancelRotateDisables",b))return false;
            if(b){WeaponOutput op;op.kind=WeaponOutputKind::CancelOwnerRotateDisables;op.name="PostFire";return emit(std::move(op));}
            ++phase_;continue;
        case 1:
            if(!flag("CancelAttachedProjectiles",b))return false;
            if(b&&!cancelAttachedDone_){cancelAttachedDone_=true;WeaponOutput op;op.kind=WeaponOutputKind::CancelAttachedProjectiles;op.name="PostFire CancelAttachedProjectiles";op.ownerId=owner.ownerId;op.weaponName=name_;
                if(!support||!support(op,error)){if(error.empty())error="PostFire attached-projectile scan unsupported";return false;}
                next.operation=std::move(op);output=std::move(next);return true;}
            if(!number("ArsenalCooldown",f)||!number("CooldownWeaponSlotThree",g))return false;
            if(f>0||g>0){error="Native PostFire arsenal cooldown propagation branch not reconstructed";return false;}
            ++phase_;continue;
        case 2:{
            // Weapon::PostFire VA180089500 (native-player-fire-post-fire-pseudocode.c l.213-334),
            // WeaponData offsets = WeaponDataDef + 0x50 (native-fields-WeaponDataDef.txt):
            // AddControlOnFire/2/3 (+0x198/+0x1b0/+0x1c8) each resolve through
            // UnitArsenal::GetWeapon VA1802feae0 (name match over every owned weapon, null when
            // absent or empty) and UnitArsenal::AddWeaponControl VA1802ff480; then
            // RemoveControlOnFire/2/3/4 (+0x138/+0x150/+0x168/+0x180) through GetWeapon and
            // UnitArsenal::RemoveWeaponControl VA1802ff510 (membership + request clear, then its
            // CancelChargeOnControlRemoved cancel, run by the RemoveWeaponControl consumer);
            // RemoveAllControlSwaps (+0x210) clears pGainedControlFrom (Weapon+0x88) of every owned
            // weapon whose gained-from weapon's name equals it.
            static constexpr const char* keys[]{"AddControlOnFire","AddControlOnFire2","AddControlOnFire3","RemoveControlOnFire","RemoveControlOnFire2","RemoveControlOnFire3","RemoveControlOnFire4"};
            while(controlKey_<7){
                const unsigned k=controlKey_++;if(!text(keys[k],s))return false;if(s.empty())continue;
                bool owned=false;for(const auto& candidate:arsenal_->weapons_)if(candidate.definition.name==s){owned=true;break;}
                if(!owned)continue; // GetWeapon null: native skips
                if(k<3){if(!arsenal_->addWeaponControl(s,error))return false;continue;}
                WeaponOutput op;op.kind=WeaponOutputKind::RemoveWeaponControl;op.name=s;op.ownerId=owner.ownerId;op.weaponName=name_;
                if(!support||!support(op,error)){if(error.empty())error="PostFire RemoveWeaponControl unsupported";return false;}
                next.operation=std::move(op);output=std::move(next);return true;
            }
            if(!text("RemoveAllControlSwaps",s))return false;
            if(!s.empty()){w=nullptr;for(auto& value:arsenal_->weapons_){if(value.gainedControlFrom==s)value.gainedControlFrom.clear();if(value.definition.name==name_)w=&value;}
                if(!w){error="PostFire weapon lost during RemoveAllControlSwaps";return false;}}
            ++phase_;continue;}
        case 3:
            if(!flag("ForceReleaseOnFire",b))return false;if(b)w->requireControlRelease=true;
            if(!text("ForceReleaseWeaponOnFire",s))return false;
            if(!s.empty())for(auto& candidate:arsenal_->weapons_)if(candidate.definition.name==s)candidate.requireControlRelease=true;
            // Stack-local target snapshot precedes subsequent request clearing.
            if(w->currentFireRequest){hasTarget_=true;targetX_=w->currentFireRequest->targetX;targetY_=w->currentFireRequest->targetY;}
            else if(w->previousFireRequest){hasTarget_=true;targetX_=w->previousFireRequest->targetX;targetY_=w->previousFireRequest->targetY;}
            ++phase_;continue;
        case 4:
            if(!text("ClearWeaponFireRequestOnFire",s))return false;
            if(!s.empty())for(auto& candidate:arsenal_->weapons_)if(candidate.definition.name==s)candidate.currentFireRequest.reset();
            if(!flag("ForceReleaseAllOnFire",b))return false;
            if(b){
                // Weapon::PostFire VA180089dxx (native-player-fire-post-fire-pseudocode.c l.380-417):
                // every other arsenal weapon (+0x430 creation order) with pControl(+0x48) gets
                // mRequireControlRelease(+0x11)=1 when AllowExternalForceRelease(+0x749), or
                // IfMinCharged(+0x74a) && fraction>=MinChargeToFire(+0x7b0), or IfAnyCharged(+0x74b) &&
                // (ChargeTime==0 || fraction>0). pControl is the resolved primary control (controlIndices[0]).
                for(auto& other:arsenal_->weapons_){
                    if(&other==w||other.controlIndices[0]<0)continue;
                    const auto& od=other.definition;
                    auto of=[&](const char* key,bool& v){auto i=od.fields.find(key);if(i==od.fields.end()||!std::holds_alternative<bool>(i->second)){error=std::string("Missing external-force-release flag ")+key;return false;}v=std::get<bool>(i->second);return true;};
                    bool allow=false,minCharged=false,anyCharged=false;if(!of("AllowExternalForceRelease",allow)||!of("AllowExternalForceReleaseIfMinCharged",minCharged)||!of("AllowExternalForceReleaseIfAnyCharged",anyCharged))return false;
                    const float fraction=od.chargeTime==0?1.0f:std::clamp(1.0f-other.chargeTimeRemaining/od.chargeTime,0.0f,1.0f);
                    bool set=allow;
                    if(!set&&minCharged){auto m=od.fields.find("MinChargeToFire");if(m==od.fields.end()||!std::holds_alternative<double>(m->second)){error="Missing MinChargeToFire";return false;}if(float(std::get<double>(m->second))<=fraction)set=true;}
                    if(!set&&anyCharged)set=od.chargeTime==0||fraction>0;
                    if(set)other.requireControlRelease=true;
                }
                w=nullptr;for(auto& value:arsenal_->weapons_)if(value.definition.name==name_){w=&value;break;}
            }
            if(w->reDisableAfterFire){w->definition.fields["Enabled"]=false;w->reDisableAfterFire=false;}
            ++phase_;continue;
        case 5:
            if(w->definition.cooldownMin!=0||w->definition.cooldownMax!=0){
                randomMin_=w->definition.cooldownMin;randomMax_=w->definition.cooldownMax;
                WeaponOutput op;op.kind=WeaponOutputKind::RequestRandomDouble;op.name="PostFire cooldown";
                if(!emit(std::move(op)))return false;await_=Await::Cooldown;return true;
            }
            pendingRandom_=w->definition.cooldown;++phase_;continue;
        case 6:
            w->cooldownTimeRemaining=std::max(w->cooldownTimeRemaining,pendingRandom_);
            w->nextClipRegenTime=w->definition.clipRegenInterval;
            if(w->clipShotsRemaining==INT32_MIN){error="Native PostFire clip counter wrap outside portable subset";return false;}
            --w->clipShotsRemaining;
            w->critBonusRemaining=w->critBonusRemaining<1?0:w->critBonusRemaining-1;
            w->additiveDamageBonusRemaining=w->additiveDamageBonusRemaining<1?0:w->additiveDamageBonusRemaining-1;
            if(!number("Scatter",f)||!number("ScatterCap",g))return false;
            w->scatter+=static_cast<float>(static_cast<double>(f)*0.017453292519943295);
            w->scatter=std::min(std::max(w->scatter,0.0f),static_cast<float>(static_cast<double>(g)*0.017453292519943295));
            ++phase_;continue;
        case 7:
            if(!number("DisableAttackForDuration",f)||!number("DisableOwnerForDuration",g))return false;
            // Status timers do not affect the clip test, so it is staged first;
            // emit advances phase_ past this step.
            clipEmpty_=w->definition.clipSize!=0&&w->clipShotsRemaining==0;
            if(f>0||g>0){WeaponOutput op;op.kind=WeaponOutputKind::ExtendOwnerDisables;op.name="PostFire";op.disableAttackFor=f;op.disableOwnerFor=g;return emit(std::move(op));}
            ++phase_;continue;
        case 8:
            if(clipEmpty_&&w->definition.clipRegenInterval<=0){
                if(w->definition.reloadTimeMin>0){
                    randomMin_=w->definition.reloadTimeMin;randomMax_=w->definition.reloadTimeMax;
                    WeaponOutput op;op.kind=WeaponOutputKind::RequestRandomDouble;op.name="PostFire reload";
                    if(!emit(std::move(op)))return false;await_=Await::Reload;return true;
                }
                pendingRandom_=w->definition.reloadTime;
            }
            ++phase_;continue;
        case 9:
            if(clipEmpty_&&w->definition.clipRegenInterval<=0){
                w->reloadTimeRemaining=pendingRandom_;
                if(w->reloadTimeRemaining<=0){w->clipShotsRemaining=w->definition.clipSize;++phase_;continue;}
            }
            phase_=12;continue;
        case 10:
            if(!w->definition.swapOnReloadFinish.empty()&&arsenal_->hasWeapon(w->definition.swapOnReloadFinish)){
                if(!arsenal_->swapWeaponControl(name_,w->definition.swapOnReloadFinish,owner,error)&&!error.empty())return false;
            }
            ++phase_;continue;
        case 11:{
            WeaponOutput op;op.kind=WeaponOutputKind::SetOwnerAnimation;op.name=w->definition.reloadFinishGraphic;
            return emit(std::move(op));
        }
        case 12:
            if(clipEmpty_){
                if(!flag("UseReloadGraphic",b))return false;
                if(b){if(!text("ReloadGraphic",s))return false;WeaponOutput op;op.kind=WeaponOutputKind::SetOwnerAnimation;op.name=s;return emit(std::move(op));}
            }
            ++phase_;continue;
        case 13:
            if(clipEmpty_){
                if(!text("ReloadFx",s))return false;
                WeaponOutput op;op.kind=WeaponOutputKind::CreateWeaponWorldAnimation;op.name=s;op.x=owner.x;op.y=owner.y;
                return emit(std::move(op));
            }
            ++phase_;continue;
        case 14:
            if(clipEmpty_){
                if(!flag("StopFireSoundOnClipEmpty",b))return false;
                if(b&&w->fireSoundId!=0){error="Native PostFire actual FireSound channel stop requires lifetime consumer";return false;}
                auto cue=w->definition.cues.find("ReloadSound");
                if(cue==w->definition.cues.end()||!cue->second.validityKnown||cue->second.valid){error="Native PostFire ReloadSound lookup/playback requires actual audio consumer";return false;}
                if(b)w->fireSoundId=0;
            }
            ++phase_;continue;
        case 15:
            if(!flag("CheckDanger",b))return false;
            // CheckDanger(+0x990) -> Weapon::GetNearbyDanger877a0; its id is OnWeaponFired's danger argument.
            if(b&&!dangerQueried_){dangerQueried_=true;WeaponOutput op;op.kind=WeaponOutputKind::QueryNearbyDanger;op.name="Weapon::GetNearbyDanger";op.ownerId=owner.ownerId;op.weaponName=name_;
                if(!support||!support(op,error)){if(error.empty())error="PostFire danger query unsupported";return false;}
                next.operation=std::move(op);output=std::move(next);return true;}
            {WeaponOutput op;op.kind=WeaponOutputKind::ScriptCallback;op.name="OnWeaponFired";
                op.hasTargetLocation=hasTarget_;op.x=targetX_;op.y=targetY_;op.value=0;
                return emit(std::move(op));}
        case 16:
            if(!flag("OnlyChargeOnce",b))return false;
            if(!b){
                if(!cancel_)cancel_=std::make_unique<WeaponCancelCharge>(*arsenal_,name_);
                WeaponCancelChargeStep child;
                if(!cancel_->advance(owner,support,child,error))return false;
                if(child.progress==WeaponCancelChargeProgress::Yielded){next.operation=std::move(child.operation);next.progress=WeaponPostFireProgress::Yielded;output=std::move(next);return true;}
            }
            ++phase_;continue;
        case 17:
            if(!text("SwapOnFire",s))return false;
            if(!s.empty()&&arsenal_->hasWeapon(s))if(!arsenal_->swapWeaponControl(name_,s,owner,error)&&!error.empty())return false;
            ++phase_;continue;
        case 18:
            if(!text("AddOnFire",s))return false;
            if(!s.empty())for(auto& candidate:arsenal_->weapons_)if(candidate.definition.name==s){
                if(std::find(arsenal_->controllableOrder_.begin(),arsenal_->controllableOrder_.end(),s)==arsenal_->controllableOrder_.end())arsenal_->controllableOrder_.push_back(s);
                candidate.controllable=true;if(candidate.definition.controlWindow>0)candidate.controlTime=candidate.definition.controlWindow;
                auto f=candidate.definition.fields.find("ForceReleaseOnSwap");
                if(f==candidate.definition.fields.end()||!std::holds_alternative<bool>(f->second)){error="Missing target ForceReleaseOnSwap";return false;}
                if(std::get<bool>(f->second))candidate.requireControlRelease=true;
            }
            ++phase_;continue;
        case 19:
            if(!flag("CancelRamWeaponOnFire",b))return false;if(b){error="Native PostFire EndRamWeapon needs real RAM component";return false;}
            // BlinkWeapon::PostFire7df20: after Weapon::PostFire, only a live pFireFx (+0x150,
            // zeroed by Weapon ctor VA1800887a7) gets angle +0x19c / radius +0x1a0. No reached
            // native writer populates it in this port; a live one must not be skipped.
            if(w->definition.type==1&&w->fireFxLifetime){error="BlinkWeapon::PostFire pFireFx SetAngle/SetRadius consumer not reconstructed";return false;}
            next.progress=WeaponPostFireProgress::Finished;output=next;finished_=true;return true;
        default:error="Invalid native PostFire continuation phase";return false;
        }
    }
    error="Invalid native PostFire continuation";return false;
}
}

namespace hades {
WeaponTriggerExecution::WeaponTriggerExecution(WeaponArsenal& arsenal,std::string name,float dt,WeaponFireRequest request)
    :arsenal_(&arsenal),name_(std::move(name)),dt_(dt),request_(request){}
bool WeaponTriggerExecution::advance(const WeaponOwnerFrame& owner,const WeaponTriggerReply& reply,
        const WeaponOutputSupport& support,WeaponTriggerStep& output,std::string& error){
    // Matching PDB HandleTriggerDown RVA863f0; source instructions at
    // native-player-weapon-trigger-down.json. Child routines preserve their
    // own stack values and reacquire instance data after every callback.
    error.clear();WeaponTriggerStep next;
    if(finished_){error="Native trigger-down cursor already finished";return false;}
    if(!std::isfinite(dt_)||dt_<0){error="Invalid native trigger delta";return false;}
    if(!owner.exists){next.progress=WeaponTriggerProgress::OwnerMissing;output=next;finished_=true;return true;}
    if((phase_!=2&&phase_!=3&&phase_!=5&&reply.randomDouble)||(phase_!=2&&phase_!=3&&reply.createdProjectile)||(phase_!=4&&reply.createdAnimation)||(phase_!=2&&reply.blinkFireContinues)){
        error="Unexpected typed native trigger reply at phase "+std::to_string(phase_)+" random="+std::to_string(reply.randomDouble.has_value())+" projectile="+std::to_string(reply.createdProjectile.has_value())+" animation="+std::to_string(reply.createdAnimation.has_value());return false;
    }
    WeaponTriggerReply remaining=reply;
    while(phase_<7){
        const auto* w=arsenal_->weaponState(name_,error);if(!w)return false;
        auto flag=[&](const char* key,bool& v){auto f=w->definition.fields.find(key);
            if(f==w->definition.fields.end()||!std::holds_alternative<bool>(f->second)){error=std::string("Missing native trigger flag ")+key;return false;}v=std::get<bool>(f->second);return true;};
        switch(phase_){
        case 0:{
            bool root=false;if(!flag("RootOwnerWhileFiring",root))return false;
            auto duration=w->definition.fields.find("RootOwnerWhileFiringDuration");
            if(duration==w->definition.fields.end()||!std::holds_alternative<double>(duration->second)){error="Missing native trigger root duration";return false;}
            if(!std::isfinite(std::get<double>(duration->second))){error="Nonfinite native trigger root duration";return false;}
            if(root&&request_.applySelfEffects&&std::get<double>(duration->second)>0&&!rootApplied_){
                WeaponOutput op;op.kind=WeaponOutputKind::RootOwnerMovement;op.ownerId=owner.ownerId;op.weaponName=name_;op.name="HandleTriggerDown noncancelable move disable";op.value=static_cast<float>(std::get<double>(duration->second));
                if(!support||!support(op,error)){if(error.empty())error="Native trigger requires actual noncancelable UnitStatus move-disable consumer";return false;}
                rootApplied_=true;next.operation=std::move(op);output=next;return true;
            }
            WeaponExecutionRequest q;q.weaponName=name_;q.dt=dt_;q.angle=request_.angle;q.hasCompleteAngle=true;
            charge_=std::make_unique<WeaponExecution>(*arsenal_,q);++phase_;continue;
        }
        case 1:{
            WeaponExecutionStep child;if(!charge_->advance(owner,support,child,error))return false;
            if(child.progress==WeaponExecutionProgress::OwnerMissing){next.progress=WeaponTriggerProgress::OwnerMissing;output=next;finished_=true;return true;}
            if(child.progress==WeaponExecutionProgress::Yielded){next.operation=std::move(child.operation);output=next;return true;}
            bool release=false;if(!flag("FireOnRelease",release))return false;
            if(child.progress==WeaponExecutionProgress::Blocked||!child.chargeReady||release){
                // ShowAimLine has a native source guard at VA180086677.
                bool show=false;if(!flag("ShowFreeAimLine",show))return false;
                if(show){error="Native ShowAimLine requires original player UI geometry/animation consumer";return false;}
                next.progress=WeaponTriggerProgress::Finished;output=next;finished_=true;return true;
            }
            // Native receives a reference to the current request. Reacquire its
            // values after synchronous charge callbacks, instead of firing an
            // obsolete pre-callback target/angle copy.
            if(!w->currentFireRequest){error="Native charge callback invalidated current fire request";return false;}
            request_=*w->currentFireRequest;
            prefix_=std::make_unique<WeaponFirePrefix>(*arsenal_,name_,request_);++phase_;continue;
        }
        case 2:{
            WeaponBlinkReply r;r.randomDouble=remaining.randomDouble;r.createdProjectile=remaining.createdProjectile;r.fireContinues=remaining.blinkFireContinues;
            remaining.randomDouble.reset();remaining.createdProjectile.reset();remaining.blinkFireContinues.reset();
            WeaponFirePrefixStep child;if(!prefix_->advance(owner,r,support,child,error))return false;
            if(child.progress==WeaponFirePrefixProgress::OwnerMissing){next.progress=WeaponTriggerProgress::OwnerMissing;output=next;finished_=true;return true;}
            if(child.progress==WeaponFirePrefixProgress::Yielded){next.expectsRandomReply=child.expectsRandomReply;next.expectsProjectileReply=child.expectsProjectileReply;next.expectsBlinkReply=child.expectsBlinkReply;next.operation=std::move(child.operation);output=next;return true;}
            // BlinkWeapon::Fire has no body after Weapon::Fire88f00 (tail call VA18007dece).
            if(w->definition.type==1){fx_=std::make_unique<WeaponFireEffects>(*arsenal_,name_,request_);phase_=4;continue;}
            gun_=std::make_unique<WeaponGunExecution>(*arsenal_,name_,request_);++phase_;continue;
        }
        case 3:{
            WeaponGunStep child;WeaponGunReply r;r.randomDouble=remaining.randomDouble;r.createdProjectile=remaining.createdProjectile;
            if(!gun_->advance(owner,r,support,child,error))return false;remaining.randomDouble.reset();remaining.createdProjectile.reset();
            if(child.progress==WeaponGunProgress::OwnerMissing){next.progress=WeaponTriggerProgress::OwnerMissing;output=next;finished_=true;return true;}
            if(child.progress==WeaponGunProgress::Yielded){next.expectsRandomReply=child.operation.kind==WeaponOutputKind::RequestRandomDouble;next.expectsProjectileReply=child.operation.kind==WeaponOutputKind::ProjectileSpawn;next.operation=std::move(child.operation);output=next;return true;}
            // HandleTriggerDown ignores virtual Fire's bool and always runs FX
            // plus PostFire, including a false return for no valid projectile.
            fx_=std::make_unique<WeaponFireEffects>(*arsenal_,name_,request_);++phase_;continue;
        }
        case 4:{
            WeaponFireEffectsStep child;if(!fx_->advance(owner,remaining.createdAnimation,support,child,error))return false;remaining.createdAnimation.reset();
            if(child.progress==WeaponFireEffectsProgress::OwnerMissing){next.progress=WeaponTriggerProgress::OwnerMissing;output=next;finished_=true;return true;}
            if(child.progress==WeaponFireEffectsProgress::Yielded){next.expectsAnimationReply=child.operation.kind==WeaponOutputKind::CreateWeaponWorldAnimation;next.operation=std::move(child.operation);output=next;return true;}
            post_=std::make_unique<WeaponPostFire>(*arsenal_,name_);++phase_;continue;
        }
        case 5:{
            WeaponPostFireStep child;if(!post_->advance(owner,remaining.randomDouble,support,child,error))return false;remaining.randomDouble.reset();
            if(child.progress==WeaponPostFireProgress::Yielded){next.expectsRandomReply=child.operation.kind==WeaponOutputKind::RequestRandomDouble;next.operation=std::move(child.operation);output=next;return true;}
            ++phase_;continue;
        }
        case 6:next.progress=WeaponTriggerProgress::Finished;next.nativeResult=true;output=next;finished_=true;return true;
        }
    }
    error="Invalid native trigger-down phase";return false;
}

WeaponTriggerRelease::WeaponTriggerRelease(WeaponArsenal& arsenal,std::string name,float dt,WeaponFireRequest request)
    :arsenal_(&arsenal),name_(std::move(name)),dt_(dt),request_(request){}
bool WeaponTriggerRelease::advance(const WeaponOwnerFrame& owner,const WeaponOutputSupport& support,
        WeaponTriggerStep& output,std::string& error){
    // Native RVA87150, native-player-trigger-release-static.json and retained
    // REA native-player-fire-trigger-release-pseudocode.c. This class does not
    // claim unsupported release-fired weapons have completed.
    error.clear();WeaponTriggerStep next;
    if(finished_){error="Native trigger-release cursor already finished";return false;}
    if(!owner.exists){error="Native trigger-release owner lifetime missing";return false;}
    if(!std::isfinite(dt_)||dt_<0){error="Invalid native trigger-release delta";return false;}
    while(phase_<8){
        const auto* w=arsenal_->weaponState(name_,error);if(!w)return false;
        auto value=[&](const char* key)->const WeaponScalar*{auto i=w->definition.fields.find(key);if(i==w->definition.fields.end()){error=std::string("Missing native trigger-release field ")+key;return nullptr;}return &i->second;};
        auto flag=[&](const char* key,bool& v){const auto* a=value(key);if(!a)return false;if(!std::holds_alternative<bool>(*a)){error="Invalid native release flag type";return false;}v=std::get<bool>(*a);return true;};
        auto text=[&](const char* key,std::string& v){const auto* a=value(key);if(!a)return false;if(!std::holds_alternative<std::string>(*a)){error="Invalid native release string type";return false;}v=std::get<std::string>(*a);return true;};
        auto emit=[&](WeaponOutputKind kind,std::string name){WeaponOutput op;op.kind=kind;op.ownerId=owner.ownerId;op.weaponName=name_;op.name=std::move(name);
            if(!support||!support(op,error)){if(error.empty())error="Unsupported native trigger-release operation";return false;}
            next.operation=std::move(op);output=next;++phase_;return true;};
        bool b=false;std::string s;
        switch(phase_){
        case 0:
            if(!flag("FireOnRelease",b))return false;
            if(b){error="Native FireOnRelease requires separate attack-disable/override virtual-fire continuation";return false;}
            ++phase_;continue;
        case 1:
            if(!flag("Enabled",b))return false;
            if(!b){next.progress=WeaponTriggerProgress::Finished;output=next;finished_=true;return true;}
            if(!text("SwapOnRelease",s))return false;
            if(!s.empty()&&!arsenal_->swapWeaponControl(name_,s,owner,error))return false;
            ++phase_;continue;
        case 2:
            if(!flag("CancelChargeOnRelease",b))return false;
            if(!b){phase_=5;continue;}
            if(w->charging){if(!text("ChargeCancelGraphic",s))return false;if(!s.empty())return emit(WeaponOutputKind::SetOwnerAnimation,s);}
            ++phase_;continue;
        case 3:cancel_=std::make_unique<WeaponCancelCharge>(*arsenal_,name_);++phase_;continue;
        case 4:{WeaponCancelChargeStep child;if(!cancel_->advance(owner,support,child,error))return false;
            if(child.progress==WeaponCancelChargeProgress::Yielded){next.operation=std::move(child.operation);output=next;return true;}
            ++phase_;continue;}
        case 5:{
            const auto* v=value("TriggerReleaseDisableDuration");if(!v)return false;
            if(!std::holds_alternative<double>(*v)){error="Invalid release-disable duration type";return false;}
            if(std::get<double>(*v)>0){error="Native trigger-release disable move/attack/rotate timers require actual UnitStatus consumer";return false;}
            ++phase_;continue;
        }
        case 6:return emit(WeaponOutputKind::ScriptCallback,"OnWeaponTriggerRelease");
        case 7:next.progress=WeaponTriggerProgress::Finished;output=next;finished_=true;return true;
        }
    }
    error="Invalid native trigger-release phase";return false;
}
}

namespace hades {
WeaponArsenalFrameExecution::WeaponArsenalFrameExecution(WeaponArsenal& arsenal,float dt)
    :arsenal_(&arsenal),dt_(dt),outer_(std::make_unique<WeaponArsenalExecution>(arsenal,dt)){}
bool WeaponArsenalFrameExecution::advance(const WeaponArsenalFrame& frame,const WeaponTriggerReply& reply,
        const WeaponOutputSupport& support,WeaponArsenalFrameStep& output,std::string& error){
    error.clear();WeaponArsenalFrameStep next;
    if(finished_){error="Native Arsenal composed frame already finished";return false;}
    if((phase_!=2&&phase_!=4&&reply.randomDouble)||(phase_!=2&&(reply.createdProjectile||reply.createdAnimation))){error="Unexpected reply outside actual native trigger child";return false;}
    WeaponTriggerReply remaining=reply;
    for(unsigned transitions=0;transitions<100000;++transitions){
        switch(phase_){
        case 0:{
            WeaponArsenalStep child;
            if(!outer_->advance(frame,completedWeapon_,child,error))return false;completedWeapon_=false;
            if(child.progress==WeaponArsenalProgress::OwnerMissing){next.progress=WeaponArsenalFrameProgress::OwnerMissing;output=next;finished_=true;return true;}
            if(child.progress==WeaponArsenalProgress::FrameFinished){next.progress=WeaponArsenalFrameProgress::FrameFinished;output=next;finished_=true;return true;}
            current_=child.weaponName;update_=std::make_unique<WeaponUpdateExecution>(*arsenal_,current_,dt_);phase_=1;continue;
        }
        case 1:{
            WeaponUpdateStep child;
            if(!update_->advance(frame.owner,childResult_,support,child,error))return false;childResult_.reset();
            if(child.progress==WeaponUpdateProgress::OwnerMissing){next.progress=WeaponArsenalFrameProgress::OwnerMissing;output=next;finished_=true;return true;}
            if(child.progress==WeaponUpdateProgress::Yielded){next.operation=std::move(child.operation);output=next;return true;}
            if(child.progress==WeaponUpdateProgress::FrameFinished){update_.reset();completedWeapon_=true;phase_=0;continue;}
            switch(child.child){
            case WeaponUpdateChild::HandleTriggerDown:
                down_=std::make_unique<WeaponTriggerExecution>(*arsenal_,current_,dt_,child.request);phase_=2;continue;
            case WeaponUpdateChild::HandleTriggerRelease:
                release_=std::make_unique<WeaponTriggerRelease>(*arsenal_,current_,dt_,child.request);phase_=3;continue;
            case WeaponUpdateChild::Reload:case WeaponUpdateChild::ReloadFinished:
                reload_=std::make_unique<WeaponReloadExecution>(*arsenal_,current_,child.child==WeaponUpdateChild::ReloadFinished);phase_=4;continue;
            }
            error="Invalid native WeaponUpdate child";return false;
        }
        case 2:{
            WeaponTriggerStep child;
            if(!down_->advance(frame.owner,remaining,support,child,error))return false;remaining={};
            if(child.progress==WeaponTriggerProgress::OwnerMissing){next.progress=WeaponArsenalFrameProgress::OwnerMissing;output=next;finished_=true;return true;}
            if(child.progress==WeaponTriggerProgress::Yielded){next.operation=std::move(child.operation);next.expectsRandomReply=child.expectsRandomReply;next.expectsProjectileReply=child.expectsProjectileReply;next.expectsAnimationReply=child.expectsAnimationReply;next.expectsBlinkReply=child.expectsBlinkReply;output=next;return true;}
            childResult_=child.nativeResult;down_.reset();phase_=1;continue;
        }
        case 3:{
            WeaponTriggerStep child;
            if(!release_->advance(frame.owner,support,child,error))return false;
            if(child.progress==WeaponTriggerProgress::Yielded){next.operation=std::move(child.operation);output=next;return true;}
            if(child.progress==WeaponTriggerProgress::OwnerMissing){next.progress=WeaponArsenalFrameProgress::OwnerMissing;output=next;finished_=true;return true;}
            childResult_=child.nativeResult;release_.reset();phase_=1;continue;
        }
        case 4:{
            WeaponReloadStep child;
            if(!reload_->advance(frame.owner,remaining.randomDouble,support,child,error))return false;remaining={};
            if(child.progress==WeaponReloadProgress::Yielded){next.operation=std::move(child.operation);next.expectsRandomReply=child.expectsRandomReply;output=next;return true;}
            childResult_=true;reload_.reset();phase_=1;continue;
        }
        default:error="Invalid native Arsenal frame phase";return false;
        }
    }
    error="Native Arsenal frame exceeded bounded internal transition budget";return false;
}
}

namespace hades {
WeaponReloadExecution::WeaponReloadExecution(WeaponArsenal& arsenal,std::string name,bool finishedOnly)
    :arsenal_(&arsenal),name_(std::move(name)),phase_(finishedOnly?2:0){}
bool WeaponReloadExecution::advance(const WeaponOwnerFrame& owner,std::optional<double> reply,
        const WeaponOutputSupport& support,WeaponReloadStep& output,std::string& error){
    // Native reload RVA85830 and onReloadFinished RVA87640. The latter resets
    // clip before checking owner existence; actual owner is required only for
    // swap and SetAnimation. No invented reload callback or FX is added here.
    error.clear();WeaponReloadStep next;
    if(finished_){error="Native reload cursor already finished";return false;}
    if(owner.ownerId!=arsenal_->ownerId_){error="Native reload owner identity mismatch";return false;}
    WeaponSnapshot* w=nullptr;for(auto& value:arsenal_->weapons_)if(value.definition.name==name_){w=&value;break;}
    if(!w){error="Native reload weapon no longer owned";return false;}
    if(awaitingRandom_){
        if(!reply||!std::isfinite(*reply)||*reply<0||*reply>=1){error="Native reload awaits one original RandomDouble result";return false;}
        w->reloadTimeRemaining=static_cast<float>(*reply)*(maximum_-minimum_)+minimum_;awaitingRandom_=false;
        phase_=w->reloadTimeRemaining<=0?2:5;
    }else if(reply){error="Unrequested native reload random result";return false;}
    auto emit=[&](WeaponOutput op){op.ownerId=owner.ownerId;op.weaponName=name_;
        if(!support||!support(op,error)){if(error.empty())error="Native reload operation has no actual consumer";return false;}
        next.operation=std::move(op);output=next;++phase_;return true;};
    while(phase_<6){
        switch(phase_){
        case 0:
            minimum_=w->definition.reloadTimeMin;maximum_=w->definition.reloadTimeMax;
            if(!std::isfinite(minimum_)||!std::isfinite(maximum_)||!std::isfinite(w->definition.reloadTime)){error="Nonfinite native reload interval";return false;}
            if(minimum_>0){WeaponOutput op;op.kind=WeaponOutputKind::RequestRandomDouble;op.name="Weapon.reload range";
                if(!emit(std::move(op)))return false;awaitingRandom_=true;output.expectsRandomReply=true;return true;}
            w->reloadTimeRemaining=w->definition.reloadTime;phase_=w->reloadTimeRemaining<=0?2:5;continue;
        case 1:error="Native reload RNG reply missing";return false;
        case 2:
            w->clipShotsRemaining=w->definition.clipSize;
            if(!owner.exists){phase_=5;continue;}
            ++phase_;continue;
        case 3:
            if(!w->definition.swapOnReloadFinish.empty()&&!arsenal_->swapWeaponControl(name_,w->definition.swapOnReloadFinish,owner,error))return false;
            ++phase_;continue;
        case 4:{WeaponOutput op;op.kind=WeaponOutputKind::SetOwnerAnimation;op.name=w->definition.reloadFinishGraphic;
            return emit(std::move(op));}
        case 5:next.progress=WeaponReloadProgress::Finished;output=next;finished_=true;return true;
        }
    }
    error="Invalid native reload phase";return false;
}
}

namespace hades {
WeaponBlinkProjectile::WeaponBlinkProjectile(WeaponArsenal& arsenal,std::string name,float x,float y,float angle,WeaponFireRequest request)
    :arsenal_(&arsenal),name_(std::move(name)),x_(x),y_(y),angle_(angle),request_(request){}
bool WeaponBlinkProjectile::advance(const WeaponOwnerFrame& owner,const WeaponBlinkReply& reply,const WeaponOutputSupport& support,
        WeaponOutput& operation,bool& finished,bool& expectsRandom,bool& expectsProjectile,std::string& error){
    // BlinkWeapon::FireProjectile RVA7e4f0 (LLVM .local/reports/native-combat2-blink-fire-projectile-static.json).
    // WeaponData offsets (+0x50 = WeaponDataDef): NumProjectiles 0x448, BonusProjectileChance
    // 0x450, NumProjectilesIsAmmo 0x454, ProjectileOffset 0x45c, ProjectileAngleStartOffset
    // 0x460, ProjectileAngleOffset/Min/Max 0x468/0x46c/0x470, ProjectileAngleOffsetScaleY
    // 0x474, ProjectileSpacing 0x478, ProjectileInterval 0x47c, ProjectileOffsetStart 0x480,
    // Spread 0x498. Create(projData, this, loc, request{angle=shot}, Spread, i, volley -1, 0)
    // then Projectile+0x3b2 mUseStartMarker = false.
    error.clear();finished=false;expectsRandom=expectsProjectile=false;
    const auto* w=arsenal_->weaponState(name_,error);if(!w)return false;
    const auto& d=w->definition;
    auto number=[&](const char* key,float& v){auto f=d.fields.find(key);if(f==d.fields.end()||!std::holds_alternative<double>(f->second)){error=std::string("Missing BlinkWeapon::FireProjectile number ")+key;return false;}v=float(std::get<double>(f->second));if(!std::isfinite(v)){error="Nonfinite Blink projectile field";return false;}return true;};
    auto flag=[&](const char* key,bool& v){auto f=d.fields.find(key);if(f==d.fields.end()||!std::holds_alternative<bool>(f->second)){error=std::string("Missing BlinkWeapon::FireProjectile flag ")+key;return false;}v=std::get<bool>(f->second);return true;};
    constexpr float tau=6.2831854820251465f;
    auto wrap=[&](float a){return a-std::floor(a/tau)*tau;}; // fmodf(a,tau) path at VA18007e768..777 (a>=0 after fmod sign fix)
    auto emit=[&](WeaponOutput op){op.ownerId=owner.ownerId;op.weaponName=name_;
        if(!support||!support(op,error)){if(error.empty())error="Unsupported BlinkWeapon::FireProjectile operation";return false;}
        operation=std::move(op);return true;};
    if(await_==Await::None){if(reply.randomDouble||reply.createdProjectile){error="Unrequested BlinkWeapon::FireProjectile reply";return false;}}
    else if(await_==Await::Projectile){
        if(!reply.createdProjectile||!*reply.createdProjectile||reply.randomDouble){error="BlinkWeapon::FireProjectile awaits actual projectile handle";return false;}
        await_=Await::None;++index_;phase_=1;
    }else{
        if(!reply.randomDouble||reply.createdProjectile||!std::isfinite(*reply.randomDouble)||*reply.randomDouble<0||*reply.randomDouble>=1){error="BlinkWeapon::FireProjectile awaits one RandomDouble";return false;}
        const double r=*reply.randomDouble;
        if(await_==Await::Bonus){
            float chance=0;if(!number("BonusProjectileChance",chance))return false;
            if(double(chance)>=r)++count_; // comisd chance,r ; cmovae VA18007e58c
            await_=Await::None;index_=0;phase_=1;
        }else{
            float offset=0,mn=0,mx=0,start=0,distance=0;
            if(!number("ProjectileAngleOffset",offset)||!number("ProjectileAngleOffsetMin",mn)||!number("ProjectileAngleOffsetMax",mx)||!number("ProjectileAngleStartOffset",start)||!number("ProjectileOffset",distance))return false;
            // VA18007e6da..7a0: a = rnd*(max-min)+min + (k*offset*sign + angle+start);
            // loc += (cos a, -sin a) * ProjectileOffset.
            float a=float(r)*(mx-mn)+mn+(k_*offset*sign_+(angle_+start));
            shotAngle_=a;shotX_+=std::cos(a)*distance;shotY_+=-std::sin(a)*distance;
            await_=Await::None;phase_=2;
        }
    }
    while(true){
        float f=0;bool b=false;
        switch(phase_){
        case 0:{
            if(!w->validProjectileData){finished=true;return true;}
            if(!number("NumProjectiles",f))return false;if(f<0||f>4096||std::floor(f)!=f){error="BlinkWeapon::FireProjectile count outside bounded subset";return false;}
            count_=int32_t(f);
            if(!flag("NumProjectilesIsAmmo",b))return false;
            if(b){int32_t ammo=0;if(!arsenal_->getAmmo(name_,ammo,error))return false;count_=ammo;}
            // RandomDouble54f80 is drawn unconditionally (VA18007e584).
            WeaponOutput op;op.kind=WeaponOutputKind::RequestRandomDouble;op.name="Blink bonus projectile chance";
            if(!emit(std::move(op)))return false;await_=Await::Bonus;expectsRandom=true;return true;
        }
        case 1:{
            if(index_>=count_){finished=true;return true;}
            float offset=0,mn=0,mx=0,start=0,scaleY=0,interval=0,spread=0;
            if(!number("ProjectileAngleOffset",offset)||!number("ProjectileAngleOffsetMin",mn)||!number("ProjectileAngleOffsetMax",mx)||!number("ProjectileAngleStartOffset",start)||!number("ProjectileAngleOffsetScaleY",scaleY)||!number("ProjectileInterval",interval)||!number("Spread",spread))return false;
            if(scaleY>0){error="BlinkWeapon::FireProjectile ProjectileAngleOffsetScaleY speed rewrite not reconstructed";return false;}
            if(interval>0){error="BlinkWeapon::FireProjectile PauseForDuration6d390 not reconstructed";return false;}
            if(spread!=0){error="BlinkWeapon::FireProjectile nonzero Spread Create argument not reconstructed";return false;}
            // ProjectileOffsetStart (+0x480) selects k/sign (VA18007e60b..64d). For one
            // projectile k=0 in every mode, so the unresolved enum ordering is irrelevant.
            if(count_>1){error="BlinkWeapon::FireProjectile ProjectileOffsetStart enum ordering unresolved for multiple projectiles";return false;}
            k_=0;sign_=-1;shotX_=x_;shotY_=y_;
            if(offset==0&&mn==0&&mx==0){shotAngle_=angle_+start;phase_=2;continue;}
            WeaponOutput op;op.kind=WeaponOutputKind::RequestRandomDouble;op.name="Blink projectile angle offset";
            if(!emit(std::move(op)))return false;await_=Await::Offset;expectsRandom=true;return true;
        }
        case 2:{
            float spacing=0;if(!number("ProjectileSpacing",spacing))return false;
            if(spacing!=0){const float perp=wrap(shotAngle_+1.5707963705062866f);const float s=k_*spacing*sign_;shotX_+=std::cos(perp)*s;shotY_+=-std::sin(perp)*s;}
            WeaponOutput op;op.kind=WeaponOutputKind::ProjectileSpawn;op.name=w->projectileData.name;
            op.x=shotX_;op.y=shotY_;op.z=owner.z;op.angle=shotAngle_;op.projectile=w->projectileSource;
            WeaponFireRequest r=request_;r.angle=shotAngle_;op.fireRequest=r;
            op.projectileIndex=index_;op.volleyIndex=-1;op.clearUseStartMarker=true;
            if(!emit(std::move(op)))return false;await_=Await::Projectile;expectsProjectile=true;return true;
        }
        default:error="Invalid BlinkWeapon::FireProjectile phase";return false;
        }
    }
}
}
