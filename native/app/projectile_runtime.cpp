#include "projectile_runtime.hpp"
#include <cmath>
#include <algorithm>
#include <limits>
#include <utility>

namespace hades {
bool describeProjectileCreation(const ProjectileDefinition& definition,ProjectileCreationDescriptor& output,std::string& error){
    // Matching DLL d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254.
    // ProjectileManager::Create RVA79d40 switch at VA180079e8e uses full
    // ProjectileData+2d8; exact enum values from PDB TypeIndex dc54. V6 keeps
    // native constructor defaults/post-overrides and explicit source values.
    error.clear();ProjectileCreationDescriptor staged;
    auto integer=[&](const char* key,int max,int& value){
        auto f=definition.fields.find(key);
        if(f==definition.fields.end()||!std::holds_alternative<double>(f->second)){error=std::string("Missing typed native projectile field ")+key;return false;}
        const double number=std::get<double>(f->second);
        if(!std::isfinite(number)||number<0||number>max||std::floor(number)!=number){error=std::string("Unsupported native projectile enum ")+key;return false;}
        value=static_cast<int>(number);return true;
    };
    int type=0,unit=0,obstacle=0;
    if(!integer("Type",6,type)||!integer("UnitCollisionCheck",5,unit)||!integer("ObstacleCollisionCheck",5,obstacle))return false;
    staged.concreteClass=static_cast<ProjectileClass>(type);
    staged.unitCollisionCheck=static_cast<ProjectileCollisionCheck>(unit);
    staged.obstacleCollisionCheck=static_cast<ProjectileCollisionCheck>(obstacle);
    // Base/ARC vtable slot160 -> Thing::Defaults RVA2dbbb0. HOMING slot160
    // -> HomingProjectile::Defaults RVA65450 -> Thing::Defaults. No public ID
    // allocation/write occurs in the covered Create/Init paths. Other subclass
    // initializers have not been audited for identity/side effects here.
    // INSTANT vtable RVA5d86e8 likewise uses Thing::Defaults; its Init65f90
    // invokes base Init before the derived synchronous detonation/target pass.
    switch(staged.concreteClass){
    case ProjectileClass::Straight:case ProjectileClass::Arc:case ProjectileClass::Instant:
        staged.usesBaseProjectileInit=true;break;
    case ProjectileClass::Homing:
        staged.usesBaseProjectileInit=true;staged.requiresHomingDefaults=true;break;
    default:error="Native projectile subclass-specific initialization not reconstructed";return false;
    }
    output=staged;return true;
}
}

