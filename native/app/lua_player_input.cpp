#include "lua_runtime.hpp"
#include "weapon_execution.hpp"
#include "input_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
extern "C" {
#include <lua.h>
}
namespace {
float scalar(const hades::ActorWorld&w,int32_t id,const char*k){const auto*p=w.currentProperty(id,k);if(!p)throw std::runtime_error(std::string("Original movement field unavailable: ")+k);float n=float(p->number());if(!std::isfinite(n))throw std::runtime_error("Nonfinite movement property");return n;}
bool flag(const hades::ActorWorld&w,int32_t id,const char*k){const auto*p=w.currentProperty(id,k);if(!p)throw std::runtime_error(std::string("Original movement flag unavailable: ")+k);return p->boolean();}
std::string text(const hades::ActorWorld&w,int32_t id,const char*k){const auto*p=w.currentProperty(id,k);if(!p)throw std::runtime_error(std::string("Original movement animation field unavailable: ")+k);return p->text();}
constexpr float Tau=6.2831854820251465f;
float wrap(float a){return a-std::floor(a/Tau)*Tau;}
float stepAngle(float from,float to,float maximum){
 // stepTowardAngle VA1801ea880: shortest arc, native negative tie at pi.
 from=wrap(wrap(from));const float target=wrap(to),delta=target-from;
 if(std::min(std::abs(delta),Tau-std::abs(delta))<=maximum)return to;
 const bool positive=std::abs(delta)<3.1415927410125732421875f?delta>0:delta<0;
 return from+(positive?maximum:-maximum);
}
bool fieldBool(const hades::WeaponSnapshot&w,const char*k){const auto i=w.definition.fields.find(k);if(i==w.definition.fields.end()||!std::holds_alternative<bool>(i->second))throw std::runtime_error(std::string("Actual weapon input flag unavailable: ")+k);return std::get<bool>(i->second);}
}
bool ScriptRuntime::setLocalInput(float x,float y,const std::vector<std::string>&down,std::string&error){
 if(!std::isfinite(x)||!std::isfinite(y)||std::abs(x)>1.001f||std::abs(y)>1.001f||down.size()>32){error="Android local input outside bounds";return false;}for(const auto&name:down)if(!weaponCatalog_||weaponCatalog_->controlIndex(name)<0){error="Original native action control unavailable: "+name;return false;}const float length=std::hypot(x,y);if(length>1){x/=length;y/=length;}localMoveX_=x;localMoveY_=y;localActionDown_=down;std::sort(localActionDown_.begin(),localActionDown_.end());localActionDown_.erase(std::unique(localActionDown_.begin(),localActionDown_.end()),localActionDown_.end());error.clear();return true;
}
bool ScriptRuntime::advanceLocalPlayerInput(float dt,std::string&error){try{
 if(!std::isfinite(dt)||dt<0||dt>1)throw std::runtime_error("Actual local input clock outside bounds");float speedConfig=0;if(!configurationFloat("SpeedMultiplier",speedConfig,error))return false;
 for(const auto&p:snapshotInputPlayers(state_)){if(p.playerIndex!=1)continue;auto*a=actorWorld_.actor(p.activeUnitId);if(!a||!a->registered||a->entityHandle!=p.activeUnitLifetime||!a->object||!a->arsenal||!a->object->alive||a->lifeStatus!=0)continue;auto&body=*a->object;auto&move=localMovement_[a->entityHandle];if(!move.initialized){move.initialized=true;move.goalAngle=move.cosmeticAngle=move.cosmeticAngleTarget=body.angle;move.lastMoveStartX=body.x;move.lastMoveStartY=body.y;}
 const bool primaryInputDisabled=a->animation.active()&&(a->animation.clip()->flags&hades::ActorDisableInput);const bool allowed=p.inputAllowed&&a->allowInput&&!primaryInputDisabled;float x=allowed?localMoveX_:0,y=allowed?localMoveY_:0;if(x>0&&!isRecoveredControlEnabled(state_,"Right"))x=0;if(x<0&&!isRecoveredControlEnabled(state_,"Left"))x=0;if(y>0&&!isRecoveredControlEnabled(state_,"Down"))y=0;if(y<0&&!isRecoveredControlEnabled(state_,"Up"))y=0;const float magnitude2=x*x+y*y;
 if(allowed)for(const auto&name:localActionDown_)if(!std::binary_search(previousLocalActionDown_.begin(),previousLocalActionDown_.end(),name)){if(!dispatchCallbacks("OnControlPressed",name.c_str(),a->id,[&](lua_State*L,std::string&){lua_pushstring(L,name.c_str());lua_setfield(L,-2,"Control");return true;}))return false;}
 if(allowed&&magnitude2>0&&a->physics){
   if(!selfEffects_.refreshUnit(a->id,error))return false;bool canMove=false;if(!actorWorld_.unitCanMove(a->id,speedConfig,canMove,error))return false;float elapsed=0;if(!hades::physicsComponentElapsed(dt,worldSpeedModifier_,a->timeModifierFraction,a->timeMultiplier,elapsed,error))return false;
   hades::UnitMoveData data;data.canMove=canMove;data.onlyForward=flag(actorWorld_,a->id,"CanOnlyMoveForward");data.playerControlled=body.playerControlled;data.speed=a->speed;data.speedAddition=a->speedAddition;data.speedModifier=a->speedModifier;data.playerSpeedMultiplier=speedConfig;data.acceleration=scalar(actorWorld_,a->id,"Acceleration");data.ownerAngle=body.angle;const float angle=wrap(std::atan2(-y,x));const float fraction=magnitude2<a->lowSpeedThreshold?a->lowSpeedFraction:1;
   // Current native Unit.CanRotate: primary DisableOwnerRotation, timer and
   // active/pending EffectData.DisableRotate; GetRotationSpeed multiplies pending.
   bool rotate=!(a->animation.active()&&(a->animation.clip()->flags&hades::ActorDisableRotation))&&a->status&&a->status->timers.rotate<=0;float rotation=scalar(actorWorld_,a->id,"RotationSpeed");if(a->status){const auto status=a->status->snapshot();for(auto h:status.active){const auto*d=selfEffects_.effectData(h,error);if(!d)return false;auto i=d->fields.find("DisableRotate");if(i==d->fields.end())throw std::runtime_error("Actual effect DisableRotate unavailable");if(std::get<bool>(i->second))rotate=false;}for(auto h:status.pending){const auto*d=selfEffects_.effectData(h,error);if(!d)return false;auto f=d->fields.find("DisableRotate"),r=d->fields.find("RotationMultiplier");if(f==d->fields.end()||r==d->fields.end())throw std::runtime_error("Actual pending rotation effect unavailable");if(std::get<bool>(f->second))rotate=false;rotation*=float(std::get<double>(r->second));}}
   std::string lookup;const bool strafing=a->arsenal->isStrafing("",lookup);if(!lookup.empty()){error=lookup;return false;}if(rotate&&rotation>0&&!strafing&&canMove)move.goalAngle=angle;
   // ApplyDirectForce 0x1802bcffb multiplies ThingData+0x1fc = mSpinFactor (Thing.SpinFactor); no ImpactRotation field exists. Evidence: .local/reports/native-impact-rotation-default-recovery.json
   hades::PhysicsForceOwner force;force.impactRotation=scalar(actorWorld_,a->id,"Thing.SpinFactor");hades::PhysicsForceCallbacks callbacks;callbacks.onForce=[this,a](float,std::string& detail){return onActorForce(a->id,true,detail);};callbacks.addComponent=[a](std::string&){a->physicsPending=true;return true;};if(!hades::applyUnitMoveInput(*a->physics,move,data,angle,fraction,elapsed,force,callbacks,error))return false;
   // Unit::MoveInput VA1802f6415..6501: update the current animation's
   // PlaySpeedMultiplier component only after CanMove accepted this input.
   // A newly selected stop/idle animation keeps its own default multiplier.
   if(canMove&&a->animation.active()){
     const auto definition=actorAnimationDefinitions_.find(a->animation.clip()->name);
     if(definition==actorAnimationDefinitions_.end())throw std::runtime_error("Movement animation source unavailable");
     const auto* native=definition->second.get("definition")->get("NativeMoveSpeed");
     if(native&&native->number()>0)a->animation.setPlaySpeedMultiplier(std::hypot(a->physics->velocity.x,a->physics->velocity.y)/float(native->number()));
   }
 }
 // PlayerUnitInput::HandleInput2eccb0 runs CheckWeaponControl2ef100 before
 // the weapon input loop: lose-control removals (RemoveWeaponControl2ff510
 // with its charge cancel), AddWeaponControl(pGainedControlFrom), and
 // RequireControlRelease clears for released controls (raw held bit).
 if(allowed){
  const auto held=[&](const std::string&name){return !name.empty()&&std::binary_search(localActionDown_.begin(),localActionDown_.end(),name);};
  std::vector<std::string> lose;if(!a->arsenal->checkWeaponControl(held,lose,error))return false;
  for(const auto&name:lose){hades::WeaponOutput op;op.kind=hades::WeaponOutputKind::RemoveWeaponControl;op.ownerId=a->id;op.name=name;hades::WeaponTriggerReply ignored;
   if(!consumeWeaponOperation(op,ignored,error))return false;a=actorWorld_.actor(p.activeUnitId);if(!a||!a->arsenal){error="Weapon control removal lost actual owner";return false;}
   if(!a->arsenal->finishLostControl(name,error))return false;}
  a->arsenal->clearReleasedControls(held);
 }
 // Actual native primary/alternate + secondary/disallowed control semantics.
 const auto down=[&](const std::string&name){return allowed&&!name.empty()&&isRecoveredControlEnabled(state_,name)&&std::binary_search(localActionDown_.begin(),localActionDown_.end(),name);};
 const auto pressed=[&](const std::string&name){return down(name)&&!std::binary_search(previousLocalActionDown_.begin(),previousLocalActionDown_.end(),name);};
 for(const auto&name:a->arsenal->controllableWeapons()){const auto*w=a->arsenal->weaponState(name,error);if(!w)return false;const auto controls=w->definition.controls;if(!(down(controls[0])||down(controls[1]))||(!controls[2].empty()&&!down(controls[2]))||down(controls[3]))continue;if(!fieldBool(*w,"FullyAutomatic")&&!w->charging&&!pressed(controls[0])&&!pressed(controls[1]))continue;if(!fieldBool(*w,"CanPullTriggerWithoutMoving")&&!move.moving&&magnitude2==0)continue;
   hades::WeaponOwnerFrame frame;if(!actorWorld_.weaponOwnerFrame(a->id,worldTime_,speedConfig,false,allowed,frame,error))return false;hades::WeaponFireRequest request;request.angle=magnitude2>0?wrap(std::atan2(-y,x)):body.angle;const auto* cancel=w->definition.fields.contains("CanCancelDisables")?&w->definition.fields.at("CanCancelDisables"):nullptr;if(!cancel)throw std::runtime_error("Actual weapon cancellation input policy missing");const bool canAttack=std::get<bool>(*cancel)?frame.canAttackAllowCancellation:frame.canAttack;
   // PlayerUnitInput::CheckWeaponInput2ef3f0: ((pAnim attack gate clear &&
   // UnitStatus::CanAttack2fcd50) || UnitStatus::ShouldLocKTrigger2fce40).
   // ShouldLocKTrigger: WeaponData AcceptTriggerLockRequests, not during
   // cooldown when IgnoreTriggerLockDuringCooldown, and every active then
   // pending DisableAttack effect has RequestTriggerLock (at least one).
   if(frame.animationBlocksAttack||!canAttack){
    bool lock=false;const auto accept=w->definition.fields.find("AcceptTriggerLockRequests"),ignore=w->definition.fields.find("IgnoreTriggerLockDuringCooldown");
    if(accept==w->definition.fields.end()||ignore==w->definition.fields.end()||!std::holds_alternative<bool>(accept->second)||!std::holds_alternative<bool>(ignore->second))throw std::runtime_error("Actual weapon trigger-lock policy unavailable");
    if(std::get<bool>(accept->second)&&!(std::get<bool>(ignore->second)&&w->cooldownTimeRemaining>0)&&a->status){
     const auto status=a->status->snapshot();bool denied=false;
     for(const auto* list:{&status.active,&status.pending})for(const auto h:*list){if(denied)break;const auto* d=selfEffects_.effectData(h,error);if(!d)return false;
      const auto da=d->fields.find("DisableAttack"),rt=d->fields.find("RequestTriggerLock");if(da==d->fields.end()||rt==d->fields.end()||!std::holds_alternative<bool>(da->second)||!std::holds_alternative<bool>(rt->second))throw std::runtime_error("Actual effect trigger-lock fields unavailable");
      if(std::get<bool>(da->second)){if(!std::get<bool>(rt->second))denied=true;else lock=true;}}
     if(denied)lock=false;
    }
    if(!lock)continue;
   }if(!a->arsenal->requestFire(name,request,frame,error)&&!error.empty())return false;
 }
 }
 previousLocalActionDown_=localActionDown_;error.clear();return true;
 }catch(const std::exception&e){error=e.what();return false;}}
