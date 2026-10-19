#include "projectile_world.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace hades {
namespace {
const SourceValue& actorProperty(const ActorWorld& world,int32_t id,const char* key){const auto* v=world.currentProperty(id,key);if(!v)throw std::runtime_error(std::string("Actual projectile collision owner property unavailable: ")+key);return *v;}
const ProjectileDefinition& currentData(const ProjectileActor& p){std::string e;if(!p.firingArsenal)throw std::runtime_error("Projectile collision source Arsenal unavailable");const auto* d=p.firingArsenal->projectileData(p.request.weaponName,p.spawnedDefinition,e);if(!d)throw std::runtime_error(e);return *d;}
const WeaponScalar& property(const ProjectileDefinition& d,const char* key){const auto it=d.fields.find(key);if(it==d.fields.end())throw std::runtime_error(std::string("Actual projectile collision property unavailable: ")+key);return it->second;}
bool boolean(const ProjectileDefinition& d,const char* key){const auto& v=property(d,key);if(!std::holds_alternative<bool>(v))throw std::runtime_error(std::string("Projectile collision expected boolean: ")+key);return std::get<bool>(v);}
float number(const ProjectileDefinition& d,const char* key){const auto& v=property(d,key);if(!std::holds_alternative<double>(v)||!std::isfinite(std::get<double>(v)))throw std::runtime_error(std::string("Projectile collision expected finite scalar: ")+key);return float(std::get<double>(v));}
const std::string& string(const ProjectileDefinition& d,const char* key){const auto& v=property(d,key);if(!std::holds_alternative<std::string>(v))throw std::runtime_error(std::string("Projectile collision expected name: ")+key);return std::get<std::string>(v);}
const ActorComponents* unit(const ActorWorld& world,int32_t id){const auto* a=world.actor(id);return a&&a->registered&&a->definition&&a->definition->kind=="Unit"?a:nullptr;}
const ActorComponents* owner(const ProjectileActor& p){const auto* a=unit(*p.firingWorld,p.state.ownerId);if(!a&&p.state.ownerSpawnedById)a=unit(*p.firingWorld,p.state.ownerSpawnedById);return a;}
// PolygonCollision(Geometry,Vector2,float), native2ce260. Every edge uses
// closest point with clamped projection; inclusive radius comparison, followed
// by the actual polygon containment test. Empty native geometry is not valid.
bool circlePolygon(const std::vector<ActorPoint>& polygon,float x,float y,float radius){
    if(polygon.size()<3)throw std::runtime_error("Projectile defense needs actual nonempty collision polygon");
    const float rr=radius*radius;ActorPoint a=polygon.back();bool inside=false;
    for(const auto b:polygon){const float dx=b.x-a.x,dy=b.y-a.y,dd=dx*dx+dy*dy;
        float t=dd==0?0:((x-a.x)*dx+(y-a.y)*dy)/dd;t=std::clamp(t,0.f,1.f);
        const float ex=x-(a.x+t*dx),ey=y-(a.y+t*dy);if(ex*ex+ey*ey<=rr)return true;
        if((a.y>y)!=(b.y>y)){const float at=a.x+(y-a.y)*(b.x-a.x)/(b.y-a.y);if(x<at)inside=!inside;}
        a=b;
    }
    return inside;
}
float normalizedAngle(float angle){constexpr float turn=6.283185482025146484375f;return angle-std::floor(angle/turn)*turn;}
}
bool projectileUnitsShareTeam(const ActorWorld& world,int32_t first,int32_t second,const ProjectileTeamProviders& providers,bool& result,std::string& error){
    // Unit::HasSameTeam2f39f0. Actual Player association is pointer identity,
    // both nullptr compares equal. NoAllegiance and either traitor force false.
    try{result=false;const auto* a=unit(world,first);const auto* b=unit(world,second);
        if(!a)throw std::runtime_error("HasSameTeam requires actual first Unit");if(!b){error.clear();return true;}
        if(actorProperty(world,first,"NoAllegiance").boolean()||actorProperty(world,second,"NoAllegiance").boolean()){error.clear();return true;}
        auto player=[&](int32_t id){if(actorProperty(world,id,"FriendlyToPlayer").boolean()){if(!providers.firstPlayerIdentity)throw std::runtime_error("FriendlyToPlayer requires actual PlayerManager first Player identity");return providers.firstPlayerIdentity;}
            uint64_t value=0;if(!providers.unitPlayerIdentity||!providers.unitPlayerIdentity(id,value,error))throw std::runtime_error(error.empty()?"HasSameTeam requires actual Unit.pPlayer association":error);return value;};
        const uint64_t pa=player(first),pb=player(second);
        if(!a->status||!b->status)throw std::runtime_error("HasSameTeam requires actual UnitStatus");
        result=pa==pb&&!a->status->isTraitor&&!b->status->isTraitor;error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool projectileAffectsUnit(const ProjectileActor& projectile,int32_t victim,float worldTime,const ProjectileTeamProviders& teams,bool& result,std::string& error){
    // Projectile.AffectsUnit6b200 and ShouldAffectSelf6db80, public REA
    // ev_99a091bbf158418b8938d42d335a3fa2cce74f3c86654203f37c5d9ca562163f.
    try{result=false;if(!projectile.firingWorld||!std::isfinite(worldTime))throw std::runtime_error("AffectsUnit requires actual source World and clock");
        const auto& world=*projectile.firingWorld;const auto* target=unit(world,victim);if(!target){error.clear();return true;}
        const auto& data=currentData(projectile);const auto& excluded=string(data,"DoesNotAffectName");
        if(!excluded.empty()&&excluded==actorProperty(world,victim,"Name").text()){error.clear();return true;}
        if(actorProperty(world,victim,"AllDamageAffects").boolean()&&(number(data,"Damage")>0||number(data,"DamageHigh")>0)){result=true;error.clear();return true;}
        const auto* attacker=owner(projectile);const bool traitor=projectile.state.firedByTraitor||(attacker&&attacker->status&&attacker->status->isTraitor);
        const bool targetTreatsTraitorsFriendly=actorProperty(world,victim,"TraitorsTreatAsFriendly").boolean();
        const bool friends=boolean(data,projectile.state.flipAllegiance?"AffectsEnemies":"AffectsFriends");
        const bool enemies=boolean(data,projectile.state.flipAllegiance?"AffectsFriends":"AffectsEnemies");
        if(traitor&&targetTreatsTraitorsFriendly){if(friends){result=true;error.clear();return true;}if(enemies){error.clear();return true;}}
        if(!attacker){result=friends?(traitor==targetTreatsTraitorsFriendly):(!enemies||(traitor!=targetTreatsTraitorsFriendly));error.clear();return true;}
        if(attacker==target){const bool finalJump=boolean(data,"FinalJumpToOwner");const float after=number(data,"ReturnToOwnerAfterInactiveSeconds");
            const bool returning=(finalJump||after!=0)&&(projectile.state.forceOwnerReturn||(after!=0&&worldTime-projectile.state.lastJumpTime>=after)||(finalJump&&projectile.state.numJumps<=0));
            result=returning||boolean(data,projectile.state.flipAllegiance?"AffectsEnemies":"AffectsSelf");error.clear();return true;
        }
        bool same=false;if(!projectileUnitsShareTeam(world,attacker->id,victim,teams,same,error))throw std::runtime_error(error);
        result=same?friends:enemies;error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool ProjectileWorld::checkArcProjectileDefense(uint64_t handle,const ProjectileDefenseProviders& providers,std::string& error){return checkProjectileDefense(handle,providers,true,error);}
// arc=false: base Projectile::CheckProjectileDefense72560 (.local/reports/native-weapons-check-
// projectile-defense-pseudocode.c) with ProjectileManager::ProjectileDefenseApplies7b0b0
// (native-weapons-projectile-defense-applies-pseudocode.c). ProjectileData = Def + 0x50:
// +0x640 ProjectileDefenseRadius, +0x644 UseDetonationForProjectileDefense, +0x648
// ProjectileDefenseAngle, +0x64c DeflectProjectiles. Base differs from ARC in: entry admits
// radius>0 || UseDetonationForProjectileDefense; defender location = Thing::GetLocation
// (mAttachedTo live -> its location + mAttachOffset, else +0x88); coverage angle = body(+0xc8)
// angle(+0x54); reflect direction uses both GetLocations.
bool ProjectileWorld::checkProjectileDefense(uint64_t handle,const ProjectileDefenseProviders& providers,bool arc,std::string& error){
    bool mutated=false;
    try{auto* defending=actor(handle);if(!defending||!defending->active||defending->disposed||!defending->firingWorld||(arc!=(defending->descriptor.concreteClass==ProjectileClass::Arc)))throw std::runtime_error("Projectile defense requires actual active source binding of the dispatched class");
        const auto& defense=currentData(*defending);const float radius=number(defense,"ProjectileDefenseRadius");
        if(arc?radius<=0:(!(radius>0)&&!boolean(defense,"UseDetonationForProjectileDefense"))){error.clear();return true;}
        if(!arc&&canBeReflected_.empty()){error.clear();return true;}
        const auto* defenderOwner=unit(*defending->firingWorld,defending->state.ownerId);
        // ProjectileDefenseApplies first owner gate reads ThingData.StopsProjectiles,
        // not groups, collision cache or an assumed healthy player.
        if(!defenderOwner||!actorProperty(*defending->firingWorld,defenderOwner->id,"Thing.StopsProjectiles").boolean()){error.clear();return true;}
        const float coverage=number(defense,"ProjectileDefenseAngle");const bool deflect=boolean(defense,"DeflectProjectiles");
        // Capture native vector's endpoints. Callback admissions remain pending;
        // reentrant promotion is forbidden by the manager.
        const auto candidates=canBeReflected_;
        for(const auto candidateHandle:candidates){auto* candidate=actor(candidateHandle);
            if(!candidate||candidate->lifeStatus!=0)continue; // Thing::IsAlive2c720.
            if(!arc&&candidateHandle==handle)continue; // base VA180072630 self skip
            const auto& data=currentData(*candidate);
            if((!candidate->state.hasStarted&&boolean(data,"InvulnerableToProjectileDefenseBeforeStart"))||(candidate->state.pauseTimeRemaining>0&&boolean(data,"InvulnerableToProjectileDefenseDuringPause")))continue;
            if(!boolean(data,"CanBeProjectileDefenseDestroyed")&&!deflect){const auto& by=string(data,"CanBeProjectileDefenseDestroyedByName");if(by.empty()||by!=defense.name)continue;}
            if(candidate->descriptor.concreteClass==ProjectileClass::Lob)throw std::runtime_error("LOB defense vertical-motion range requires actual LobProjectile state");
            if(!boolean(data,"CheckUnitImpact"))continue;
            const auto* candidateOwner=owner(*candidate);
            // GetOwner fallback is fired-by weapon owner after direct/spawned-by
            // lookup. Portable bound Arsenal's original owner is request.ownerId.
            if(!candidateOwner)candidateOwner=unit(*candidate->firingWorld,candidate->request.ownerId);
            bool same=false;if(!projectileUnitsShareTeam(*defending->firingWorld,defenderOwner->id,candidateOwner?candidateOwner->id:-1,providers.teams,same,error))throw std::runtime_error(error);
            const bool friends=boolean(data,candidate->state.flipAllegiance?"AffectsEnemies":"AffectsFriends");
            const bool enemies=boolean(data,candidate->state.flipAllegiance?"AffectsFriends":"AffectsEnemies");
            if((!friends&&same)||(!enemies&&!same))continue;
            if(std::find(candidate->thingsHit.begin(),candidate->thingsHit.end(),defenderOwner->entityHandle)!=candidate->thingsHit.end())continue;
            float defX=defending->spawnX,defY=defending->spawnY;
            if(!arc&&!thingLocation(*defending,defX,defY,error))throw std::runtime_error(error);
            if(coverage>0){if(boolean(data,"IgnoreCoverageAngles"))continue;
                // 7b0b0: atan2(-(cand.y-def.y), cand.x-def.x) with cand GetLocation (attachment aware).
                float cx=candidate->body.x,cy=candidate->body.y;if(!arc&&!thingLocation(*candidate,cx,cy,error))throw std::runtime_error(error);
                const float direction=normalizedAngle(std::atan2(-(cy-defY),cx-defX));
                const float defenseAngle=normalizedAngle(defending->body.angle);const float distance=std::abs(direction-defenseAngle);
                constexpr float turn=6.283185482025146484375f;if(coverage*.5f<std::min(distance,turn-distance))continue;
            }
            bool collision=false;if(boolean(currentData(*defending),"UseDetonationForProjectileDefense")){
                if(!providers.inBlast||!providers.inBlast(handle,candidateHandle,collision,error))throw std::runtime_error(error.empty()?"Actual defense IsInBlast consumer unavailable":error);
            }
            if(!collision)collision=circlePolygon(candidate->geometry,defX,defY,radius); // PolygonCollision2ce260(cand +0x1c0, location, radius)
            if(!collision)continue;
            ProjectileDefenseAction action;action.defender=handle;action.candidate=candidateHandle;action.ownerId=defenderOwner->id;
            if(deflect&&!boolean(currentData(*candidate),"DestroyOnReflect")){
                action.kind=ProjectileDefenseActionKind::Reflect;action.directionX=candidate->body.x-defending->body.x;action.directionY=candidate->body.y-defending->body.y;
                if(!arc){float ax=0,ay=0,bx=0,by=0;if(!thingLocation(*candidate,bx,by,error)||!thingLocation(*defending,ax,ay,error))throw std::runtime_error(error);action.directionX=bx-ax;action.directionY=by-ay;}
                const float length=std::sqrt(action.directionX*action.directionX+action.directionY*action.directionY);if(length!=0){action.directionX/=length;action.directionY/=length;}
            }else{action.kind=ProjectileDefenseActionKind::OnProjectileBlock;action.weaponName=defending->request.weaponName;}
            if(!providers.preflight||!providers.execute||!providers.preflight(action,error))throw std::runtime_error(error.empty()?"Actual projectile defense callback consumer unavailable":error);
            mutated=true;if(!providers.execute(action,error))throw std::runtime_error(error);
            candidate=actor(candidateHandle);if(!candidate)throw std::runtime_error("Projectile defense callback destroyed retained candidate storage");
            if(action.kind==ProjectileDefenseActionKind::OnProjectileBlock)candidate->state.expired=true;
        }
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();if(mutated){faulted_=true;fault_=error;}return false;}
}
}

namespace hades {
namespace {
SpatialBounds polygonBounds(const std::vector<ActorPoint>& p){if(p.empty())throw std::runtime_error("Projectile collision has no actual shape bounds");SpatialBounds b{p[0].x,p[0].y,p[0].x,p[0].y};for(const auto v:p){b.minX=std::min(b.minX,v.x);b.minY=std::min(b.minY,v.y);b.maxX=std::max(b.maxX,v.x);b.maxY=std::max(b.maxY,v.y);}return b;}
bool overlaps(const SpatialBounds& a,const SpatialBounds& b){return a.minX<=b.maxX&&b.minX<=a.maxX&&a.minY<=b.maxY&&b.minY<=a.maxY;}
bool pointInPolygon(const std::vector<ActorPoint>& p,float x,float y){if(p.empty())return false;bool inside=false;auto a=p.back();for(const auto b:p){if((a.y>y)!=(b.y>y)&&x<a.x+(y-a.y)*(b.x-a.x)/(b.y-a.y))inside=!inside;a=b;}return inside;}
// Native PolygonCollision(Polygon,Polygon,motion0) uses separating axes.
// Zero-motion unit checks include contact, obstacle branch requires nonzero MTV.
bool polygonsOverlap(const std::vector<ActorPoint>& a,const std::vector<ActorPoint>& b,bool obstacle){
    if(a.empty()||b.empty())return false;float minimum=INFINITY;bool axisFound=false;
    for(const auto* p:{&a,&b}){auto previous=p->back();for(const auto v:*p){const float dx=v.x-previous.x,dy=v.y-previous.y;previous=v;const float length=std::sqrt(dx*dx+dy*dy);if(length==0)continue;axisFound=true;
        const float nx=-dy/length,ny=dx/length;float amin=INFINITY,amax=-INFINITY,bmin=INFINITY,bmax=-INFINITY;
        for(const auto q:a){const float dot=q.x*nx+q.y*ny;amin=std::min(amin,dot);amax=std::max(amax,dot);}for(const auto q:b){const float dot=q.x*nx+q.y*ny;bmin=std::min(bmin,dot);bmax=std::max(bmax,dot);}
        const float penetration=std::min(amax,bmax)-std::max(amin,bmin);if(penetration<0)return false;minimum=std::min(minimum,penetration);
    }}
    return axisFound&&(!obstacle||minimum>0);
}
bool primaryNoCollision(const ActorComponents& a){if(a.animation.active())return (a.animation.clip()->flags&ActorNoCollision)!=0;if(a.primaryEffectAnimation||a.primaryAnimationLifetime){if(!a.primaryOwnerHasNoCollision)throw std::runtime_error("Actual collision primary OwnerHasNoCollision unavailable");return *a.primaryOwnerHasNoCollision;}return false;}
float effectNumber(const WeaponEffectDefinition& d,const char* key){const auto it=d.fields.find(key);if(it==d.fields.end()||!std::holds_alternative<double>(it->second))throw std::runtime_error(std::string("Actual collision effect scalar unavailable: ")+key+" effect="+d.name+" type="+std::to_string(d.type));return float(std::get<double>(it->second));}
}
bool checkProjectileDodge(const ProjectileActor& p,int32_t id,const ProjectileImpactProviders& providers,bool& dodged,std::string& error){
    // LifeComponent::CheckDodge VA1802b7b70: pending AND active DisableDodge
    // gate; active DodgeChance additions; RandomDouble is consumed even at 0.
    try{dodged=false;if(!p.firingWorld)throw std::runtime_error("CheckDodge requires bound World");
        if(boolean(currentData(p),"IgnoreDodge")){error.clear();return true;}
        const auto* a=unit(*p.firingWorld,id);if(!a||!a->hasLife){error.clear();return true;}
        uint64_t player=0;if(!providers.teams.unitPlayerIdentity||!providers.teams.unitPlayerIdentity(id,player,error))throw std::runtime_error(error.empty()?"CheckDodge requires actual Player association":error);
        // Unit.pPlayer -> active Unit Life.Invulnerable (native +4e0/+d0/+31).
        // Portable player bindings identify only the live active Unit.
        if(player&&a->invulnerable){error.clear();return true;}
        if(!a->status)throw std::runtime_error("CheckDodge requires live UnitStatus");
        const auto status=a->status->snapshot();float chance=a->life.dodgeChance;
        auto effect=[&](uint64_t h)->const WeaponEffectDefinition&{const WeaponEffectDefinition* d=nullptr;if(!providers.effectData||!providers.effectData(h,d,error)||!d)throw std::runtime_error(error.empty()?"CheckDodge requires admitted effect":error);return *d;};
        for(const auto* handles:{&status.pending,&status.active})for(const auto h:*handles){const auto& fields=effect(h).fields;const auto it=fields.find("DisableDodge");if(it==fields.end()||!std::holds_alternative<bool>(it->second))throw std::runtime_error("CheckDodge effect DisableDodge unavailable");if(std::get<bool>(it->second)){error.clear();return true;}}
        for(const auto h:status.active)chance+=effectNumber(effect(h),"DodgeChance");
        double draw=0;if(!providers.random||!providers.random(draw,error))throw std::runtime_error(error.empty()?"CheckDodge requires live RandomDouble":error);
        dodged=double(chance)>=draw;error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool ProjectileWorld::checkForImpact(uint64_t handle,const ProjectileImpactProviders& providers,ProjectileImpactResult& result,std::string& error,int32_t* stopVictim){
    bool mutated=false;
    try{auto* p=actor(handle);if(!p||!p->active||p->disposed||!p->firingWorld||!std::isfinite(providers.worldTime))throw std::runtime_error("Impact requires actual active projectile and World clock");
        auto& world=*p->firingWorld;
        if(stopVictim)*stopVictim=-1; // VA180070901 clears the victim out-parameter.
        // CheckForImpact708b0 prelude, shared by every class (vslot0x1b0):
        // mCurrentDistanceSqrd, DetonateAtTarget and Type!=ARC/BEAM range stop.
        {const float dx=p->body.x-p->spawnX,dy=p->body.y-p->spawnY;mutated=true;p->currentDistanceSquared=dx*dx+dy*dy;}
        auto canDetonate=[&](){const auto& d=currentData(*p);return number(d,"DamageRadius")>0||!string(d,"SpawnOnDetonate").empty();}; // CanDetonate75950
        auto detonateNow=[&](){if(!canDetonate())return;ProjectileDetonationProviders dp;dp.worldTime=providers.worldTime;dp.teams=providers.teams;dp.applyImpact=providers.detonationImpact;dp.impact=&providers;if(!detonate(handle,dp,error))throw std::runtime_error(error);p=actor(handle);};
        if(boolean(currentData(*p),"DetonateAtTarget")&&(p->state.targetX!=0||p->state.targetY!=0)){
            const float tx=p->spawnX-p->state.targetX,ty=p->spawnY-p->state.targetY;
            if(p->currentDistanceSquared>tx*tx+ty*ty){detonateNow();result=ProjectileImpactResult::Stop;error.clear();return true;}
        }
        {const int32_t type=int32_t(p->descriptor.concreteClass); // data+0x2d8 Type, fixed at creation
            if(type!=5&&type!=2&&p->state.rangeSquared>0&&p->currentDistanceSquared>p->state.rangeSquared){
                detonateNow();p->state.hitMaxRange=true;
                result=(number(currentData(*p),"Deceleration")!=0&&p->state.speed>0)?ProjectileImpactResult::Continue:ProjectileImpactResult::Stop;error.clear();return true;
            }
        }
        if(!boolean(currentData(*p),"CheckUnitImpact")&&!boolean(currentData(*p),"CheckObstacleImpact")){result=ProjectileImpactResult::Continue;error.clear();return true;}
        const auto& initial=currentData(*p);
        // DamageRadius only gates the movement-reaction sweep below (VA70a9d);
        // positive-radius Impact remains rejected inside the Impact consumer.
        if(boolean(initial,"FinalJumpToOwner")||number(initial,"ReturnToOwnerAfterInactiveSeconds")!=0||p->state.numJumps||p->state.numBounces||number(initial,"PenetrateFinalJumpUnitRangeModifier")!=0)throw std::runtime_error("Actual impact jump/bounce branch not yet supported");
        const auto supportedChecks=[&](const ProjectileDefinition& d){return number(d,"UnitCollisionCheck")==1&&(number(d,"ObstacleCollisionCheck")==1||number(d,"ObstacleCollisionCheck")==4);};
        if(!supportedChecks(initial))throw std::runtime_error("Actual impact collision check shape not reconstructed");
        result=ProjectileImpactResult::Continue;
        auto call=[&](ProjectileCollisionOperation op,bool predicate,bool& value){op.projectile=handle;ProjectileCollisionReply reply;
            if(!providers.preflight||!providers.execute||!providers.preflight(op,error))throw std::runtime_error(error.empty()?"Missing actual projectile collision consumer":error);
            mutated=true;if(!providers.execute(op,reply,error))throw std::runtime_error(error);
            if(predicate){if(!reply.predicate)throw std::runtime_error("Actual collision predicate returned no result");value=*reply.predicate;}else if(reply.predicate)throw std::runtime_error("Unexpected predicate from actual collision side effect");
        };
        const auto* fireOwner=unit(world,p->request.ownerId);
        // Actual pre-collision movement-reaction sweep uses obstacle centers
        // inside the original projectile polygon; map flag gates each subtree.
        if(fireOwner&&boolean(initial,"CheckObstacleImpact")&&number(initial,"DamageRadius")<=0&&!p->geometry.empty()){
            std::vector<int32_t> nearby;if(!world.spatialObstacleCandidates(polygonBounds(p->geometry),ObstacleGridCategory::All,nearby,error))throw std::runtime_error(error);
            for(const int32_t id:nearby){const auto* a=world.actor(id);if(!a||!a->registered)continue;float x=0,y=0;if(!world.location(id,x,y,error))throw std::runtime_error(error);if(!pointInPolygon(p->geometry,x,y))continue;
                std::vector<int32_t> stack{id},visited;
                while(!stack.empty()){const auto at=stack.back();stack.pop_back();if(std::find(visited.begin(),visited.end(),at)!=visited.end())continue;visited.push_back(at);
                    a=world.actor(at);if(!a||!a->registered||!a->sourceId)continue;const auto* seed=world.seed(a->sourceId);if(!seed)throw std::runtime_error("Original movement-reaction map record unavailable");
                    const auto* allow=seed->map.get("AllowMovementReaction");if(!allow)throw std::runtime_error("Original MapThing.AllowMovementReaction unavailable");if(!allow->boolean())continue;
                    ProjectileCollisionOperation op;op.kind=ProjectileCollisionOperationKind::OnMovementReaction;op.victimId=at;bool ignored=false;call(op,false,ignored);
                    a=world.actor(at);if(!a)throw std::runtime_error("Movement reaction lost retained obstacle storage");for(auto it=a->children.rbegin();it!=a->children.rend();++it)stack.push_back(*it);
                }
            }
        }
        p=actor(handle);if(!p)throw std::runtime_error("Collision callback lost retained projectile storage");
        if(!supportedChecks(currentData(*p)))throw std::runtime_error("Movement-reaction callback selected unsupported collision shape");
        mutated=true; // mCurrentDistanceSqrd was stored by the prelude (VA7096f).
        if(p->geometry.empty()){error.clear();return true;}
        const auto bounds=polygonBounds(p->geometry);
        std::vector<int32_t> candidates;
        if(boolean(currentData(*p),"CheckUnitImpact"))for(const auto id:world.registrationOrder()){const auto* a=unit(world,id);if(a)candidates.push_back(id);}
        if(boolean(currentData(*p),"CheckObstacleImpact")){std::vector<int32_t> obstacles;if(!world.spatialObstacleCandidates(bounds,ObstacleGridCategory::StopsProjectiles,obstacles,error))throw std::runtime_error(error);candidates.insert(candidates.end(),obstacles.begin(),obstacles.end());}
        struct Hit {int32_t id;uint64_t lifetime;bool unit;};std::vector<Hit> hits;
        for(const auto id:candidates){p=actor(handle);auto* a=world.actor(id);if(!p||!a||!a->registered||!a->object||!a->definition)continue;const auto& data=currentData(*p);const bool isUnit=a->definition->kind=="Unit";
            const bool repeat=boolean(data,isUnit?"MultipleUnitCollisions":"MultipleObstacleCollisions");
            if(!repeat&&std::find(p->hitByImpact.begin(),p->hitByImpact.end(),a->entityHandle)!=p->hitByImpact.end())continue;
            if(!a->geometryAvailable){if(a->geometryRequired)throw std::runtime_error("Actual collision polygon unavailable for live actor "+std::to_string(id));continue;}
            if(a->geometry.empty()||!overlaps(bounds,polygonBounds(a->geometry)))continue;
            float z=0;if(!world.zLocation(id,z,error))throw std::runtime_error(error);
            if(z+float(actorProperty(world,id,"Thing.Tallness").number())<number(data,"MinVictimZ"))continue;
            if(isUnit){
                if(a->lifeStatus!=0||z>number(data,"MaxVictimZ")||!actorProperty(world,id,"Thing.StopsProjectiles").boolean()||primaryNoCollision(*a))continue;
                if(a->sourceId){const auto* seed=world.seed(a->sourceId);if(!seed||!seed->map.get("Collision"))throw std::runtime_error("Actual map Unit collision flag unavailable");if(!seed->map.get("Collision")->boolean())continue;}
                const auto* attacker=owner(*p);if(!attacker)attacker=fireOwner;
                if(attacker){bool same=false;if(!projectileUnitsShareTeam(world,id,attacker->id,providers.teams,same,error))throw std::runtime_error(error);
                    const bool friends=boolean(data,p->state.flipAllegiance?"AffectsEnemies":"AffectsFriends"),enemies=boolean(data,p->state.flipAllegiance?"AffectsFriends":"AffectsEnemies");
                    if((same&&!friends)||(!same&&!enemies)||(attacker->status&&attacker->status->isTraitor&&actorProperty(world,id,"TraitorsTreatAsFriendly").boolean()))continue;
                }
            }else{bool stops=false;if(!world.obstacleStops(id,true,stops,error))throw std::runtime_error(error);if(!stops)continue;
                const float threshold=float(actorProperty(world,id,"MaxUpwardVelocityToCollide").number());if(threshold>0&&a->physics&&a->physics->upVelocity>threshold)continue;
            }
            if(!isUnit&&number(data,"ObstacleCollisionCheck")==4){
                // GridSquare::detectCollisions30ae10, params+0e==4:
                // PointInPolygon2ceaf0(target.geometry, projectile GetLocation).
                // The native rectangle/Z/StopsProjectiles gates above still run.
                float x=0,y=0;if(!thingLocation(*p,x,y,error))throw std::runtime_error(error);
                if(!pointInPolygon(a->geometry,x,y))continue;
            }else if(!polygonsOverlap(p->geometry,a->geometry,!isUnit))continue;
            hits.push_back({id,a->entityHandle,isUnit});
            if(!boolean(currentData(*p),"MultipleCollisionPerFrame"))break;
        }
        // Native DetectCollisions has completed before reflection/dodge/OnHit.
        // Keep captured candidate identities even when original callbacks move
        // later targets; do not re-run narrowphase on the mutated world.
        for(const auto hit:hits){const auto id=hit.id;const bool isUnit=hit.unit;p=actor(handle);auto* a=world.actor(id);
            if(!p||!a||a->entityHandle!=hit.lifetime)throw std::runtime_error("Captured actual collision target lifetime unavailable");
            const auto& data=currentData(*p);
            auto actualEffect=[&](uint64_t h)->const WeaponEffectDefinition&{const WeaponEffectDefinition* d=nullptr;if(!providers.effectData||!providers.effectData(h,d,error)||!d)throw std::runtime_error(error.empty()?"Actual admitted collision effect definition unavailable":error);return *d;};
            bool reflected=false;
            if(boolean(data,"CanBeReflected")){
                float reflection=a->hasLife?a->life.reflection:0;
                // Life.GetReflection2b930a adds EffectData+3b0, the actual
                // reflected field ReflectAddition. Life's raw field is named
                // Reflection; copying that name into EffectData is invalid.
                if(isUnit&&a->status)for(const auto e:a->status->snapshot().active)reflection+=effectNumber(actualEffect(e),"ReflectAddition");
                if(reflection>0){if(!providers.random)throw std::runtime_error("Actual reflection RNG unavailable");double draw=0;if(!providers.random(draw,error))throw std::runtime_error(error);if(!std::isfinite(draw)||draw<0||draw>=1)throw std::runtime_error("Reflection RNG outside [0,1)");
                    if(draw<=actorProperty(world,id,"Life.ReflectionChance").number()){const float angle=normalizedAngle(p->body.angle+3.1415927410125732421875f),facing=normalizedAngle(a->object->angle),diff=std::abs(angle-facing);reflected=a->life.armorCoverage*.5f>std::min(diff,6.283185482025146484375f-diff);}
                }
                if(!reflected&&isUnit&&a->status)for(const auto effect:a->status->snapshot().active){if(actualEffect(effect).type==9){ProjectileCollisionOperation op;op.kind=ProjectileCollisionOperationKind::TryProjectileDefenseEffect;op.victimId=id;op.effect=effect;op.unit=true;call(op,true,reflected);break;}}
            }
            if(reflected){if(!isUnit||!boolean(currentData(*p),"DestroyOnReflect"))throw std::runtime_error("Actual reflection requires native aggregate collision minimum-translation vector");
                if(stopVictim)*stopVictim=id;result=ProjectileImpactResult::Stop;error.clear();return true;
            }
            if(isUnit&&!boolean(currentData(*p),"IgnoreDodge")){bool dodged=false;ProjectileCollisionOperation op;op.kind=ProjectileCollisionOperationKind::CheckDodge;op.victimId=id;op.unit=true;call(op,true,dodged);
                if(dodged){p=actor(handle);p->hitByImpact.push_back(a->entityHandle);op.kind=ProjectileCollisionOperationKind::OnDodge;bool ignored=false;call(op,false,ignored);continue;}
            }
            bool visible=true;
            for(unsigned sight=0;sight<2;++sight){
                const bool fromOwner=sight==1;if(fromOwner&&(!isUnit||!fireOwner||!boolean(currentData(*p),"ImpactLineOfSightFromOwner")))continue;
                if(!fromOwner&&!boolean(currentData(*p),"ImpactLineOfSight"))continue;
                ProjectileCollisionOperation op;op.kind=ProjectileCollisionOperationKind::HasLineOfSight;op.victimId=id;op.unit=isUnit;op.z=p->body.z;op.backoff=isUnit?0:12;
                if(!world.location(id,op.toX,op.toY,error))throw std::runtime_error(error);op.fromX=p->body.x;op.fromY=p->body.y;
                if(fromOwner&&!world.location(fireOwner->id,op.fromX,op.fromY,error))throw std::runtime_error(error);
                call(op,true,visible);if(!visible)break;
            }
            if(!visible)continue;
            if(isUnit){bool affects=false;if(!projectileAffectsUnit(*p,id,providers.worldTime,providers.teams,affects,error))throw std::runtime_error(error);if(!affects)continue;}
            // The real Impact consumer performs CalcImpactLocation/immunity,
            // records native hit cache before synchronous TakeHit/OnHit,
            // ApplyEffects/FX/forces and mThingsHit. An ack cannot replace it.
            ProjectileCollisionOperation impact;impact.kind=ProjectileCollisionOperationKind::Impact;impact.victimId=id;impact.unit=isUnit;bool ignored=false;call(impact,false,ignored);
            p=actor(handle);a=world.actor(id);if(!p||!a)throw std::runtime_error("Impact callback lost retained actual storage");
            if(!a->hasLife)throw std::runtime_error("Native post-Impact penetration requires actual victim Life component: "+std::to_string(id)+" "+(a->definition?a->definition->name:std::string()));
            if(p->state.numJumps||p->state.numBounces||boolean(currentData(*p),"FinalJumpToOwner")||number(currentData(*p),"PenetrateFinalJumpUnitRangeModifier")!=0)throw std::runtime_error("Impact callback entered unsupported native jump/bounce branch");
            const bool always=actorProperty(world,id,"Life.ProjectilesAlwaysPenetrate").boolean();const bool canPenetrate=actorProperty(world,id,"Life.ProjectilesCanPenetrate").boolean();
            const bool unlimited=boolean(currentData(*p),isUnit?"UnlimitedUnitPenetration":"UnlimitedObstaclePenetration");
            if(!unlimited&&!always&&!(p->state.numPenetrations>0&&canPenetrate)){if(stopVictim)*stopVictim=id;result=ProjectileImpactResult::Stop;error.clear();return true;}
            if(!always)--p->state.numPenetrations;
        }
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();if(mutated){faulted_=true;fault_=error;}return false;}
}
}

namespace hades {
namespace {
float cross(ActorPoint a,ActorPoint b,ActorPoint c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);}
bool onSegment(ActorPoint a,ActorPoint b,ActorPoint c){return cross(a,b,c)==0&&c.x>=std::min(a.x,b.x)&&c.x<=std::max(a.x,b.x)&&c.y>=std::min(a.y,b.y)&&c.y<=std::max(a.y,b.y);}
bool linePolygon(const std::vector<ActorPoint>& polygon,ActorPoint a,ActorPoint b){if(polygon.empty())return false;if(pointInPolygon(polygon,a.x,a.y)||pointInPolygon(polygon,b.x,b.y))return true;
    auto c=polygon.back();for(const auto d:polygon){const float abC=cross(a,b,c),abD=cross(a,b,d),cdA=cross(c,d,a),cdB=cross(c,d,b);
        if(((abC>0&&abD<0)||(abC<0&&abD>0))&&((cdA>0&&cdB<0)||(cdA<0&&cdB>0)))return true;
        if(onSegment(a,b,c)||onSegment(a,b,d)||onSegment(c,d,a)||onSegment(c,d,b))return true;c=d;
    }return false;
}
}
bool ProjectileWorld::hasLineOfSight(const ProjectileCollisionOperation& op,bool& clear,std::string& error){
    // Thing.HasLineOfSight2dff90 -> GridManager.GetLineOfSightBlocker3076a0.
    // Covered first Sword supplies width0, requireStopsUnits=true,
    // ignoreAlwaysPenetrate=false; DieOnCollision obstacles do not block.
    try{const auto* p=actor(op.projectile);if(op.kind!=ProjectileCollisionOperationKind::HasLineOfSight||!p||!p->firingWorld)throw std::runtime_error("Actual projectile LOS source unavailable");
        for(float v:{op.fromX,op.fromY,op.toX,op.toY,op.z,op.backoff})if(!std::isfinite(v))throw std::runtime_error("Nonfinite actual projectile LOS input");
        clear=true;ActorPoint a{op.fromX,op.fromY},b{op.toX,op.toY};if(a.x==b.x&&a.y==b.y){error.clear();return true;}
        const float dx=b.x-a.x,dy=b.y-a.y,length2=dx*dx+dy*dy;
        if(op.backoff>0){if(op.backoff*op.backoff>length2){error.clear();return true;}const float fraction=op.backoff/std::sqrt(length2);b.x-=dx*fraction;b.y-=dy*fraction;}
        SpatialBounds bounds{std::min(a.x,b.x),std::min(a.y,b.y),std::max(a.x,b.x),std::max(a.y,b.y)};
        auto& world=*p->firingWorld;std::vector<int32_t> candidates;if(!world.spatialObstacleCandidates(bounds,ObstacleGridCategory::StopsUnits,candidates,error))throw std::runtime_error(error);
        for(const int32_t id:candidates){if(id==op.victimId)continue;auto* obstacle=world.actor(id);if(!obstacle||!obstacle->registered)continue;
            if(op.z>0&&op.z>actorProperty(world,id,"Thing.Tallness").number())continue;
            bool stops=false;if(!world.obstacleStops(id,false,stops,error))throw std::runtime_error(error);if(!stops||actorProperty(world,id,"Life.DieOnCollision").boolean())continue;
            if(!obstacle->geometryAvailable){if(obstacle->geometryRequired)throw std::runtime_error("Actual LOS blocker polygon unavailable");continue;}
            if(linePolygon(obstacle->geometry,a,b)){clear=false;break;}
        }
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
}

namespace hades {
bool ProjectileWorld::calcDamage(uint64_t handle,int32_t victim,bool disabled,const ProjectileImpactProviders& providers,ProjectileCalculatedDamage& output,std::string& error){
    // Actual float return in XMM0, omitted by Ghidra's inferred void signature.
    // Bounded original LLVM source native-projectile-calc-damage-static.json
    // establishes the arithmetic, paired with public REA ev_7fbb6fc6748742adfb9d1b5ddf2eaadc6af901ba7d6c14901cffa072a52aa46b.
    bool drew=false;
    try{auto* p=actor(handle);if(!p||!p->firingWorld||!p->firingArsenal)throw std::runtime_error("CalcDamage requires actual retained projectile/source binding");
        auto& world=*p->firingWorld;const auto* target=world.actor(victim);if(!target||!target->registered||!target->object)throw std::runtime_error("CalcDamage requires actual live victim");
        output={};if(disabled){error.clear();return true;}
        const auto& data=currentData(*p);const auto* weapon=p->firingArsenal->weaponState(p->request.weaponName,error);if(!weapon)throw std::runtime_error(error);
        auto draw=[&](){if(!providers.random)throw std::runtime_error("CalcDamage requires actual native RNG");double value=0;drew=true;if(!providers.random(value,error))throw std::runtime_error(error);if(!std::isfinite(value)||value<0||value>=1)throw std::runtime_error("Actual damage RNG outside [0,1)");return value;};
        auto weaponNumber=[&](const char* key){const auto it=weapon->definition.fields.find(key);if(it==weapon->definition.fields.end()||!std::holds_alternative<double>(it->second))throw std::runtime_error(std::string("Actual firing WeaponData missing: ")+key);return float(std::get<double>(it->second));};
        const float lo=number(data,"DamageLow"),hi=number(data,"DamageHigh");float damage=(lo!=0||hi!=0)?float(draw())*(hi-lo)+lo:number(data,"Damage");
        if(number(data,"DamagePerConsecutiveHit")>0){if(!target->hasLife)throw std::runtime_error("Consecutive damage requires actual victim Life");damage+=float(target->life.consecutiveHits)*number(data,"DamagePerConsecutiveHit");}
        if(number(data,"DamagePerConsecutiveProjectileHit")>0){const auto it=p->consecutiveHits.find(victim);if(it!=p->consecutiveHits.end())damage+=float(it->second);}
        if(std::find(p->hitByImpact.begin(),p->hitByImpact.end(),target->entityHandle)!=p->hitByImpact.end())damage+=number(data,"DamageBonusToImpactedUnit");
        const float wave=p->state.waveIndex!=0?weaponNumber("AdditionalWaveDamageMultiplier"):1.f;
        damage*=((number(data,"DamageMultiplier")-1.f+1.f)+(weaponNumber("DamageMultiplier")-1.f))*(wave*p->state.damageMultiplier);
        if(number(data,"DamageFalloff")!=1){const float dx=p->spawnX-p->body.x,dy=p->spawnY-p->body.y,distance=std::sqrt(dx*dx+dy*dy);const float factor=distance<=0?1.f:1.f+std::clamp(distance/number(data,"Range"),0.f,1.f)*(number(data,"DamageFalloff")-1.f);damage*=factor;}
        if(number(data,"DamageRadiusFalloff")!=1){float x=0,y=0;if(!world.location(victim,x,y,error))throw std::runtime_error(error);const float dx=p->body.x-x,dy=p->body.y-y,distance=std::sqrt(dx*dx+dy*dy);const float factor=distance<=0?1.f:1.f+std::clamp(distance/p->state.blastRadius,0.f,1.f)*(number(data,"DamageRadiusFalloff")-1.f);damage*=factor;}
        const auto* attacker=unit(world,p->state.ownerId);
        if(boolean(data,"CanCrit")&&attacker){float chance=number(data,"CriticalHitChance")+p->state.critBonus+float(actorProperty(world,attacker->id,"CritAddition").number());
            auto effect=[&](uint64_t h)->const WeaponEffectDefinition&{const WeaponEffectDefinition* d=nullptr;if(!providers.effectData||!providers.effectData(h,d,error)||!d)throw std::runtime_error(error.empty()?"Crit query needs actual admitted effect definition":error);return *d;};
            if(!attacker->status)throw std::runtime_error("Crit query needs actual attacker status");for(const auto h:attacker->status->snapshot().active)chance+=effectNumber(effect(h),"CritAddition");
            if(target->definition&&target->definition->kind=="Unit"){if(!target->status)throw std::runtime_error("Crit query needs actual victim status");for(const auto h:target->status->snapshot().active){const auto& e=effect(h);const float perStack=effectNumber(e,"CritVulnerabilityAddition");int32_t stacks=0;if(perStack!=0&&(!providers.effectStacks||!providers.effectStacks(h,stacks,error)))throw std::runtime_error(error.empty()?"Crit vulnerability needs actual effect stack count":error);if(stacks==INT32_MAX)throw std::runtime_error("Native effect stack increment outside portable signed range");chance=(chance+float(stacks+1)*perStack)*effectNumber(e,"CritVulnerabilityMultiplier");}}
            if(chance>0&&!target->invulnerable&&actorProperty(world,victim,"Life.CanBeCrit").boolean()){
                bool primary=false;
                if(target->animation.active()||target->primaryEffectAnimation||target->primaryAnimationLifetime){if(!providers.primaryInvulnerable||!providers.primaryInvulnerable(victim,primary,error))throw std::runtime_error(error.empty()?"Positive crit requires actual primary OwnerInvulnerable query":error);}
                if(!primary)output.critical=draw()<=double(chance);
            }
        }
        if(!std::isfinite(damage))throw std::runtime_error("Native damage calculation outside finite portable subset");output.amount=damage;error.clear();return true;
    }catch(const std::exception& e){error=e.what();if(drew){faulted_=true;fault_=error;}return false;}
}
}

namespace hades {
bool ProjectileWorld::prepareArcImpact(uint64_t handle,int32_t victim,ProjectileImpactGeometry& output,std::string& error){
    // CalcImpactLocation76f80 averages every inclusive edge intersection,
    // including duplicate vertex contacts, but discards the sentinel(0,0).
    // Exact REA ev_7ca188a86008fb9ae7732e318dfa7e4903f845a059776d41ba9bbba23403cc36.
    // ARC GetImpactAngle6e7d0 uses current direct owner Body angle unless radial;
    // it does not use shot/arc angle. GetImpactLocation72a90 then uses mImpactLocation.
    try{output={};auto* p=actor(handle);const bool arc=p&&p->descriptor.concreteClass==ProjectileClass::Arc;if(!p||!p->firingWorld||(!arc&&p->descriptor.concreteClass!=ProjectileClass::Straight&&p->descriptor.concreteClass!=ProjectileClass::Homing))throw std::runtime_error("Impact geometry requires actual ARC/STRAIGHT/HOMING source binding");
        auto& world=*p->firingWorld;const auto* target=world.actor(victim);if(!target||!target->object)throw std::runtime_error("Impact geometry requires retained actual victim");
        if(target->isDisposed||!target->hasLife){error.clear();return true;}
        const auto& data=currentData(*p);if(number(data,"ImmunityDuration")>0)throw std::runtime_error("Actual positive projectile immunity needs ECS Immunities storage");
        float x=0,y=0;size_t count=0;
        for(size_t i=0;i<p->geometry.size();++i){const auto a=p->geometry[i],b=p->geometry[(i+1)%p->geometry.size()];const float dx=b.x-a.x,dy=b.y-a.y;
            for(size_t j=0;j<target->geometry.size();++j){const auto c=target->geometry[j],d=target->geometry[(j+1)%target->geometry.size()];const float ex=d.x-c.x,ey=d.y-c.y,denominator=dx*ey-dy*ex;if(denominator==0)continue;
                const float ax=a.x-c.x,ay=a.y-c.y,t=(ay*ex-ax*ey)/denominator,u=(ay*dx-ax*dy)/denominator;if(t<0||t>1||u<0||u>1)continue;
                const float ix=t*dx+a.x,iy=t*dy+a.y;if(ix==0&&iy==0)continue;x+=ix;y+=iy;++count;
            }
        }
        if(count){x/=float(count);y/=float(count);}else if(!thingLocation(*p,x,y,error))throw std::runtime_error(error); // Thing::GetLocation (attached-aware)
        p->state.impactX=x;p->state.impactY=y;
        output.x=x;output.y=y;if(!thingZ(*p,output.z,error))throw std::runtime_error(error);output.nativeImpactAdmitted=true;
        // With DamageRadius0 and zero impact sentinel GetImpactLocation returns
        // raw mLocation. Positive-radius fallback belongs to non-covered blasts.
        if(x==0&&y==0&&number(data,"DamageRadius")>0)throw std::runtime_error("Actual blast impact nearest-edge location unavailable");
        if(!impactAngle(handle,victim,x,y,output.angle,error))throw std::runtime_error(error);
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool ProjectileWorld::impactAngle(uint64_t handle,int32_t victim,float x,float y,float& angle,std::string& error){
    // Projectile::GetImpactAngle6e7d0 given its GetImpactLocation72a90 result (x,y).
    try{auto* p=actor(handle);if(!p||!p->firingWorld)throw std::runtime_error("Impact angle lost actual projectile");
        auto& world=*p->firingWorld;const auto& data=currentData(*p);const bool arc=p->descriptor.concreteClass==ProjectileClass::Arc;
        struct {float angle=0;} out;auto& output=out;
        const auto* attacker=unit(world,p->state.ownerId);
        // Projectile::GetImpactAngle6e7d0 (.local/reports/native-projectile-hit-angle-pseudo.txt):
        // !AttachToOwner && Type!=ARC uses GetImpactLocation72a90 (mImpactLocation):
        // radial -> atan2 from impact location to victim GetLocation; else
        // (mSpawnOnDetonate null && Type<3) || mPrevLocation==impact -> Body angle;
        // else atan2 from mPrevLocation to impact. Otherwise the owner branch below.
        if(!arc&&!boolean(data,"AttachToOwner")){
            if(boolean(data,"UseRadialImpact")){float vx=0,vy=0;if(!world.location(victim,vx,vy,error))throw std::runtime_error(error);output.angle=normalizedAngle(std::atan2(-(vy-y),vx-x));}
            else if(string(data,"SpawnOnDetonate").empty()||(p->previousX==x&&p->previousY==y))output.angle=p->body.angle;
            else output.angle=normalizedAngle(std::atan2(-(y-p->previousY),x-p->previousX));
            error.clear();angle=output.angle;return true;
        }
        if(!attacker){output.angle=p->body.angle;}
        else if(boolean(data,"UseRadialImpact")){float ownerX=0,ownerY=0,targetX=0,targetY=0;if(!world.location(attacker->id,ownerX,ownerY,error)||!world.location(victim,targetX,targetY,error))throw std::runtime_error(error);output.angle=normalizedAngle(std::atan2(-(targetY-ownerY),targetX-ownerX));}
        else output.angle=attacker->object?attacker->object->angle:0;
        angle=output.angle;error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool ProjectileWorld::impactGeometry(uint64_t handle,int32_t victim,ProjectileImpactGeometry& output,std::string& error){
    // ApplyImpact76590 own geometry: GetImpactLocation72a90 (.local/reports/native-weapons-get-
    // impact-location-pseudocode.c): mImpactLocation(+0x3f4) nonzero -> it; else mLocation(+0x88)
    // when it differs from mSpawnLocation(+0x90) or DamageRadius(data+0x30c)<=0; else victim
    // GetLocation v: v when spawn==v; victim without polygon (+0x1f0) -> v + 0.1*(spawn-v)/|spawn-v|;
    // else Polygon::ClosestPointOnEdge68af0(victim mGeometry +0x1c0, spawn).
    try{output={};auto* p=actor(handle);if(!p||!p->firingWorld)throw std::runtime_error("Impact geometry lost actual projectile");
        auto& world=*p->firingWorld;const auto* target=world.actor(victim);if(!target||!target->object)throw std::runtime_error("Impact geometry requires retained actual victim");
        const auto& data=currentData(*p);float x=0,y=0;
        if(p->state.impactX!=0||p->state.impactY!=0){x=p->state.impactX;y=p->state.impactY;}
        else if(p->body.x!=p->spawnX||p->body.y!=p->spawnY||!(number(data,"DamageRadius")>0)){x=p->body.x;y=p->body.y;}
        else{float vx=0,vy=0;if(!world.location(victim,vx,vy,error))throw std::runtime_error(error);
            if(p->spawnX==vx&&p->spawnY==vy){x=vx;y=vy;}
            else if(target->geometry.empty()){const float dx=p->spawnX-vx,dy=p->spawnY-vy,len=std::sqrt(dx*dx+dy*dy);y=(1.0f/len)*dy*0.1f+vy;x=(1.0f/len)*dx*0.1f+vx;}
            else{float best=3.4028235e+38f;x=0;y=0;const auto& g=target->geometry;const float px=p->spawnX,py=p->spawnY; // ClosestPointOnEdge68af0
                for(size_t i=0;i<g.size();++i){float ax=g[i].x,ay=g[i].y;const auto b=g[(i+1)%g.size()];float rx=px-ax,ry=py-ay;const float ex=b.x-ax,ey=b.y-ay,t=rx*ex+ry*ey;float d2=0;
                    if(0.f<t){const float l2=ex*ex+ey*ey;if(t<l2){const float f=t/l2;ax=ex*f+ax;ay=ey*f+ay;rx=px-ax;ry=py-ay;d2=rx*rx+ry*ry;}else{d2=(px-b.x)*(px-b.x)+(py-b.y)*(py-b.y);ax=b.x;ay=b.y;}}
                    else d2=rx*rx+ry*ry;
                    const float d=std::sqrt(d2);if(d<best){best=d;x=ax;y=ay;}}}
        }
        output.x=x;output.y=y;if(!thingZ(*p,output.z,error))throw std::runtime_error(error);output.nativeImpactAdmitted=true;
        if(!impactAngle(handle,victim,x,y,output.angle,error))throw std::runtime_error(error);
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
}

namespace hades {
namespace {
// Owner-selected native string predicate: AnimationManager::Create29e410 and
// CreateCustom29f870 both return null for an empty name or the literal "null".
bool absentAnimation(const std::string& name){return name.empty()||name=="null";}
}
bool ProjectileWorld::blastDone(uint64_t handle,bool& done,std::string& error){
    // Projectile::IsBlastDone71bc0: blastTime<=0 && (BlastSpeed<=0 ||
    // DamageRadius*mBlastRadiusModifier <= mBlastRadius).
    try{const auto* p=actor(handle);if(!p)throw std::runtime_error("IsBlastDone lost actual projectile");const auto& d=currentData(*p);
        done=p->state.blastTimeRemaining<=0&&(number(d,"BlastSpeed")<=0||number(d,"DamageRadius")*p->state.blastRadiusModifier<=p->state.blastRadius);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool ProjectileWorld::isInBlast(uint64_t handle,int32_t target,bool& inside,std::string& error){
    // Projectile::IsInBlast760e0 (REA ev_7d91a586fee23ad7a787bb3353af37f94a3d25f7651d17766499bc015d2b8c45).
    // Target point = Thing::GetLocation; RequireHitCenter=false selects the
    // closest edge point of the target polygon (not reconstructed here).
    // Circle when ScaleX==ScaleY==1 uses strict <, ellipse uses <=1.
    try{inside=false;const auto* p=actor(handle);if(!p||!p->firingWorld)throw std::runtime_error("IsInBlast requires actual projectile/World");
        const auto& d=currentData(*p);float tx=0,ty=0;if(!p->firingWorld->location(target,tx,ty,error))throw std::runtime_error(error);
        if(!boolean(d,"RequireHitCenter"))throw std::runtime_error("IsInBlast RequireHitCenter=false Polygon::ClosestPointOnEdge branch not reconstructed");
        const float px=p->body.x,py=p->body.y,r=p->state.blastRadius,sx=number(d,"DamageRadiusScaleX"),sy=number(d,"DamageRadiusScaleY"),band=number(d,"HollowBlastRadiusBand");
        const float dx=tx-px,dy=ty-py;
        if(sx==1.f&&sy==1.f){
            if(band>0&&band<r){const float inner=r-band;if(dy*dy+dx*dx<inner*inner){error.clear();return true;}}
            inside=dy*dy+dx*dx<r*r;
        }else{
            if(band>0&&band<r){const float inner=r-band,ix=inner*sx,iy=inner*sy;if((dy*dy)/(iy*iy)+(dx*dx)/(ix*ix)<=1.f){error.clear();return true;}}
            const float ex=r*sx,ey=r*sy;inside=(dy*dy)/(ey*ey)+(dx*dx)/(ex*ex)<=1.f;
        }
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool ProjectileWorld::checkBaseProjectileDefense(uint64_t handle,std::string& error){
    // Base Projectile::CheckProjectileDefense72560 (vtable slot 0x1b8 for STRAIGHT);
    // parent-supplied consumers (OnProjectileBlock / Reflect) via setDefenseProviders.
    return checkProjectileDefense(handle,defenseProviders_,false,error);
}
bool ProjectileWorld::updateDetonation(uint64_t handle,float elapsed,const ProjectileDetonationProviders& providers,std::string& error){
    // Projectile::UpdateDetonation71c10 (REA ev_09cc5ff36dcf1dde1a9f0bb02eda7113a5f89572d380f084485e8fb1d7b087f0).
    bool mutated=false;
    try{auto* p=actor(handle);if(!p||!p->firingWorld||!p->firingArsenal||!std::isfinite(elapsed))throw std::runtime_error("UpdateDetonation requires actual projectile source binding");
        auto& world=*p->firingWorld;const auto& d=currentData(*p);
        const float speed=number(d,"BlastSpeed"),target=p->state.blastRadiusModifier*number(d,"DamageRadius");
        float radius=target;
        if(speed>0){volatile float step=speed*elapsed*p->state.blastRadiusModifier;const float current=p->state.blastRadius;radius=target;
            if(step<std::abs(target-current)){if(target<=current){radius=current;if(target<current)radius=current-step;}else radius=current+step;}}
        mutated=true;p->state.blastRadius=radius;
        // DebugDrawBlastRadius75e40 is gated by ConfigOptions::DrawExtents
        // (VA181a0f0e8, zero-initialized .bss, no reconstructed writer).
        if(!boolean(d,"DrawAsBeam")&&(p->attachedAnimation||!p->attachedAnimations.empty()))throw std::runtime_error("UpdateDetonation attached Animation::SetRadius consumer not reconstructed");
        if(!(p->state.blastRadius>0)){error.clear();return true;}
        // Virtual slot0x1b8: base CheckProjectileDefense72560 / Arc626e0.
        if(p->descriptor.concreteClass!=ProjectileClass::Straight&&p->descriptor.concreteClass!=ProjectileClass::Homing)throw std::runtime_error("UpdateDetonation derived CheckProjectileDefense override not reconstructed");
        if(!checkBaseProjectileDefense(handle,error))throw std::runtime_error(error);
        p=actor(handle);const float r=p->state.blastRadius,px=p->body.x,py=p->body.y;
        // GetUnits309b20 visits cells touched by the UNSCALED rectangle and
        // unions their Unit lists; it does NOT filter Unit centers to that rect.
        // Android broadphase adaptation: use the same sparse512-unit grid and
        // transformed polygon AABBs as ActorWorld obstacles. Native map-grid
        // origin/clamping/hash order remain distinct, explicitly documented.
        const float left=float(int32_t(px-r)),top=float(int32_t(py-r)),size=float(int32_t(r+r));
        const auto* owner=unit(world,p->request.ownerId); // UnitManager::Get(pFiredBy+0x30)
        const auto& data=currentData(*p);
        ActorSpatialGrid units;
        for(const auto id:world.registrationOrder()){const auto* a=unit(world,id);if(!a||!a->object)continue;
            SpatialObstacle entry;entry.id=id;entry.lifetime=a->entityHandle;
            float x=0,y=0;if(!world.location(id,x,y,error))throw std::runtime_error(error);entry.bounds={x,y,x,y};
            if(!a->geometry.empty()){entry.bounds={a->geometry[0].x,a->geometry[0].y,a->geometry[0].x,a->geometry[0].y};for(const auto& q:a->geometry){entry.bounds.minX=std::min(entry.bounds.minX,q.x);entry.bounds.maxX=std::max(entry.bounds.maxX,q.x);entry.bounds.minY=std::min(entry.bounds.minY,q.y);entry.bounds.maxY=std::max(entry.bounds.maxY,q.y);}}
            if(!units.update(entry,error))throw std::runtime_error(error);
        }
        std::vector<SpatialObstacle> candidates;if(!units.query({left,top,left+size,top+size},ObstacleGridCategory::All,candidates,error,false))throw std::runtime_error(error);
        for(const auto& candidate:candidates){const auto id=candidate.id;
            const auto* a=unit(world,id);if(!a||!a->object)continue;
            if(!actorProperty(world,id,"Thing.StopsProjectiles").boolean())continue; // ThingData+0x219
            if(primaryNoCollision(*a)&&!boolean(data,"IgnoreNoCollisionAnimData"))continue; // AnimData+0x8d
            bool affects=false;if(!projectileAffectsUnit(*p,id,providers.worldTime,providers.teams,affects,error))throw std::runtime_error(error);if(!affects)continue;
            float z=0;if(!world.zLocation(id,z,error))throw std::runtime_error(error);
            bool airborne=false;if(!world.unitAirborne(id,airborne,error))throw std::runtime_error(error);
            if(airborne)throw std::runtime_error("Unit::GetAirborneZ2f2930 airborne Slide AnimationData+0x2a8 value not reconstructed");
            if(!(z<=number(data,"MaxVictimZ"))||!(number(data,"MinVictimZ")<=float(actorProperty(world,id,"Thing.Tallness").number())+z))continue;
            const auto known=std::find(p->hitByDetonation.begin(),p->hitByDetonation.end(),id);
            if(!boolean(data,"MultipleUnitCollisions")&&known!=p->hitByDetonation.end()){
                if(boolean(data,"ResetCollisionOutsideDetonation")){bool in=false;if(!isInBlast(handle,id,in,error))throw std::runtime_error(error);if(!in)p->hitByDetonation.erase(known);}
                continue;
            }
            bool in=false;if(!isInBlast(handle,id,in,error))throw std::runtime_error(error);if(!in)continue;
            float ux=0,uy=0;if(!world.location(id,ux,uy,error))throw std::runtime_error(error);
            for(unsigned sight=0;sight<2;++sight){
                const bool fromOwner=sight==1;if(fromOwner?(!owner||!boolean(data,"DetonateLineOfSightFromOwner")):!boolean(data,"DetonateLineOfSight"))continue;
                ProjectileCollisionOperation op;op.kind=ProjectileCollisionOperationKind::HasLineOfSight;op.projectile=handle;op.victimId=id;op.unit=true;
                op.fromX=p->body.x;op.fromY=p->body.y;op.toX=ux;op.toY=uy;
                if(fromOwner&&!world.location(owner->id,op.fromX,op.fromY,error))throw std::runtime_error(error);
                // Same explicit z/backoff approximation as obstacle detonation.
                bool clear=false;if(!hasLineOfSight(op,clear,error))throw std::runtime_error(error);if(!clear){in=false;break;}
            }
            if(!in)continue;
            const float start=number(data,"DetonateArcStart"),end=number(data,"DetonateArcEnd");
            if(start!=end){float x=0,y=0;if(!thingLocation(*p,x,y,error))throw std::runtime_error(error);
                const float angle=normalizedAngle(std::atan2(-(uy-y),ux-x)),lo=normalizedAngle(start*0.01745329238474369f+p->body.angle),hi=normalizedAngle(end*0.01745329238474369f+p->body.angle);
                // Exact native isAngleBetween1ea670 branch comparisons. Its
                // endpoint convention excludes the interval when lo <= hi.
                if(!(lo>hi?(angle>hi&&lo>angle):(lo>angle||angle>hi)))continue;
            }
            if(std::find(p->hitByDetonation.begin(),p->hitByDetonation.end(),id)==p->hitByDetonation.end())p->hitByDetonation.push_back(id);
            if(!providers.impact)throw std::runtime_error("Unit detonation requires live impact providers");
            bool dodged=false;if(a->hasLife&&!checkProjectileDodge(*p,id,*providers.impact,dodged,error))throw std::runtime_error(error);
            if(dodged&&(number(data,"Damage")>0||number(data,"DamageLow")>0||number(data,"DamageHigh")>0))throw std::runtime_error("Detonation Unit::OnDodge2f3c10 consumer not reconstructed");
            bool reflected=false;
            if(boolean(data,"CanBeReflected")&&a->hasLife){float reflection=a->life.reflection;
                if(a->status)for(const auto h:a->status->snapshot().active){const WeaponEffectDefinition* e=nullptr;if(!providers.impact->effectData||!providers.impact->effectData(h,e,error)||!e)throw std::runtime_error(error.empty()?"Detonation reflection effect unavailable":error);reflection+=effectNumber(*e,"ReflectAddition");}
                if(reflection>0){double draw=0;if(!providers.impact->random||!providers.impact->random(draw,error))throw std::runtime_error(error.empty()?"Detonation reflection RNG unavailable":error);
                    if(draw<=actorProperty(world,id,"Life.ReflectionChance").number()){float x=0,y=0;if(!thingLocation(*p,x,y,error))throw std::runtime_error(error);
                        const float direction=normalizedAngle(std::atan2(-(uy-y),ux-x)+3.1415927410125732421875f),facing=normalizedAngle(a->object->angle),diff=std::abs(direction-facing);reflected=a->life.armorCoverage*.5f>std::min(diff,6.283185482025146484375f-diff);}
                }
            }
            if(reflected)continue;
            // CheckImmunities759a0 returns immediately for nonpositive duration;
            // a positive authored duration needs the real ECS immunity cache.
            if(number(data,"ImmunityDuration")>0)throw std::runtime_error("Detonation positive ImmunityDuration ECS cache not reconstructed");
            if(!providers.applyImpact||!providers.applyImpact(handle,id,error))throw std::runtime_error(error.empty()?"Unit detonation ApplyImpact unavailable":error);
            p=actor(handle);if(!p)throw std::runtime_error("Unit detonation impact lost projectile storage");
        }
        if(boolean(currentData(*p),"CheckObstacleImpact")){
            std::vector<int32_t> obstacles;if(!world.spatialObstacleCandidates(SpatialBounds{left,top,left+size,top+size},ObstacleGridCategory::StopsProjectiles,obstacles,error))throw std::runtime_error(error);
            for(const auto id:obstacles){const auto* a=world.actor(id);if(!a||!a->registered)continue;bool stops=false;if(!world.obstacleStops(id,true,stops,error))throw std::runtime_error(error);if(!stops)continue;
                if(!boolean(currentData(*p),"MultipleObstacleCollisions")&&std::find(p->hitByDetonation.begin(),p->hitByDetonation.end(),id)!=p->hitByDetonation.end())continue;
                bool in=false;if(!isInBlast(handle,id,in,error))throw std::runtime_error(error);if(!in)continue;
                // VA180072400..72491: DetonateLineOfSight(data+0x320) -> Thing::HasLineOfSight2dff90
                // from mLocation(+0x88) to the obstacle GetLocation (trailing arg 0); blocked skips.
                if(boolean(currentData(*p),"DetonateLineOfSight")){
                    ProjectileCollisionOperation op;op.kind=ProjectileCollisionOperationKind::HasLineOfSight;op.projectile=handle;op.victimId=id;op.unit=false;
                    // APPROXIMATION: z=0 / backoff=0 for the 2dff90 trailing argument (not decompiled).
                    op.fromX=p->body.x;op.fromY=p->body.y;if(!world.location(id,op.toX,op.toY,error))throw std::runtime_error(error);
                    bool clear=true;if(!hasLineOfSight(op,clear,error))throw std::runtime_error(error);if(!clear)continue;
                }
                // hash_set insert of Thing.mId (64dd0), then ApplyImpact76590(this, obstacle).
                mutated=true;p->hitByDetonation.push_back(id);
                if(!providers.applyImpact)throw std::runtime_error("Detonation ApplyImpact76590 requires actual hit consumer");
                if(!providers.applyImpact(handle,id,error))throw std::runtime_error(error.empty()?"Detonation ApplyImpact failed":error);
                p=actor(handle);if(!p)throw std::runtime_error("Detonation impact lost projectile storage");
            }
        }
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();if(mutated){faulted_=true;fault_=error;}return false;}
}
bool ProjectileWorld::detonate(uint64_t handle,const ProjectileDetonationProviders& providers,std::string& error){
    // Projectile::Detonate6d670 (REA ev_59bdd9864e1b701d332a893d661c09e17161e2f1a3e273017a1fec6df8e65f17,
    // .local/reports/native-projectile-detonate-pseudocode.c). ProjectileData
    // native offsets are ProjectileDataDef offsets + 0x50.
    bool mutated=false;
    try{auto* p=actor(handle);if(!p||!p->active||p->disposed||!p->firingWorld||!p->firingArsenal)throw std::runtime_error("Detonate requires actual active projectile source binding");
        const auto& d=currentData(*p);const bool multi=boolean(d,"MultiDetonate");const float maxRaw=number(d,"MaxDetonations");
        if(std::floor(maxRaw)!=maxRaw)throw std::runtime_error("Noninteger native MaxDetonations");const int32_t maximum=int32_t(maxRaw);
        // VA6d6b2-6d6e1 admission.
        if((p->state.detonated&&!multi)||(multi&&maximum>0&&p->state.detonations>=maximum)){error.clear();return true;}
        mutated=true;++p->state.detonations;p->state.detonated=true;
        p->state.blastTimeRemaining=number(d,"BlastDuration"); // +0x3dc = data+0x3d4
        p->hitByDetonation.clear();                                // hashtable reset VA6d70e-6d716
        if(!string(d,"SpawnOnDetonate").empty())throw std::runtime_error("Detonate SpawnOnDetonate weapon-interval gate/SpawnObject72c70 not reconstructed");
        if(!boolean(d,"HideDetonateGraphic")){ // data+0xf8
            // VA18006d77a..6d84d: GetLocation (mAttachedTo live: its location + mAttachOffset
            // +0x200/+0x204, else +0x88/+0x8c), AnimationManager::Create29e410(DetonateGraphic
            // data+0x100, this, location); non-null -> Animation::SetRadius2974e0(DamageRadius
            // data+0x30c) and PreferUnmodifiedTime29a5c0. GetLocation again, then
            // Create29e410(DetonateGraphic2 data+0x118, this, location) with no follow-up.
            for(const bool primary:{true,false}){const std::string name=string(currentData(*p),primary?"DetonateGraphic":"DetonateGraphic2");
                if(absentAnimation(name))continue; // Create returns null for empty/"null"
                float x=0,y=0;if(!thingLocation(*p,x,y,error))throw std::runtime_error(error);
                if(!detonateGraphic_)throw std::runtime_error("Detonate DetonateGraphic requires actual AnimationManager consumer: "+name);
                if(!detonateGraphic_(handle,name,x,y,primary,number(currentData(*p),"DamageRadius"),error))throw std::runtime_error(error.empty()?"Detonate DetonateGraphic creation failed: "+name:error);
                p=actor(handle);if(!p)throw std::runtime_error("Detonate graphic callback lost projectile storage");}
        }
        // SoundCue(data+0x2b0) -> AudioManager::PlayCue3D4a8a0 at location.
        // VA18006d8a6..6d8c4: GetLocation, SoundCue copy (43450), PlayCue3D4a8a0(cue, location).
        // Cue validity is the original event-description lookup, resolved by the parent
        // against the loaded audio catalog (the exporter leaves named cues unresolved).
        const auto cue=d.cues.find("DetonateSound");if(cue==d.cues.end())throw std::runtime_error("Actual DetonateSound cue unavailable");
        if(!(cue->second.validityKnown&&!cue->second.valid)){
            float sx=0,sy=0;if(!thingLocation(*p,sx,sy,error))throw std::runtime_error(error);
            if(!detonateSound_)throw std::runtime_error("Detonate DetonateSound requires actual PlayCue3D4a8a0 consumer: "+cue->second.name);
            if(!detonateSound_(cue->second.name,sx,sy,error))throw std::runtime_error(error.empty()?"Detonate DetonateSound playback failed":error);
            p=actor(handle);if(!p)throw std::runtime_error("Detonate sound callback lost projectile storage");
        }
        if(number(d,"DetonateScreenShakeSpeed")>0)throw std::runtime_error("Detonate Camera::Shake303970 consumer not reconstructed");
        if(!updateDetonation(handle,0,providers,error))throw std::runtime_error(error); // VA6d9cd with elapsed 0
        p=actor(handle);
        const auto* owner=unit(*p->firingWorld,p->state.ownerId); // UnitManager::Get(mOwnerId)
        if(owner&&boolean(currentData(*p),"CheckObstacleImpact")){
            // GetObstacles3071b0 rectangle uses DamageRadius; IsInBlast decides
            // CheckMovementReactionsWithChildren2c37e0.
            const float r=number(currentData(*p),"DamageRadius"),left=float(int32_t(p->body.x-r)),top=float(int32_t(p->body.y-r)),size=float(int32_t(r+r));
            std::vector<int32_t> obstacles;if(!p->firingWorld->spatialObstacleCandidates(SpatialBounds{left,top,left+size,top+size},ObstacleGridCategory::All,obstacles,error))throw std::runtime_error(error);
            for(const auto id:obstacles){const auto* a=p->firingWorld->actor(id);if(!a||!a->registered)continue;bool in=false;if(!isInBlast(handle,id,in,error))throw std::runtime_error(error);
                if(in){std::vector<int32_t> targets;if(!p->firingWorld->movementReactionTargets(id,targets,error))throw std::runtime_error(error);
                    if(!targets.empty())throw std::runtime_error("Detonate ScriptManager::OnMovementReaction28b060 not reconstructed: obstacle "+std::to_string(targets.front()));}}
        }
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();if(mutated){faulted_=true;fault_=error;}return false;}
}
bool ProjectileWorld::createStartFx(uint64_t handle,std::string& error){
    // Projectile::CreateStartFx6f1a0 (REA ev_d703a4d595e885f9a4239cc783d38fba57654fe07ac52d963a69252340302d3e).
    try{const auto* p=actor(handle);if(!p)throw std::runtime_error("CreateStartFx lost actual projectile");const auto& d=currentData(*p);
        if(p->state.useStartMarker&&!string(d,"StartAtMarker").empty())throw std::runtime_error("CreateStartFx StartAtMarker owner-marker relocation not reconstructed");
        if(p->state.fireIndex==0){
            if(!absentAnimation(string(d,"StartFx")))throw std::runtime_error("CreateStartFx projectile-owned StartFx animation not reconstructed: "+string(d,"StartFx"));
            if(!absentAnimation(string(d,"StartFx2")))throw std::runtime_error("CreateStartFx projectile-owned StartFx2 animation not reconstructed: "+string(d,"StartFx2"));
        }
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool ProjectileWorld::prepareDeath(uint64_t handle,int32_t victim,ProjectileDeathArguments& out,std::string& error){
    // Projectile::OnDeath6f5c0 (REA ev_c5fcabf5d99b33c1b6a9ef88badd9cd3e4dc218c8d807aaa59b04f03930fc38d)
    // through ScriptManager::OnProjectileDeath289ed0 argument construction.
    try{out={};const auto* p=actor(handle);if(!p||!p->firingWorld||!p->firingArsenal)throw std::runtime_error("OnDeath requires actual projectile source binding");
        auto& world=*p->firingWorld;const auto& d=currentData(*p);
        const auto* deathVictim=victim>=0?world.actor(victim):nullptr;if(victim>=0&&(!deathVictim||!deathVictim->registered||!deathVictim->object))throw std::runtime_error("OnDeath actual victim lifetime unavailable");
        if(deathVictim&&deathVictim->physics&&number(d,"OnDeathImpactForce")!=0)throw std::runtime_error("OnDeath nonzero victim physics force requires actual consumer");
        out.victimId=victim;out.killedVictim=deathVictim&&deathVictim->hasLife&&deathVictim->lifeStatus!=0;
        std::string dissipate=string(d,"DissipateGraphic");if(dissipate.empty())dissipate=string(d,"ImpactFx"); // VA6f6d4: victim null only
        if(!absentAnimation(dissipate)||p->primaryAnimation){if(!deathGraphic_||!deathGraphic_(handle,dissipate,error))throw std::runtime_error(error.empty()?"OnDeath actual animation factory unavailable":error);}
        if(!string(d,"SpawnOnDeath").empty()&&!p->state.blockSpawns)throw std::runtime_error("OnDeath SpawnOnDeath SpawnObject72c70 not reconstructed");
        const auto* owner=unit(world,p->request.ownerId); // UnitManager::Get(pFiredBy+0x30)
        if(owner&&(!string(d,"OnDeathFiredByAnimation").empty()||!string(d,"AddControlOnDeath").empty()||!string(d,"AddControlOnDeath2").empty()))throw std::runtime_error("OnDeath owner animation/AddWeaponControl consumer not reconstructed");
        // victim null: InvulnerableImpact derives from the last mThingsHit Thing.
        if(!p->thingsHit.empty()){
            // VA18006fd09: ThingManager::GetThing2e81b0(last mThingsHit entity).
            // IsInvulnerable651a0 -> InvulnerableImpact; else if GetLocation
            // differs and !IgnoreCoverageAngles: LifeComponent::
            // IsInvulnerableOnlyFromAngle2b7b00(arcDistance1ea5f0(body angle,
            // atan2(-(proj.y-last.y),proj.x-last.x))). ScriptManager passes the
            // last Thing's Lua table as LastHitTable and UnitsHit=its Unit flag.
            const ActorComponents* last=nullptr;
            for(const auto id:world.registrationOrder()){const auto* a=world.actor(id);if(a&&a->registered&&a->entityHandle==p->thingsHit.back()){last=a;break;}}
            if(last){
                out.lastHitId=last->id;out.lastHitIsUnit=last->definition&&last->definition->kind=="Unit";
                if(victim<0){
                if(!primaryCombat_)throw std::runtime_error("OnDeath last-hit invulnerability requires actual primary combat provider");
                bool primaryInvulnerable=false,noCoverage=false;if(!primaryCombat_(last->id,primaryInvulnerable,noCoverage,error))throw std::runtime_error(error);
                const bool lifeInvulnerable=last->hasLife&&last->invulnerable;
                if(primaryInvulnerable||lifeInvulnerable)out.invulnerableImpact=true;
                else{
                    float px=0,py=0,lx=0,ly=0;if(!thingLocation(*p,px,py,error)||!world.location(last->id,lx,ly,error))throw std::runtime_error(error);
                    if((px!=lx||py!=ly)&&!boolean(d,"IgnoreCoverageAngles")){
                        if(!last->hasLife||!last->object)throw std::runtime_error("OnDeath coverage test requires actual last-hit Life");
                        constexpr float turn=6.283185482025146484375f;
                        float angle=std::atan2((py-ly)*-1.f,px-lx);angle=angle-std::floor(angle/turn)*turn;
                        float a=last->object->angle,b=angle;a=a-std::floor(a/turn)*turn;b=b-std::floor(b/turn)*turn;
                        const float span=std::max(b,a)-std::min(b,a);const float distance=std::min(turn-span,span);
                        const float coverage=last->life.invulnerableCoverage;
                        out.invulnerableImpact=coverage>0&&!noCoverage&&coverage*0.5f>=distance;
                    }
                }
                }
            }
        }
        // Non-null victim branch VA6fb85..6fd9a uses GetImpactAngle(victim)
        // + pi for its coverage arc; the zero OnDeathImpactForce path has no
        // Physics mutation (ApplyForce2bd070 returns immediately for force0).
        if(deathVictim){
            if(!primaryCombat_)throw std::runtime_error("OnDeath victim requires actual primary combat flags");bool invulnerable=false,noCoverage=false;
            if(!primaryCombat_(victim,invulnerable,noCoverage,error))throw std::runtime_error(error);
            if(invulnerable||(deathVictim->hasLife&&deathVictim->invulnerable))out.invulnerableImpact=true;
            else if(!boolean(d,"IgnoreCoverageAngles")){
                float x=0,y=0,vx=0,vy=0;if(!thingLocation(*p,x,y,error)||!world.location(victim,vx,vy,error))throw std::runtime_error(error);
                if(x!=vx||y!=vy){ProjectileImpactGeometry geometry;if(!impactGeometry(handle,victim,geometry,error))throw std::runtime_error(error);
                    const float direction=normalizedAngle(geometry.angle+3.1415927410125732f),body=normalizedAngle(deathVictim->object->angle),span=std::abs(direction-body),distance=std::min(span,6.2831854820251465f-span);
                    out.invulnerableImpact=deathVictim->hasLife&&deathVictim->life.invulnerableCoverage>0&&!noCoverage&&deathVictim->life.invulnerableCoverage*.5f>=distance;}
            }
        }
        // Weapon volley block VA6fda8-701b5: Gun weapon (flags&0x44, same gate
        // as recordProjectileHit), !TreatProjectilesAsUniqueVolleys(+0x455), volley>=0.
        const auto* weapon=p->firingArsenal->weaponState(p->request.weaponName,error);if(!weapon)throw std::runtime_error(error);
        const auto unique=weapon->definition.fields.find("TreatProjectilesAsUniqueVolleys");
        if(unique==weapon->definition.fields.end()||!std::holds_alternative<bool>(unique->second))throw std::runtime_error("Native volley flag unavailable");
        if(weapon->definition.type==0&&!std::get<bool>(unique->second)&&p->state.volleyIndex>=0){
            const size_t slot=size_t(p->state.volleyIndex%4);const auto& times=weapon->volleyHitTimes[slot];
            if(!weapon->volleySlotInitialized[slot]||p->state.fireIndex<0||size_t(p->state.fireIndex)>=times.size())throw std::runtime_error("Actual GUN volley slot/fire index has not been created");
            // VA6fe49-6ff40 store into a freed copy (no persistent effect), then
            // last-index argmax over fresh copies; equality keeps later index.
            size_t best=0;for(size_t i=0;i<times.size();++i){if(times[i]>times[best]||(times[i]==times[best]&&best<i))best=i;}
            if(best==size_t(p->state.fireIndex)){out.lastProjectileDeath=true;out.firstUnitInVolley=weapon->volleyFirstHitId[slot];}
        }
        if(p->state.pauseTimeRemaining>0&&(p->attachedAnimation||!p->attachedAnimations.empty()))throw std::runtime_error("OnDeath paused attached-animation unpause consumer not reconstructed");
        out.name=d.name;out.weaponName=weapon->definition.name;out.attackerId=owner?owner->id:-1;
        out.hasLocation=true;out.locationX=p->body.x;out.locationY=p->body.y;
        out.angleDegrees=p->body.angle*57.29578f;out.fireIndex=p->state.fireIndex;out.volley=p->state.volleyIndex;
        out.isDeflected=p->state.flipAllegiance; // +0x4c0
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
}

namespace hades {
bool barrelObstacleAt(ActorWorld& world,float x,float y,int32_t& hit,std::string& error){
    // CollisionParameters ctor306ab0 then GunWeapon::Fire stores: byte0=0 (no
    // units), byte1=1 (obstacles), +4=1 (StopsProjectiles list and
    // Obstacle::StopsProjectiles2c5640), +0xc=1 (return after first result),
    // +0xe=4 (PointInPolygon2ceaf0 against Obstacle+0x1d8 at +0x20), +0x14=0
    // minimum Z (ctor), rect {(int)(x-.5),(int)(y-.5),1,1}. +0x10 MaxVictimZ is
    // read only by the Unit branch. Grid cell/hash order is portable spatial
    // order; only the first containing obstacle is reported in either case.
    hit=0;
    try{
        if(!std::isfinite(x)||!std::isfinite(y))throw std::runtime_error("Nonfinite native barrel point");
        const float left=float(int(x-0.5f)),top=float(int(y-0.5f));
        std::vector<int32_t> candidates;if(!world.spatialObstacleCandidates(SpatialBounds{left,top,left+1,top+1},ObstacleGridCategory::StopsProjectiles,candidates,error))throw std::runtime_error(error);
        for(const int32_t id:candidates){const auto* a=world.actor(id);if(!a||!a->registered||!a->object)continue;
            float z=0;if(!world.zLocation(id,z,error))throw std::runtime_error(error);
            if(!(0<=z+float(actorProperty(world,id,"Thing.Tallness").number())))continue;
            bool stops=false;if(!world.obstacleStops(id,true,stops,error))throw std::runtime_error(error);if(!stops)continue;
            const float threshold=float(actorProperty(world,id,"MaxUpwardVelocityToCollide").number());
            if(threshold>0&&a->physics&&a->physics->upVelocity>threshold)continue;
            if(!a->geometryAvailable){if(a->geometryRequired)throw std::runtime_error("Actual barrel obstacle polygon unavailable");continue;}
            if(!pointInPolygon(a->geometry,x,y))continue;
            hit=id;break;
        }
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
}