namespace hades {
ProjectileInitialization::ProjectileInitialization(ProjectileInitializationRequest request)
    :request_(std::move(request)){}
bool ProjectileInitialization::advance(const ProjectileDefinition& definition,
        const ProjectileInitializationFrame& frame,std::optional<double> randomReply,
        const ProjectileInitializationSupport& support,ProjectileInitializationStep& output,std::string& error){
    // Matching Projectile::Init RVA69910. Complete retained public REA evidence
    // ev_b9f9777ef7cf335c9a82e24fd15d507d86f839ad6b4e0864ebb0b4a39bba5d92.
    // The split is the native ClearOnAttackEffects -> GUN-mask test boundary.
    // State below is never an active World object; parent owns Thing/Body/link
    // lifetime, real synchronous effects and later PROJECTILES_TO_ADD enqueue.
    error.clear();ProjectileInitializationStep next;
    auto number=[&](const char* key,float& value){
        auto f=definition.fields.find(key);
        if(f==definition.fields.end()||!std::holds_alternative<double>(f->second)){error=std::string("Missing native ProjectileData numeric field ")+key;return false;}
        const double n=std::get<double>(f->second);value=static_cast<float>(n);
        if(!std::isfinite(n)||!std::isfinite(value)){error=std::string("Invalid native ProjectileData numeric field ")+key;return false;}return true;
    };
    auto integer=[&](const char* key,int32_t& value){float n=0;if(!number(key,n))return false;
        if(n<static_cast<double>(INT32_MIN)||n>static_cast<double>(INT32_MAX)||std::trunc(n)!=n){error=std::string("Invalid native ProjectileData integer field ")+key;return false;}value=static_cast<int32_t>(n);return true;};
    auto flag=[&](const char* key,bool& value){auto f=definition.fields.find(key);
        if(f==definition.fields.end()||!std::holds_alternative<bool>(f->second)){error=std::string("Missing native ProjectileData flag ")+key;return false;}
        value=std::get<bool>(f->second);return true;};
    auto text=[&](const char* key,std::string& value){auto f=definition.fields.find(key);
        if(f==definition.fields.end()||!std::holds_alternative<std::string>(f->second)){error=std::string("Missing native ProjectileData string ")+key;return false;}
        value=std::get<std::string>(f->second);return true;};
    auto yield=[&](ProjectileInitializationOperationKind kind,const std::string& name={},float value=0,float start=0,float duration=0,bool bit=false){
        ProjectileInitializationOperation op;op.kind=kind;op.lifetimeHandle=request_.lifetimeHandle;
        op.ownerId=request_.ownerId;op.projectileName=definition.name;op.weaponName=request_.weaponName;op.name=name;
        op.x=request_.x;op.y=request_.y;op.value=value;op.start=start;op.duration=duration;op.flag=bit;
        if(!support){error="Projectile Init has no synchronous operation consumer";return false;}
        if(!support(op,error)){if(error.empty())error="Projectile Init operation unsupported: "+name;return false;}
        next.operation=std::move(op);next.progress=ProjectileInitializationProgress::Yielded;
        output=std::move(next);++phase_;return true;
    };
    if(!request_.lifetimeHandle||request_.weaponName.empty()||definition.name.empty()||request_.ownerId<0||
       !std::isfinite(request_.x)||!std::isfinite(request_.y)||!std::isfinite(request_.shotAngle.value_or(request_.fire.angle))||!std::isfinite(frame.worldTime)){
        error="Invalid native Projectile Init request/frame";return false;
    }
    if(!definitionName_.empty()&&definition.name!=definitionName_){error="Projectile Init data identity changed";return false;}
    if(phase_>0&&!frame.thingInitialized){error="Projectile Init resumed before actual Thing::Init completed";return false;}
    if(phase_>0&&!frame.hasBody){error="Projectile Init requires actual BodyComponent";return false;}
    if(randomReply&&!waitingRandom_){error="Unrequested projectile random reply";return false;}
    if(waitingRandom_){
        if(!randomReply){error="Projectile Init awaits its synchronous RandomDouble result";return false;}
        if(!std::isfinite(*randomReply)||*randomReply<0||*randomReply>=1){error="Invalid native RandomDouble result";return false;}
        const float draw=static_cast<float>(*randomReply);
        if(phase_==13)state_.speed=(draw*(speedVariance_-(-speedVariance_))+(-speedVariance_))+speedBase_;
        else if(phase_==16)state_.fuseRemaining=draw*(fuseMax_-fuseMin_)+fuseMin_;
        else {error="Invalid Projectile Init random continuation";return false;}
        waitingRandom_=false;
    }
    while(phase_<23){
        float f=0,g=0;bool b=false;std::string s;
        switch(phase_){
        case 0:{
            ProjectileCreationDescriptor descriptor;
            if(!describeProjectileCreation(definition,descriptor,error))return false;
            if(request_.fire.targetId!=-1&&!request_.fire.targetLifetime){error="Projectile Init target entity-generation link unavailable";return false;}
            if(!yield(ProjectileInitializationOperationKind::ThingInitialize,"Thing::Init(null map,initialize=true)"))return false;
            definitionName_=definition.name;state_.lifetimeHandle=request_.lifetimeHandle;
            state_.publicThingId=descriptor.publicThingId;return true;
        }
        case 1:
            state_.spawnAngle=request_.shotAngle.value_or(request_.fire.angle);
            if(frame.hasPhysics)return yield(ProjectileInitializationOperationKind::SetPhysicsIgnoreGravity,"mIgnoreGravity",0,0,0,true);
            ++phase_;continue;
        case 2:{
            if(!frame.ownerExists){error="Covered Projectile Init requires live firing Unit owner";return false;}
            ProjectileInitialState staged=state_;
            staged.fireIndex=request_.fireIndex;staged.volleyIndex=request_.volleyIndex;staged.waveIndex=0;
            staged.lastJumpTime=frame.worldTime;staged.ownerId=request_.ownerId;
            staged.ownerSpawnedById=frame.ownerSpawnedById;
            if(!flag("IgnoreOwnerAllegiance",b)||!number("StartDelay",staged.startDelay)||!number("AlphaOverride",f))return false;
            staged.firedByTraitor=!b&&frame.ownerTraitor;staged.hasStarted=staged.startDelay<=0;
            // PDB AdjustArgs target0,start4,duration8. Native BodyComponent
            // alpha transition helper is Body+800; smooth/ease/ping/phase=0.
            const float target=staged.hasStarted?(f==0?1:f):0;
            if(!yield(ProjectileInitializationOperationKind::AdjustBodyAlpha,"alpha",target,1,staged.hasStarted?0.2f:0))return false;
            state_=staged;state_.useStartMarker=true;return true;
        }
        case 3:
            if(!flag("OffsetFromOwnerZ",b))return false;
            if(b){if(!frame.ownerExists||!std::isfinite(frame.ownerTotalZ)){error="Projectile OffsetFromOwnerZ lacks live owner total Z";return false;}
                return yield(ProjectileInitializationOperationKind::AddOwnerZ,"owner total Z",frame.ownerTotalZ);}
            ++phase_;continue;
        case 4:return yield(ProjectileInitializationOperationKind::RetainFiringWeapon,"native firing weapon reference");
        case 5:
            if(!request_.groupName.empty())return yield(ProjectileInitializationOperationKind::AddToGroup,request_.groupName);
            ++phase_;continue;
        case 6:return yield(ProjectileInitializationOperationKind::SetWeaponLastFirePerfectCharge,"mWasLastFirePerfectCharge",0,0,0,frame.weaponPerfectCharge);
        case 7:
            if(!text("PerfectChargeGraphic",s))return false;
            if(!s.empty()&&frame.weaponPerfectCharge)return yield(ProjectileInitializationOperationKind::SetAnimation,s);
            ++phase_;continue;
        case 8:
            if(!text("FinalShotGraphic",s))return false;
            if(!s.empty()&&!frame.weaponHasAmmo)return yield(ProjectileInitializationOperationKind::SetAnimation,s);
            ++phase_;continue;
        case 9:
            if(!text("AttachedPowershotAnimation",s))return false;
            if(!s.empty()&&frame.weaponPerfectCharge)return yield(ProjectileInitializationOperationKind::CreateAttachedAnimation,s);
            ++phase_;continue;
        case 10:
            if(!flag("DrawAsBeam",b)||!number("BlastSpeed",f))return false;
            if(!b&&f<=0){if(!number("DamageRadius",g))return false;return yield(ProjectileInitializationOperationKind::SetAttachedAnimationRadius,"all attached animations",g);}
            ++phase_;continue;
        case 11:
            ++phase_;continue;
        case 12:{
            if(!number("Speed",f)||!number("SpeedVariance",g))return false;
            if(!yield(ProjectileInitializationOperationKind::RequestRandomDouble,"projectile speed variance"))return false;
            speedBase_=f;speedVariance_=g;waitingRandom_=true;return true;
        }
        case 13:{
            ProjectileInitialState staged=state_;
            if(!integer("NumBounces",staged.numBounces)||!integer("NumJumps",staged.numJumps)||!integer("NumPenetrations",staged.numPenetrations)||
               !number("Range",staged.range)||!number("MaxSpeed",staged.maxSpeed)||!number("Acceleration",staged.acceleration))return false;
            staged.rangeSquared=staged.range*staged.range;staged.hitMaxRange=false;
            staged.blastTimeRemaining=0;staged.blastRadiusModifier=1;state_=staged;++phase_;continue;
        }
        case 14:{
            float lo=0,hi=0;
            if(!number("Fuse",f)||!number("FuseMin",lo)||!number("FuseMax",hi)||!flag("StartFuseOnTouchdown",b))return false;
            if((f<0&&(lo==-1||hi==-1))||b){state_.fuseRemaining=std::numeric_limits<float>::max();phase_=17;continue;}
            if(lo==-1||hi==-1){state_.fuseRemaining=f;phase_=16;continue;}
            // GetFuse stores min/max on its native stack before RandomDouble.
            fuseMin_=lo;fuseMax_=hi;++phase_;continue;
        }
        case 15:
            if(!yield(ProjectileInitializationOperationKind::RequestRandomDouble,"projectile fuse range"))return false;
            waitingRandom_=true;return true;
        case 16:
            if(!number("BlastSpeed",f))return false;
            if(f>0)return yield(ProjectileInitializationOperationKind::SetAttachedAnimationRadius,"all attached animations",0.01f);
            ++phase_;continue;
        case 17:
            if(!number("TotalFuse",f)||!number("MoveInterval",g))return false;
            state_.lifeRemaining=f<=0?std::numeric_limits<float>::max():f;state_.moveIntervalRemaining=g;
            ++phase_;continue;
        case 18:return yield(ProjectileInitializationOperationKind::SetupGeometry,"Thing::SetupGeometry");
        case 19:
            // Virtual slot128 is Thing::SetLocation(Vector2 const&,bool).
            // Native argument is actual current Thing.mLocation, not the raw
            // launch point (Thing::Init may apply offsets). Parent reads it.
            return yield(ProjectileInitializationOperationKind::RefreshLocation,"SetLocation(current local,false)");
        case 20:return yield(ProjectileInitializationOperationKind::SetAngle,"Thing::SetAngle",request_.shotAngle.value_or(request_.fire.angle));
        case 21:
            if(!flag("ClearOnAttackEffects",b))return false;
            if(b&&frame.ownerExists)return yield(ProjectileInitializationOperationKind::ClearOnAttackEffects,"UnitStatus::ClearOnAttackEffects");
            ++phase_;continue;
        case 22:
            next.progress=ProjectileInitializationProgress::BeforeWeaponModifiers;output=next;++phase_;return true;
        }
    }
    error="Projectile Init prefix already finished; GUN modifiers/start FX/attachment/manager enqueue remain mandatory";return false;
}
}