bool ScriptRuntime::advanceUnitMovement(float dt,std::string&error){try{
 float speedConfig=0;if(!configurationFloat("SpeedMultiplier",speedConfig,error))return false;if(!std::isfinite(dt)||dt<0||dt>1)throw std::runtime_error("Invalid actual Unit.UpdateMovement clock");
 // Unit::UpdateMovement2f65e0 and UpdateAngle2f8810 are Unit phases,
 // including Lua MoveAI-driven enemies. Local-player input is a separate phase.
 const auto units=actorWorld_.registrationOrder();
 for(const auto id:units){auto*a=actorWorld_.actor(id);if(!a||!a->registered||!a->definition||a->definition->kind!="Unit"||!a->object||!a->physics||!a->status)continue;auto&body=*a->object;auto&physics=*a->physics;auto&move=localMovement_[a->entityHandle];if(!move.initialized){move.initialized=true;move.goalAngle=move.cosmeticAngle=move.cosmeticAngleTarget=body.angle;move.lastMoveStartX=body.x;move.lastMoveStartY=body.y;}
  float elapsed=0;if(!hades::physicsComponentElapsed(dt,worldSpeedModifier_,a->timeModifierFraction,a->timeMultiplier,elapsed,error))return false;
  // Unit::UpdateAngle VA1802f883d..8a00 updates cosmetic facing BEFORE
  // body rotation, independently of CanRotate/DisableRotate. Source degree
  // speeds have already passed SGD_DeserializeAngle in the actor catalog.
  float cosmeticTarget=body.angle;
  if(flag(actorWorld_,a->id,"CosmeticallyTargetPlayerUnit")){
   const auto players=snapshotInputPlayers(state_);if(players.empty())throw std::runtime_error("Cosmetic targeting requires the actual first Player");
   float x=0,y=0,tx=0,ty=0;if(!actorWorld_.location(a->id,x,y,error)||!actorWorld_.location(players.front().activeUnitId,tx,ty,error))return false;
   cosmeticTarget=wrap(std::atan2(-(ty-y),tx-x));move.cosmeticAngleTarget=cosmeticTarget;
  }
  move.cosmeticAngle=wrap(stepAngle(move.cosmeticAngle,cosmeticTarget,scalar(actorWorld_,a->id,"CosmeticRotationSpeed")*elapsed));
  bool canRotate=!(a->animation.active()&&(a->animation.clip()->flags&hades::ActorDisableRotation))&&a->status->timers.rotate<=0;const auto status=a->status->snapshot();float rotation=scalar(actorWorld_,a->id,"RotationSpeed");for(auto h:status.active){const auto*d=selfEffects_.effectData(h,error);if(!d)return false;const auto i=d->fields.find("DisableRotate");if(i==d->fields.end())throw std::runtime_error("Actual rotation effect unavailable");if(std::get<bool>(i->second))canRotate=false;}for(auto h:status.pending){const auto*d=selfEffects_.effectData(h,error);if(!d)return false;const auto disable=d->fields.find("DisableRotate"),multiplier=d->fields.find("RotationMultiplier");if(disable==d->fields.end()||multiplier==d->fields.end())throw std::runtime_error("Actual pending rotation effect unavailable");if(std::get<bool>(disable->second))canRotate=false;rotation*=float(std::get<double>(multiplier->second));}
  if(canRotate&&rotation>0){for(const auto*k:{"LeftAngleClamp","LeftAngleHardClamp","RightAngleClamp","RightAngleHardClamp"})if(scalar(actorWorld_,a->id,k)!=0)throw std::runtime_error("Nonzero native movement angle clamp requires full clamp provider");const float next=wrap(stepAngle(body.angle,move.goalAngle,rotation*elapsed));if(body.angle!=next){if(!actorWorld_.setAngle(a->id,next,error))return false;}}
  const float max=(a->speed+a->speedAddition)*a->speedModifier*(body.playerControlled?speedConfig:1.f),velocity2=physics.velocity.x*physics.velocity.x+physics.velocity.y*physics.velocity.y;
  bool airborne=false;if(!actorWorld_.unitAirborne(a->id,airborne,error))return false;
  if(flag(actorWorld_,a->id,"HaltOnSlowMovement")&&move.speedFraction<=0&&move.previousSpeedFraction>0&&velocity2>0&&velocity2<max*max&&!airborne)physics.velocity={};
  float startX=0,startY=0;if(!actorWorld_.location(a->id,startX,startY,error))return false;float targetX=startX+physics.moveShift.x*elapsed,targetY=startY+physics.moveShift.y*elapsed;const float shiftLength=std::hypot(physics.moveShift.x,physics.moveShift.y);if(move.unappliedSpeed>0&&shiftLength>0){targetX+=physics.moveShift.x/shiftLength*move.unappliedSpeed;targetY+=physics.moveShift.y/shiftLength*move.unappliedSpeed;move.unappliedSpeed=0;}
  move.previousSpeedFraction=move.speedFraction;move.speedFraction=0;
  if(targetX!=startX||targetY!=startY){if(a->attachedParent)throw std::runtime_error("Moving attached local Unit requires source parent-relative location setter");if(!actorWorld_.refreshGeometry(a->id,error))return false;if(a->geometry.size()<3)throw std::runtime_error("Actual moving Unit source polygon unavailable");
   // Unit::UpdateMovement2f65e0 VA1802f69a0..9be: mLastCollisionTranslation(+0x410)=0, then
   // ApplyFullShift2f76d0(target, target-location, UnitData+0x15e StopShiftOnCollision, true)
   // (shared exact push-out, lua_combat.cpp). The DAT_1806a7f1f&&PlayerControlled direct
   // SetLocation branch is an engine debug global (not set in the shipped game), not modelled.
   unitCollisionSets_[a->entityHandle].lastTranslationX=0;unitCollisionSets_[a->entityHandle].lastTranslationY=0;
   if(!unitApplyFullShift(a->id,targetX,targetY,targetX-startX,targetY-startY,flag(actorWorld_,a->id,"StopShiftOnCollision"),true,error))return false;
   a=actorWorld_.actor(id);if(!a||!a->object){error="ApplyFullShift lost actual Unit";return false;}
  }
  const float dx=body.x-startX,dy=body.y-startY,distance2=dx*dx+dy*dy;bool moving=move.moving;
  if((body.x!=0||body.y!=0)&&distance2>0){const float sinceX=body.x-move.lastMoveStartX,sinceY=body.y-move.lastMoveStartY;if(!move.moving&&(distance2>(dt*120)*(dt*120)||sinceX*sinceX+sinceY*sinceY>100))moving=true;else if(distance2<(dt*30)*(dt*30))moving=false;}else moving=false;
  if(moving!=move.moving){move.moving=moving;move.lastMoveStartX=body.x;move.lastMoveStartY=body.y;if(body.playerControlled&&!dispatchCallbacks(moving?"OnPlayerMoveStarted":"OnPlayerMoveStopped",body.name.c_str(),a->id))return false;}
 }
 error.clear();return true;
 }catch(const std::exception&e){error=e.what();return false;}}