namespace hades {
bool applyProjectileGunModifiers(ProjectileInitialState& state,const WeaponSnapshot& weapon,
        const ProjectileGunOwnerFrame& owner,float& bodyScale,std::string& error){
    // Init REA ev_b9f9777ef7cf335c9a82e24fd15d507d86f839ad6b4e0864ebb0b4a39bba5d92;
    // calcRangeMultiplier RVA7f870, calcSpeedMultiplier RVA7f960 bounded PDB.
    error.clear();if(weapon.definition.type!=0){error="Projectile GUN modifier requires actual GUN subclass";return false;}
    if(!owner.exists||weapon.ownerId!=state.ownerId||!std::isfinite(owner.damageOutputMultiplier)||
       !std::isfinite(owner.damageOutputModifier)||!std::isfinite(owner.projectileSpeedMultiplier)||
       !std::isfinite(owner.perfectChargeBonus)||!std::isfinite(weapon.chargeTimeRemaining)){
        error="Projectile modifier lacks actual current live owner/weapon fields";return false;
    }
    auto number=[&](const char* key,float& value){auto f=weapon.definition.fields.find(key);
        if(f==weapon.definition.fields.end()||!std::holds_alternative<double>(f->second)){error=std::string("Missing native GUN modifier field ")+key;return false;}
        value=static_cast<float>(std::get<double>(f->second));if(!std::isfinite(value)){error="Nonfinite GUN modifier data";return false;}return true;};
    float damage=0,range=0,speed=0,scale=0,duration=0,perfectDamage=0,perfectRange=0,perfectSpeed=0,speedMultiplier=0,chargeTime=0;
    if(!number("ChargeDamageMultiplier",damage)||!number("ChargeRangeMultiplier",range)||
       !number("ChargeSpeedMultiplier",speed)||!number("ChargeScaleMultiplier",scale)||
       !number("PerfectChargeWindowDuration",duration)||!number("PerfectChargeDamageMultiplier",perfectDamage)||
       !number("PerfectChargeRangeMultiplier",perfectRange)||!number("PerfectChargeSpeedMultiplier",perfectSpeed)||
       !number("SpeedMultiplier",speedMultiplier)||!number("ChargeTime",chargeTime))return false;
    const float fraction=chargeTime==0?1:std::clamp(1.0f-weapon.chargeTimeRemaining/chargeTime,0.0f,1.0f);
    auto interpolation=[&](float m){return m>0?(m-1.0f)*fraction+1.0f:1.0f;};
    const bool perfect=duration>0&&owner.perfectCharge;
    float damageMultiplier=owner.damageOutputMultiplier*owner.damageOutputModifier;
    damageMultiplier*=interpolation(damage);
    if(perfect&&perfectDamage>0){damageMultiplier*=perfectDamage;damageMultiplier*=owner.perfectChargeBonus;}
    // Native perfect-range branch deliberately uses the raw ChargeRangeMultiplier
    // rather than interpolation, even if it is zero. Do not repair source data.
    float rangeMultiplier=perfect&&perfectRange>0?(perfectRange*range)*owner.perfectChargeBonus:interpolation(range);
    float velocityMultiplier=speedMultiplier>0?speedMultiplier:1;
    velocityMultiplier*=interpolation(speed);
    if(perfect&&perfectSpeed>0)velocityMultiplier*=perfectSpeed*owner.perfectChargeBonus;
    if(owner.projectileSpeedMultiplier!=0&&owner.projectileSpeedMultiplier!=1)velocityMultiplier*=owner.projectileSpeedMultiplier;
    auto staged=state;staged.damageMultiplier*=damageMultiplier;
    staged.range*=rangeMultiplier;staged.rangeSquared=staged.range*staged.range;
    staged.maxSpeed*=velocityMultiplier;staged.acceleration*=velocityMultiplier;staged.speed*=velocityMultiplier;
    if(staged.maxSpeed<staged.speed)staged.speed=staged.maxSpeed;
    const float newScale=interpolation(scale);
    state=staged;bodyScale=newScale;return true;
}
}

namespace hades {
ProjectileInitializationTail::ProjectileInitializationTail(ProjectileInitializationRequest request,ProjectileInitialState state)
    :request_(std::move(request)),state_(std::move(state)){}
bool ProjectileInitializationTail::advance(const ProjectileDefinition& definition,const WeaponSnapshot& weapon,
        const ProjectileInitializationTailFrame& frame,const ProjectileInitializationSupport& support,
        ProjectileInitializationTailStep& output,std::string& error){
    // Native Init tail and manager Create are separate functions. The queue
    // append below belongs AFTER Init, before inheritance/group-copy, exactly
    // as Create VA18007ac00..7acc5. Pending is not active ALL_PROJECTILES.
    error.clear();ProjectileInitializationTailStep next;
    auto number=[&](const char* key,float& value){auto f=definition.fields.find(key);
        if(f==definition.fields.end()||!std::holds_alternative<double>(f->second)){error=std::string("Missing projectile tail number ")+key;return false;}
        value=static_cast<float>(std::get<double>(f->second));if(!std::isfinite(value)){error="Nonfinite projectile tail data";return false;}return true;};
    auto flag=[&](const char* key,bool& value){auto f=definition.fields.find(key);
        if(f==definition.fields.end()||!std::holds_alternative<bool>(f->second)){error=std::string("Missing projectile tail flag ")+key;return false;}
        value=std::get<bool>(f->second);return true;};
    auto text=[&](const char* key,std::string& value){auto f=definition.fields.find(key);
        if(f==definition.fields.end()||!std::holds_alternative<std::string>(f->second)){error=std::string("Missing projectile tail string ")+key;return false;}
        value=std::get<std::string>(f->second);return true;};
    auto yield=[&](ProjectileInitializationOperationKind kind,const std::string& name={},float value=0){
        ProjectileInitializationOperation op;op.kind=kind;op.lifetimeHandle=request_.lifetimeHandle;
        op.ownerId=request_.ownerId;op.projectileName=definition.name;op.weaponName=request_.weaponName;op.name=name;
        op.x=frame.worldX;op.y=frame.worldY;op.value=value;
        if(!support){error="Projectile tail has no synchronous consumer";return false;}
        if(!support(op,error)){if(error.empty())error="Unsupported projectile tail operation: "+name;return false;}
        next.progress=ProjectileInitializationTailProgress::Yielded;next.operation=std::move(op);output=std::move(next);++phase_;return true;
    };
    if(!request_.lifetimeHandle||state_.lifetimeHandle!=request_.lifetimeHandle||state_.ownerId!=request_.ownerId||
       weapon.ownerId!=request_.ownerId||weapon.definition.name!=request_.weaponName||definition.name.empty()||
       !std::isfinite(frame.worldX)||!std::isfinite(frame.worldY)||!std::isfinite(frame.spawnX)||!std::isfinite(frame.spawnY)){
        error="Projectile tail lacks matching initialized instance/current firing weapon/transform";return false;
    }
    while(phase_<18){
        float f=0,g=0,h=0;bool b=false;std::string s;
        switch(phase_){
        case 0:{
            // Init gate (~Weapon+8 & 0x44)==0 (native-player-projectile-init-pseudocode.c l.278):
            // GunWeapon only. BlinkWeapon objects carry typeflags 0x24 (Weapon::Weapon(edx=0x24) in
            // vector<BlinkWeapon>::DoInsertValuesEnd VA18007f040, native-combat4-blink-pool-insert-static.json),
            // so charge modifiers, Body scale MarkDirty and that SetupGeometry are skipped.
            if(weapon.definition.type==1){phase_=2;continue;}
            auto staged=state_;float bodyScale=0;
            if(!applyProjectileGunModifiers(staged,weapon,frame.gunOwner,bodyScale,error))return false;
            if(!yield(ProjectileInitializationOperationKind::SetBodyScale,"Body.mScale + MarkDirty",bodyScale))return false;
            state_=staged;return true;
        }
        case 1:return yield(ProjectileInitializationOperationKind::SetupGeometry,"Thing::SetupGeometry after charge scale");
        case 2:
            if(!number("HasActiveBlinkTriggers",f)||!number("ActiveBlinkSpeedMultiplier",g)||!number("ActiveBlinkRangeMultiplier",h))return false;
            if(f>0||g>0||h>0){error="Projectile active RAM weapon iteration/modifiers require actual owner Arsenal view";return false;}
            ++phase_;continue;
        case 3:
            if(state_.hasStarted){
                if(!text("StartAtMarker",s))return false;
                if(state_.useStartMarker&&!s.empty())return yield(ProjectileInitializationOperationKind::ApplyStartMarker,s);
            }
            ++phase_;continue;
        case 4:case 5:
            if(state_.hasStarted&&state_.fireIndex==0){
                if(!text(phase_==4?"StartFx":"StartFx2",s))return false;
                // Native attempts even empty/literal-null Create, and calls
                // PreferUnmodifiedTime only for a real returned Animation.
                return yield(ProjectileInitializationOperationKind::CreateStartAnimation,s);
            }
            ++phase_;continue;
        case 6:
            // Projectile.Init69910 resolves FireRequest EntityLinkedObject
            // before reading target location; invalid generation uses the
            // captured target vector instead. Life death is not entity death.
            if(request_.fire.targetId!=-1&&!frame.targetLinkChecked){error="Projectile target entity location requires live parent resolver";return false;}
            state_.targetX=frame.targetExists?frame.targetX:request_.fire.targetX;state_.targetY=frame.targetExists?frame.targetY:request_.fire.targetY;
            if(!number("Range",f))return false;
            if(state_.targetX==0&&state_.targetY==0&&f>0){
                state_.targetX=std::cos(request_.shotAngle.value_or(request_.fire.angle))*f+frame.spawnX;
                state_.targetY=-(std::sin(request_.shotAngle.value_or(request_.fire.angle))*f)+frame.spawnY;
            }
            ++phase_;continue;
        case 7:
            if(!flag("AttachToOwner",b))return false;
            if(b)return yield(ProjectileInitializationOperationKind::AttachToOwner,"Weapon::GetFireOffset -> Thing::Attach + attach-offset-Z");
            ++phase_;continue;
        case 8:
            if(!flag("MatchOwnerBody",b))return false;
            if(b)return yield(ProjectileInitializationOperationKind::MatchOwnerBody,"owner scale/flips + MarkDirty + SetupGeometry");
            ++phase_;continue;
        case 9:
            if(!flag("SetAnimRadialOffset",b))return false;
            if(b){error="Projectile per-animation radial offsets require actual attached-animation iteration and per-entry RandomDouble barriers";return false;}
            ++phase_;continue;
        case 10:{
            ProjectileCreationDescriptor descriptor;
            if(!describeProjectileCreation(definition,descriptor,error))return false;
            if(descriptor.concreteClass==ProjectileClass::Homing)
                return yield(ProjectileInitializationOperationKind::FinalizeHomingTargetLink,"FireRequest target entity link copy");
            ++phase_;continue;
        }
        case 11:
            return yield(ProjectileInitializationOperationKind::QueuePendingProjectile,"PROJECTILES_TO_ADD");
        case 12:
            if(!flag("InheritOwnerElapsedTimeMultiplier",b))return false;
            if(b&&frame.gunOwner.exists){
                if(!std::isfinite(frame.ownerElapsedTimeMultiplier)){error="Projectile inherited time multiplier invalid";return false;}
                return yield(ProjectileInitializationOperationKind::InheritElapsedTimeMultiplier,"Thing.mElapsedTimeMultiplier + attached Animation time",frame.ownerElapsedTimeMultiplier);
            }
            ++phase_;continue;
        case 13:
            if(frame.gunOwner.exists)return yield(ProjectileInitializationOperationKind::CopyOwnerGroups,"Thing::AddToGroups(owner membership)");
            ++phase_;continue;
        case 14:
            if(!number("ProjectileNumOpacityMultiplier",f))return false;
            if(f>0)return yield(ProjectileInitializationOperationKind::SetProjectileNumOpacityFlag,"ProjectileManager global opacity-dirty",1);
            ++phase_;continue;
        case 15:
            next.progress=ProjectileInitializationTailProgress::PendingQueueSetupFinished;output=next;phase_=18;return true;
        default:error="Invalid projectile tail continuation";return false;
        }
    }
    error="Projectile pending-queue setup already finished; active update/collision/lifetime are separate";return false;
}
}