bool ScriptRuntime::updateLocalMoveAnimation(int32_t id,std::string&error){try{
 auto*a=actorWorld_.actor(id);if(!a||!a->registered||!a->object||!a->animation.active())return true;const auto m=localMovement_.find(a->entityHandle);if(m==localMovement_.end())return true;const auto&move=m->second;const auto*clip=a->animation.clip();const auto definition=actorAnimationDefinitions_.find(clip->name);if(definition==actorAnimationDefinitions_.end())throw std::runtime_error("Current Unit move animation source unavailable");const auto&d=*definition->second.get("definition");const auto sourceText=[&](const char*k){auto*p=d.get(k);return p?p->text():std::string{};};
 float config=0;if(!configurationFloat("SpeedMultiplier",config,error))return false;bool canMove=false;if(!actorWorld_.unitCanMove(id,config,canMove,error))return false;const float velocity=a->physics?std::hypot(a->physics->velocity.x,a->physics->velocity.y):0;
 const auto idle=text(actorWorld_,id,"Thing.Graphic"),start=text(actorWorld_,id,"StartGraphic"),stop=text(actorWorld_,id,"StopGraphic");
 // Unit::IsPlayingMoveAnimation VA1802f1d80 compares Start/Move/Low/High/Max
 // names, or accepts a current animation with its own StopAnimation. Sword
 // run overrides MoveGraphic through MoveAnimation and relies on this last
 // branch; omitting it leaves the hero running in place after stick release.
 bool isMove=!sourceText("StopAnimation").empty();
 for(const auto*k:{"MoveGraphic","StartGraphic","LowSpeedAnimation","MaxSpeedAnimation","HighSpeedAnimation"})if(clip->name==text(actorWorld_,id,k))isMove=true;
 const auto playMove=[&](){
   std::string next=sourceText("MoveAnimation");
   if(next.empty()){
     const float maximum=(a->speed+a->speedAddition)*a->speedModifier*(a->object->playerControlled?config:1.f);const auto low=text(actorWorld_,id,"LowSpeedAnimation");
     if(!low.empty()&&velocity>0&&velocity<maximum*scalar(actorWorld_,id,"LowSpeedThreshold"))next=low;
     else if(!start.empty()&&(clip->name==idle||clip->name==stop))next=start;
     else if(clip->name==start)return true;
     else{const auto maximumAnimation=text(actorWorld_,id,"MaxSpeedAnimation"),high=text(actorWorld_,id,"HighSpeedAnimation");if(!maximumAnimation.empty()&&velocity>=maximum)next=maximumAnimation;else if(!high.empty()&&velocity>scalar(actorWorld_,id,"HighSpeedThreshold"))next=high;else next=text(actorWorld_,id,"MoveGraphic");}
   }
   return next.empty()||next==clip->name||setActorAnimation(id,next,error);
 };
 // Unit::UpdateAnimation VA1802f1b18..1c25: finished non-chain primaries
 // return to movement, the Unit stop graphic, or its idle graphic. Completion
 // must not depend on CancelOnOwnerMove or the primary's movement disable.
 if(a->animation.finished()){
   if(move.moving&&canMove&&!text(actorWorld_,id,"MoveGraphic").empty())return playMove();
   const auto& next=isMove&&!stop.empty()?stop:idle;
   return next.empty()||setActorAnimation(id,next,error);
 }
 // VA1802f1c2f..1cdf: only living movable Units cancel an unfinished primary,
 // and CancelOnOwnerMove requires self-applied movement, not recoil/knockback.
 if(!flag(actorWorld_,id,"CanMove")||!a->object->alive)return true;
 const bool selfApplied=!a->physics||a->physics->lastForceSelfApplied;
 if(move.moving&&velocity>0&&(((clip->flags&hades::ActorCancelOnMove)&&canMove&&selfApplied)||isMove))return playMove();
 if(isMove&&(!move.moving||velocity<=0)){
   // VA1802f1ce6..1d40: a source StopAnimation override is considered only
   // when Unit.StopGraphic exists; otherwise finish-on-idle or idle applies.
   std::string next;
   if(!stop.empty()){next=sourceText("StopAnimation");if(next.empty())next=stop;}
   else{if(flag(actorWorld_,id,"FinishMoveAnimationOnIdle")&&!a->animation.finished())return true;next=idle;}
   if(next!=clip->name&&!next.empty())return setActorAnimation(id,next,error);
 }
 return true;
 }catch(const std::exception&e){error=e.what();return false;}}