namespace hades {
bool initialArcProjectileAngle(const ProjectileDefinition& data,float& output,std::string& error){
    error.clear();auto t=data.fields.find("Type"),a=data.fields.find("ArcStart");
    if(t==data.fields.end()||!std::holds_alternative<double>(t->second)||std::get<double>(t->second)!=5||
       a==data.fields.end()||!std::holds_alternative<double>(a->second)){
        error="Native ARC initialization requires actual ARC definition/ArcStart";return false;
    }
    const float value=static_cast<float>(std::get<double>(a->second));
    if(!std::isfinite(value)){error="Nonfinite native ARC start angle";return false;}
    output=value;return true;
}
bool planArcProjectileMove(const ProjectileDefinition& data,float current,const ArcProjectileMoveFrame& frame,
        ArcProjectileMovePlan& output,std::string& error){
    // PDB-bounded native-player-arc-move.json VA180062489..626b7;
    // mAngleOffset510, mRange3d0, mSpawnAngle3f0, mSpawnLocation90.
    // ArcStart/End are already native radians in source packet v10+.
    error.clear();ArcProjectileMovePlan next;float start=0;
    if(!initialArcProjectileAngle(data,start,error))return false;
    auto number=[&](const char* key,float& value){auto i=data.fields.find(key);
        if(i==data.fields.end()||!std::holds_alternative<double>(i->second)){error=std::string("Missing native ARC field ")+key;return false;}
        value=static_cast<float>(std::get<double>(i->second));if(!std::isfinite(value)){error="Nonfinite native ARC field";return false;}return true;};
    auto attached=data.fields.find("AttachToOwner");
    if(attached==data.fields.end()||!std::holds_alternative<bool>(attached->second)){error="Missing native ARC AttachToOwner flag";return false;}
    float speed=0,end=0;if(!number("Speed",speed)||!number("ArcEnd",end))return false;
    for(float value:{current,frame.dt,frame.worldTime,frame.creationTime,frame.spawnX,frame.spawnY,frame.range,frame.spawnAngle}){
        if(!std::isfinite(value)){error="Nonfinite native ARC movement frame";return false;}
    }
    if(frame.dt<0){error="Negative native ARC movement delta";return false;}
    next.nextAngleOffset=current;
    if(frame.worldTime>frame.creationTime){
        const float rate=static_cast<float>(static_cast<double>(std::abs(speed))*0.017453292519943295);
        const float distance=rate*frame.dt,remaining=std::abs(end-current);
        if(distance>=remaining)next.nextAngleOffset=end;
        else if(end>current)next.nextAngleOffset=current+distance;
        else if(current>end)next.nextAngleOffset=current-distance;
    }
    if(std::get<bool>(attached->second)){
        if(!frame.ownerExists){next.nativeShouldRemove=true;output=next;return true;}
        if(!std::isfinite(frame.ownerX)||!std::isfinite(frame.ownerY)||(frame.ownerHasBody&&!std::isfinite(frame.ownerBodyAngle))){error="Nonfinite native ARC current owner transform";return false;}
        next.x=frame.ownerX;next.y=frame.ownerY;
        next.requestedAngle=(frame.ownerHasBody?frame.ownerBodyAngle:0)+next.nextAngleOffset;
    }else{
        const float angle=next.nextAngleOffset+frame.spawnAngle;
        next.x=std::cos(angle)*frame.range+frame.spawnX;
        next.y=-std::sin(angle)*frame.range+frame.spawnY;
        const float dx=next.x-frame.spawnX,dy=-(next.y-frame.spawnY);
        const float raw=std::atan2(dy,dx),twoPi=6.283185482025146484375f;
        next.requestedAngle=raw-std::floor(raw/twoPi)*twoPi;
    }
    if(!std::isfinite(next.nextAngleOffset)||!std::isfinite(next.x)||!std::isfinite(next.y)||!std::isfinite(next.requestedAngle)){error="Native ARC movement exceeds finite portable domain";return false;}
    next.hasTransform=true;output=next;return true;
}
}
