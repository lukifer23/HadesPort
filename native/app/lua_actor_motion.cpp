#include "lua_runtime.hpp"
#include "recovered_runtime.hpp"
#include "platform_log.hpp"
#include <strings.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string_view>
extern "C" {
#include <lua.h>
}
namespace {
float motionNumber(const hades::SourceValue&v,const char*key,float fallback=0){const auto*p=v.get(key);if(!p)return fallback;const double n=p->number();if(!std::isfinite(n)||std::abs(n)>1e7)throw std::runtime_error(std::string("Actual script motion numeric bounds: ")+key);return float(n);}
bool motionFlag(const hades::SourceValue&v,const char*key,bool fallback=false){const auto*p=v.get(key);return p?p->boolean():fallback;}
// TransitionHelper<float>::CalcCurrentValue 0x550f0 (pingPong cleared by Reset):
// duration==0 -> end, else sgg::Interpolate(start,end,fraction,smooth,easeIn,easeOut).
float axisValue(const hades::ObstacleMoveAxis&a){if(a.duration==0)return a.end;hades::BodyTransitionOptions o;o.smoothStep=a.smooth;o.easeIn=a.easeIn;o.easeOut=a.easeOut;return hades::bodyInterpolate(a.start,a.end,a.fraction,o);}
// TransitionHelper<float>::Reset 0x551f0 (phase 0): start/end/duration/remaining,
// fraction 0, pingPong/+0x32/smooth cleared, easeIn/easeOut (+0..+7) zeroed,
// callback cleared, rate 1, active. +8 TimeModifierFraction untouched.
void axisReset(hades::ObstacleMoveAxis&a,float start,float end,float duration){a.start=start;a.end=end;a.duration=a.remaining=duration;a.fraction=0;a.smooth=false;a.easeIn=a.easeOut=0;a.rate=1;a.active=true;}
// TransitionHelper<float>::SetNewTarget 0x220ba0: no-op when end unchanged;
// else start=CalcCurrentValue, end, duration=remaining=d, fraction 0.
void axisSetNewTarget(hades::ObstacleMoveAxis&a,float end,float duration){if(a.end==end)return;a.start=axisValue(a);a.end=end;a.duration=a.remaining=duration;a.fraction=0;}
// Obstacle::UpdateTransitionHelper 0x2c6760: tmf<1 -> dt/=((1-W20)*tmf+W20);
// remaining-=dt/rate; fraction=minss(maxss(1-remaining/duration,0),1).
void axisUpdate(hades::ObstacleMoveAxis&a,float dt,float worldModifier){if(a.tmf<1.f)dt/=((1.f-worldModifier)*a.tmf+worldModifier);a.remaining-=dt/a.rate;float f=1.f-a.remaining/a.duration;f=f>0.f?f:0.f;f=f<1.f?f:1.f;a.fraction=f;}
int32_t motionId(const hades::SourceValue&v){const double n=v.number();if(n<1||n>INT32_MAX||n!=std::trunc(n))throw std::runtime_error("Actual script motion Thing ID bounds");return int32_t(n);}
}
void ScriptRuntime::registerActorMotionActions(){const char*names[]{"Teleport","AdjustZLocation","Move","Stop","AngleTowardTarget","SetAngle","SetGoalAngle","Shift","Unattach"};for(int mode=0;mode<9;++mode){lua_pushlightuserdata(state_,this);lua_pushinteger(state_,mode);lua_pushcclosure(state_,actorMotionAction,2);lua_setglobal(state_,names[mode]);}}
int ScriptRuntime::actorMotionAction(lua_State*L){char message[2048]{};bool ok=false;{auto*self=runtime(L);std::string error;ok=self&&self->actorMotionActionImpl(L,int(lua_tointeger(L,lua_upvalueindex(2))),error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.empty()?"Native actor motion runtime unavailable":error.c_str());}if(!ok){lua_pushstring(L,message);return lua_error(L);}return 0;}
bool ScriptRuntime::actorMotionActionImpl(lua_State*L,int mode,std::string&error){try{
 hades::SourceValue args;if(!hades::readSourceValue(L,1,args,error))return false;
 if(mode==8){
  // Thing.RunScriptAction2e4166..4255: AttachedToId is the optional parent
  // filter; DestinationId is not read. Native Detach is virtual slot0x110.
  std::vector<int32_t> selected;if(!resolveObjectTargets(L,false,selected,error))return false;
  int32_t expected=0;if(const auto* value=args.get("AttachedToId")){const double n=value->number();if(!std::isfinite(n)||n<INT32_MIN||n>INT32_MAX)throw std::runtime_error("Native Unattach AttachedToId integer bounds");expected=int32_t(n);}
  for(const auto id:selected){auto* a=actorWorld_.actor(id);if(!a||!a->registered||!a->object)continue;
   const auto* parent=actorWorld_.actor(a->attachedParent);
   if(expected>0&&parent&&parent->registered&&parent->entityHandle==a->attachedParentLifetime&&parent->id!=expected)continue;
   bool detached=false;if(!actorWorld_.detach(id,detached,error))return false;
   if(detached&&a->physics){auto& p=*a->physics;
    // Physics.Halt2bcd20 clears MoveShift/Velocity, upward velocity and
    // last-self-applied flag, preserving acceleration/spin/footing state.
    p.moveShift=p.velocity={};p.upVelocity=0;p.lastForceSelfApplied=false;
    if(p.impulsingSoundId&&!audio_.stop(uint32_t(p.impulsingSoundId),false,error))return false;p.impulsingSoundId=0;
    p.ignoreGravity=false;a->physicsPending=true;
   }
   if(motionFlag(args,"CenterGeometry"))throw std::runtime_error("Native Unattach CenterGeometry polygon-center mutation not reconstructed");
  }
  error.clear();return true;
 }
 // ScriptAction.Shift274fb0: each selected Thing target=GetLocation+Offset,
 // virtual MoveTo(slot31), speed/easing/duration/tmf identical to Move. No centroid.
 if(mode==7){
  if(args.get("Name")||args.get("Names"))throw std::runtime_error("Native Shift name selection not reconstructed");
  if(motionNumber(args,"MinDuration")!=0&&motionNumber(args,"MaxDuration")!=0)throw std::runtime_error("Native Shift randomized duration requires exact engine RNG");
  auto screenArgs=args;std::get<hades::SourceValue::Object>(screenArgs.value)["ShiftThingsByOffset"].value=true;
  bool screen=false;if(!screenMoveImpl(screenArgs,screen,error))return false;
  std::vector<int32_t> selected;if(!resolveObjectTargets(L,false,selected,error))return false;
  for(auto id:selected){auto*a=actorWorld_.actor(id);if(!a||!a->registered||!a->object)continue;
   if(!a->definition||a->definition->kind!="Obstacle")throw std::runtime_error("Native Unit Shift MoveTo branch not reconstructed");
   float x=0,y=0;if(!actorWorld_.location(id,x,y,error))return false;
   const float duration=args.get("Duration")?motionNumber(args,"Duration"):motionNumber(args,"DurationSeconds");
   if(!obstacleMoveTo(id,x+motionNumber(args,"OffsetX"),y+motionNumber(args,"OffsetY"),duration,motionNumber(args,"Speed"),motionFlag(args,"SmoothStep"),motionNumber(args,"EaseIn"),motionNumber(args,"EaseOut"),motionNumber(args,"TimeModifierFraction",1),error))return false;
  }
  error.clear();return true;
 }
 // Screen (UseScreenLocation) obstacles are not ActorWorld actors: Move takes
 // the Obstacle::MoveTo screen branch; other modes fail rather than no-op.
 // Teleport of screen (GameUI) obstacles: runWithDestination25eea0 passes
 // check=false, so Obstacle::TeleportTo VA1802c3ab0 goes straight to
 // ShiftLocationWithChildren2c3510(target - location). Portable screen bodies
 // carry no structural children, so the shift is the body location itself.
 if(mode==0){std::vector<int32_t> screenSources;bool world=false;
  for(const char*k:{"Id","Ids"})if(const auto*p=args.get(k)){const auto add=[&](const hades::SourceValue&v){const double n=v.number();if(!std::isfinite(n)||n<INT32_MIN||n>INT32_MAX)return;const auto*o=findObjectSnapshot(L,int32_t(n));if(o&&o->screenSpace)screenSources.push_back(int32_t(n));else world=true;};if(std::string(k)=="Id")add(*p);else for(const auto&v:p->array())add(v);}
  if(!screenSources.empty()){
   if(world)throw std::runtime_error("Native Teleport mixed screen/world source selection not reconstructed");
   for(const auto*k:{"Name","Names","DestinationName","DestinationNames","RelativeOffsetId"})if(args.get(k))throw std::runtime_error(std::string("Native screen Teleport argument not reconstructed: ")+k);
   for(const auto*k:{"DestinationIsScreenRelative","UseCurrentLocation","KeepRelativeOffsets","ShiftThingsByOffset"})if(motionFlag(args,k))throw std::runtime_error(std::string("Uncovered native screen Teleport branch: ")+k);
   std::vector<const ObjectSnapshot*> dests;for(const char*k:{"DestinationId","DestinationIds"})if(const auto*p=args.get(k)){const auto add=[&](const hades::SourceValue&v){const auto*o=findObjectSnapshot(L,int32_t(v.number()));if(!o)return;if(!o->screenSpace)throw std::runtime_error("Native screen Teleport toward world Thing not reconstructed");dests.push_back(o);};if(std::string(k)=="DestinationId")add(*p);else for(const auto&v:p->array())add(v);}
   if(dests.empty()&&motionFlag(args,"OnlyIfDestinationExits")){error.clear();return true;}
   float x=motionNumber(args,"OffsetX"),y=motionNumber(args,"OffsetY");
   if(!dests.empty()){float sx=0,sy=0;for(const auto*o:dests){sx+=o->x;sy+=o->y;}x+=sx/float(dests.size());y+=sy/float(dests.size());}
   for(const auto id:screenSources){auto*s=screenAnimations_.thing(id);if(!s||s->dead||!s->body)continue;if(s->body->x==x&&s->body->y==y)continue;s->body->x=x;s->body->y=y;s->body->graphicsDirty=true;}
   error.clear();return true;}}
 {bool screen=false;if(mode==2){if(!screenMoveImpl(args,screen,error))return false;if(screen){error.clear();return true;}}
  else for(const char*k:{"Id","Ids"})if(const auto*p=args.get(k)){const auto check=[&](const hades::SourceValue&v){const auto*o=findObjectSnapshot(L,int32_t(v.number()));if(o&&o->screenSpace)throw std::runtime_error("Native screen Thing motion action not reconstructed");};if(std::string(k)=="Id")check(*p);else for(const auto&v:p->array())check(v);}}
 const auto select=[&](const char*single,const char*plural){std::vector<int32_t>out;const auto add=[&](const hades::SourceValue&v){const int32_t id=motionId(v);const auto*a=actorWorld_.actor(id);if(a&&a->registered&&a->object)out.push_back(id);};if(const auto*p=args.get(single))add(*p);if(const auto*p=args.get(plural))for(const auto&v:p->array())add(v);std::sort(out.begin(),out.end());out.erase(std::unique(out.begin(),out.end()),out.end());return out;};
 auto sources=select("Id","Ids");const auto targets=select("DestinationId","DestinationIds");
 if(mode==1){const float distance=motionNumber(args,"Distance"),duration=motionNumber(args,"Duration");if(duration<0)throw std::runtime_error("Negative native Z transition duration");for(auto id:sources){auto*a=actorWorld_.actor(id);if(duration==0){if(!shiftActorZ(id,distance,error))return false;}else{hades::ActorZTransition z;z.ownerId=id;z.lifetime=a->entityHandle;hades::BodyTransitionOptions options;options.duration=duration;bool changed=false;if(!z.transition.adjust(a->object->z+distance,a->object->z,options,changed,error))return false;actorMotion_.heights[z.lifetime]=std::move(z);}}error.clear();return true;}
 // Thing::RunScriptAction 0x72 SetAngle / 0x73 SetGoalAngle (VA1802e22f9..):
 // Angle default -1e6 degrees, x(pi/180) in double. SetAngle Duration==0 ->
 // virtual SetAngle; otherwise both use Body+0x80 (FUN_1802b7440, target
 // wrapped to [0,2pi), initial 0). Unit::RunScriptAction 0x2f3d70 overrides:
 // SetAngle = virtual SetAngle then the Thing path; SetGoalAngle =
 // CompleteAngle ? Unit::SetCompleteAngle 0x2f8770 : Unit::SetGoalAngle 0x2f85f0.
 if(mode==5||mode==6){
  const float radians=float(double(motionNumber(args,"Angle",-1e6f))*0.017453292519943295);
  hades::BodyTransitionOptions options;options.easeOut=motionNumber(args,"EaseOut");options.easeIn=motionNumber(args,"EaseIn");options.smoothStep=motionFlag(args,"SmoothStep");
  options.duration=args.get("Duration")?motionNumber(args,"Duration"):motionNumber(args,"DurationSeconds");
  constexpr float tau=6.2831855f;const float wrapped=radians-std::floor(radians/tau)*tau;
  for(auto id:sources){auto*a=actorWorld_.actor(id);const bool unit=a->definition&&a->definition->kind=="Unit";
   if(unit&&mode==6){auto&body=*a->object;auto&movement=localMovement_[a->entityHandle];
    if(motionFlag(args,"CompleteAngle")){if(!actorWorld_.setAngle(id,radians,error))return false;movement.cosmeticAngle=movement.cosmeticAngleTarget=radians;}
    else if(!unitGoalAngleSound(id,radians,error))return false;
    if(!movement.initialized){movement.initialized=true;movement.lastMoveStartX=body.x;movement.lastMoveStartY=body.y;}
    movement.goalAngle=radians;continue;}
   if(unit&&!actorWorld_.setAngle(id,radians,error))return false;
   if(mode==5&&options.duration==0){if(!actorWorld_.setAngle(id,radians,error))return false;continue;}
   if(!actorWorld_.adjustAngle(id,wrapped,options,error))return false;
  }
  error.clear();return true;
 }
 if(mode==3){for(auto id:sources){auto*a=actorWorld_.actor(id);actorMotion_.moves.erase(a->entityHandle);auto&move=localMovement_[a->entityHandle];move.speedFraction=0;if(motionFlag(args,"StopRotation",true))move.goalAngle=a->object->angle;}error.clear();return true;}
 // Thing::RunScriptAction 0x74 AngleTowardTarget (VA1802e24c9): destination
 // set location (first element / count), none or (0,0) -> no-op; atan2f of
 // (-(dy), dx) wrapped twice to [0,2pi), then the Body+0x80 helper with
 // {EaseOut, EaseIn, SmoothStep, GetDuration, initial 0}. Units override
 // (0x1802f451a): CompleteAngle ? SetCompleteAngle : SetGoalAngle; Duration
 // and easing are not read on that path.
 if(mode==4){
  bool things=false;for(auto id:sources){auto*a=actorWorld_.actor(id);if(!(a->definition&&a->definition->kind=="Unit"))things=true;}
  if(things&&!targets.empty()){
   for(const auto*k:{"Name","Names","DestinationName","DestinationNames"})if(args.get(k))throw std::runtime_error("Native Thing AngleTowardTarget name selection requires actual name-query provider");
   if(targets.size()>1)throw std::runtime_error("Native Thing AngleTowardTarget multi-destination hash-set order not reconstructed");
   float tx=0,ty=0;if(!actorWorld_.location(targets.front(),tx,ty,error))return false;
   if(tx!=0||ty!=0){
    hades::BodyTransitionOptions options;options.easeOut=motionNumber(args,"EaseOut");options.easeIn=motionNumber(args,"EaseIn");options.smoothStep=motionFlag(args,"SmoothStep");
    options.duration=args.get("Duration")?motionNumber(args,"Duration"):motionNumber(args,"DurationSeconds");
    constexpr float tau=6.2831854820251465f;
    for(auto id:sources){auto*a=actorWorld_.actor(id);if(a->definition&&a->definition->kind=="Unit")continue;
     float angle=std::atan2((ty-a->object->y)*-1.f,tx-a->object->x);angle-=std::floor(angle/tau)*tau;angle-=std::floor(angle/tau)*tau;
     if(!actorWorld_.adjustAngle(id,angle,options,error))return false;}
   }
  }
 }
 // ScriptAction::Move 0x274230 -> virtual slot31 MoveTo. Obstacle sources take
 // Obstacle::MoveTo 0x2c5bf0; the remaining (Unit) sources keep the Unit path.
 if(mode==2){std::vector<int32_t> obstacles,others;for(auto id:sources){const auto*a=actorWorld_.actor(id);(a->definition&&a->definition->kind=="Obstacle"?obstacles:others).push_back(id);}
  if(!obstacles.empty()){if(!obstacleMoveImpl(args,sources,obstacles,targets,error))return false;sources=std::move(others);if(sources.empty()){error.clear();return true;}}}
 if(mode==2||mode==4){if(targets.empty()){error.clear();return true;}for(const auto*k:{"Name","Names","DestinationName","DestinationNames","Duration","EaseIn","EaseOut","SmoothStep","Speed","Distance","Angle","TimeModifierFraction","DestinationIsScreenRelative"})if(args.get(k)&&!(mode==4&&(std::string_view(k)=="Duration"||std::string_view(k)=="EaseIn"||std::string_view(k)=="EaseOut"||std::string_view(k)=="SmoothStep")))throw std::runtime_error(std::string("Uncovered selected native motion argument: ")+k);float x=motionNumber(args,"OffsetX"),y=motionNumber(args,"OffsetY"),sumX=0,sumY=0;for(auto id:targets){float tx=0,ty=0;if(!actorWorld_.location(id,tx,ty,error))return false;sumX+=tx;sumY+=ty;}x+=sumX/targets.size();y+=sumY/targets.size();for(auto id:sources){auto*a=actorWorld_.actor(id);if(mode==4&&!(a->definition&&a->definition->kind=="Unit"))continue;if(a->definition->kind!="Unit"||a->attachedParent)throw std::runtime_error("Script Unit target requires unparented actual Unit");auto&move=localMovement_[a->entityHandle];if(!move.initialized){move.initialized=true;move.goalAngle=move.cosmeticAngle=move.cosmeticAngleTarget=a->object->angle;move.lastMoveStartX=a->object->x;move.lastMoveStartY=a->object->y;}if(mode==2){
 const auto* sourceMode=args.get("Mode");const bool precise=sourceMode&&strcasecmp(sourceMode->text().c_str(),"Precise")==0;
 hades::ActorMoveOrder order{id,a->entityHandle,x,y,precise,{},{}};
 order.path.repathRemaining=actorMotion_.pathRepathDelays[a->entityHandle];
 {
  // MoveAIData ctor VA1800d3310 plus source-authored MoveAI data. The
  // destination is still passed through original CalcPath's integer graph.
  const auto* ai=a->definition->properties.get("MoveAI");auto& c=order.pathConfig;
  const auto num=[&](const char*k,float fallback){return ai?motionNumber(*ai,k,fallback):fallback;};
  const auto flag=[&](const char*k,bool fallback){return ai?motionFlag(*ai,k,fallback):fallback;};
  for(const auto*k:{"StayOnCamera","FinishAtGoal","RequiresLoS"})if(flag(k,false))throw std::runtime_error(std::string("Native default Move data branch not reconstructed: ")+k);
  if(ai)if(const auto* fail=ai->get("OnFailBehavior")){
   // MoveAIDataDef reader VA1800d3a31..3a91: exact strcmp, nonmatching text -> 0.
   if(!std::holds_alternative<std::string>(fail->value))throw std::runtime_error("Native MoveAI OnFailBehavior requires source enum text");
   const auto& t=fail->text();c.onFailBehavior=t=="GOTO_NEAREST_TO_GOAL"?0:(t=="GOTO_LAST_NODE_FILLED"?1:0);}
  if(ai&&ai->get("Algorithm"))throw std::runtime_error("Native authored MoveAI Algorithm parser not reconstructed");
  if(num("AlternateNodeDistance",0)!=0||num("NodeSuccessDistanceRequiresLoS",0)!=0)throw std::runtime_error("Native alternate/LOS path state not reconstructed");
  order.targetHeadingShift=num("TargetHeadingShift",0);
  c.nodeDistance=num("NodeDistance",c.nodeDistance);c.nodeSuccessDistance=num("NodeSuccessDistance",c.nodeSuccessDistance);c.lookAheadDistance=num("LookAheadDistance",c.lookAheadDistance);c.lookAheadMultiplier=motionNumber(args,"LookAheadMultiplier",1.f);
  c.successDistance=num("SuccessDistance",c.successDistance);
  if(motionNumber(args,"SuccessDistance")>0)actorMotion_.successDistanceOverrides[a->entityHandle]=motionNumber(args,"SuccessDistance");
  if(const auto o=actorMotion_.successDistanceOverrides.find(a->entityHandle);o!=actorMotion_.successDistanceOverrides.end())c.successDistance=o->second;
  if(precise)c.successDistance=32.f;
  // VA180274a1b..a77: LiveOffsetFromId (Int), LiveOffsetDistance, LiveOffsetAngle
  // (degrees -> radians in double) are rewritten on every Unit Move.
  {const double from=motionNumber(args,"LiveOffsetFromId");order.liveOffsetFromId=from>=INT32_MIN&&from<=INT32_MAX?int32_t(from):0;}
  order.liveOffsetDistance=motionNumber(args,"LiveOffsetDistance");order.liveOffsetAngle=float(double(motionNumber(args,"LiveOffsetAngle"))*0.017453292519943295);
  // VA1802748ac..b0: only the first source receives the first destination
  // Thing (native hash order; portable sorted order is an approximation for
  // multi-destination calls). SetTarget then tracks that Thing's live location.
  if(sources.front()==id&&!targets.empty()){const auto* t=actorWorld_.actor(targets.front());if(t&&t->registered&&t->object){order.targetId=t->id;order.targetLifetime=t->entityHandle;}}
  c.moveWithoutPath=flag("MoveWithoutPath",false);c.avoidLocationDistance=num("AvoidLocationDistance",0);
  c.repathDelayMin=num("RepathDelayMin",c.repathDelayMin);c.repathDelayMax=num("RepathDelayMax",c.repathDelayMax);c.repathTargetMoveDistance=num("RepathTargetMoveDistance",c.repathTargetMoveDistance);c.stopToRotate=flag("StopToRotate",c.stopToRotate);c.slowToRotate=flag("SlowToRotate",c.slowToRotate);
  if(ai&&ai->get("NodeCountLimit"))c.nodeCountLimit=int32_t(motionNumber(*ai,"NodeCountLimit"));if(ai&&ai->get("SkippedNodeLimit"))c.skippedNodeLimit=int32_t(motionNumber(*ai,"SkippedNodeLimit"));
  if(c.repathTargetMoveDistance<0||c.repathDelayMin<0||c.repathDelayMax<c.repathDelayMin)throw std::runtime_error("Native default Move repath bounds");
 }
 if(x!=0||y!=0)actorMotion_.moves[a->entityHandle]=std::move(order);
}else{constexpr float tau=6.2831854820251465f;const float raw=std::atan2(-(y-a->object->y),x-a->object->x),angle=raw-std::floor(raw/tau)*tau;if(!motionFlag(args,"CompleteAngle")&&!unitGoalAngleSound(id,angle,error))return false;move.goalAngle=angle;if(motionFlag(args,"CompleteAngle")){move.cosmeticAngle=move.cosmeticAngleTarget=angle;if(!actorWorld_.setAngle(id,angle,error))return false;}}}error.clear();return true;}
 if(mode!=0)throw std::runtime_error("Unknown original script motion action");
 for(const auto*k:{"Name","Names","DestinationName","DestinationNames"})if(args.get(k))throw std::runtime_error("Native Teleport name selection requires actual name-query provider");
 for(const auto*k:{"DestinationIsScreenRelative","UseCurrentLocation","KeepRelativeOffsets","ShiftThingsByOffset"})if(motionFlag(args,k))throw std::runtime_error(std::string("Uncovered native Teleport branch: ")+k);
 if(args.get("RelativeOffsetId"))throw std::runtime_error("Native Teleport relative owner offset requires retained relative provider");
 if(targets.empty()&&motionFlag(args,"OnlyIfDestinationExits")){error.clear();return true;}
 float x=motionNumber(args,"OffsetX"),y=motionNumber(args,"OffsetY");if(!targets.empty()){float sumX=0,sumY=0;for(auto id:targets){float tx=0,ty=0;if(!actorWorld_.location(id,tx,ty,error))return false;sumX+=tx;sumY+=ty;}x+=sumX/targets.size();y+=sumY/targets.size();}
 // runWithDestination25eea0 ordinary Teleport dispatch passes check=false,
 // propagate=true. Unit.TeleportTo then SetLocation and StopMoving; it does
 // not halt Physics velocity or use ForceToValidLocation on this branch.
 for(auto id:sources){auto*a=actorWorld_.actor(id);if(a->attachedParent)throw std::runtime_error("Teleport of attached Thing needs source parent-relative setter");if(a->definition->kind!="Unit")throw std::runtime_error("Teleport obstacle needs native movement/grid callback provider");a->object->x=x;a->object->y=y;a->object->graphicsDirty=a->object->geometryDirty=true;if(!actorWorld_.refreshGeometry(id,error)||!actorWorld_.propertiesDirty(id,error))return false;if(auto i=localMovement_.find(a->entityHandle);i!=localMovement_.end())i->second.speedFraction=0;}
 error.clear();return true;
 }catch(const std::exception&e){error=e.what();return false;}}

// Thing.ShiftZLocation2e0ae0 + Unit.ShiftZLocation2f2e40 /
// Obstacle.ShiftZLocation2c42b0 (propagate=true). This setter
// deliberately does not fabricate Physics membership; real MoveInput/OnForce
// admission owns that component transition.
bool ScriptRuntime::shiftActorZ(int32_t id,float delta,std::string&error){try{
 auto*a=actorWorld_.actor(id);if(!a||!a->registered||!a->object)throw std::runtime_error("Native ShiftZ actual owner unavailable");
 if((a->definition->kind!="Unit"&&a->definition->kind!="Obstacle")||a->attachedParent)throw std::runtime_error("Native ShiftZ requires unparented Unit/Obstacle source branch");
 const auto*limit=actorWorld_.currentProperty(id,"Thing.MaxZ");if(!limit)throw std::runtime_error("Native ShiftZ compiled MaxZ unavailable");const float maximum=float(limit->number());
 if(!std::isfinite(delta)||!std::isfinite(maximum)||!std::isfinite(a->object->z+delta))throw std::runtime_error("Native ShiftZ numeric bounds");
 if(delta!=0){a->object->z=std::min(a->object->z+delta,maximum);a->object->graphicsDirty=true;}
 // Obstacle override calls LocationChanged(false) after the base setter,
 // even when delta==0 (2c42c6..2c42cf). The actual provider updates spatial,
 // draw bounds, interaction, lights, occluders and attached animations.
 if(a->definition->kind=="Obstacle")return actorWorld_.obstacleLocationChanged(id,false,error);
 return actorWorld_.propertiesDirty(id,error); // Unit override marks its sort score.
 }catch(const std::exception&e){error=e.what();return false;}}
bool ScriptRuntime::advanceActorMotion(float dt,std::string&error){try{
 if(!std::isfinite(dt)||dt<0||dt>1)throw std::runtime_error("Native script motion elapsed bounds");
 // World game-update transitions use actual world dt, not the screen clock.
 // Selected RoomOpening has UndoModifier=1. Modified event clocks require
 // the original event-specific UndoModifier before extending this adapter.
 if(worldSpeedModifier_!=1&&!actorMotion_.heights.empty())throw std::runtime_error("Script Z transition modified event clock not yet recovered");
 for(auto i=actorMotion_.heights.begin();i!=actorMotion_.heights.end();){auto&a=i->second;auto*owner=actorWorld_.actor(a.ownerId);if(!owner||!owner->registered||owner->entityHandle!=a.lifetime){i=actorMotion_.heights.erase(i);continue;}
  if(!a.transition.advance(dt,1,error)||!shiftActorZ(a.ownerId,a.transition.current()-owner->object->z,error))return false;
  if(!a.transition.active())i=actorMotion_.heights.erase(i);else ++i;
 }
 // ObstacleManager::Update 0x2c80c0 (World manager list, dt*World+0x20 per
 // World::Update 0x314fdf): for each obstacle with X.active&&Y.active (1802c8795):
 // UpdateTransitionHelper X,Y; delta=Calc-GetLocation; ShiftLocationWithChildren
 // (1802c8862); clear both when (X.duration==0||X.remaining<0)&&(Y...) (1802c8867..ae).
 // Approximation: runs at this point of the Android world step; the native
 // manager-list position relative to Unit movement is not recovered.
 {const float managerDt=dt; // Incoming elapsed is already World-scaled.
  for(auto i=actorMotion_.obstacles.begin();i!=actorMotion_.obstacles.end();){auto&m=i->second;auto*a=actorWorld_.actor(m.ownerId);
   if(!a||!a->registered||a->isDisposed||a->entityHandle!=m.lifetime||!a->object){i=actorMotion_.obstacles.erase(i);continue;}
   if(!(m.x.active&&m.y.active)){i=actorMotion_.obstacles.erase(i);continue;}
   axisUpdate(m.x,managerDt,worldSpeedModifier_);axisUpdate(m.y,managerDt,worldSpeedModifier_);
   float x=0,y=0;if(!actorWorld_.location(m.ownerId,x,y,error))return false;
   if(!shiftObstacleWithChildren(m.ownerId,axisValue(m.x)-x,axisValue(m.y)-y,error))return false;
   if((m.x.duration==0||m.x.remaining<0)&&(m.y.duration==0||m.y.remaining<0)){i=actorMotion_.obstacles.erase(i);continue;}
   ++i;}}
 float config=0;if(!configurationFloat("SpeedMultiplier",config,error))return false;
 const auto number=[&](int32_t id,const char*k){const auto*p=actorWorld_.currentProperty(id,k);if(!p)throw std::runtime_error(std::string("Script Move actual property unavailable: ")+k);const float n=float(p->number());if(!std::isfinite(n))throw std::runtime_error("Script Move nonfinite source property");return n;};
 const auto boolean=[&](int32_t id,const char*k){const auto*p=actorWorld_.currentProperty(id,k);if(!p)throw std::runtime_error(std::string("Script Move actual flag unavailable: ")+k);return p->boolean();};
 for(auto i=actorMotion_.moves.begin();i!=actorMotion_.moves.end();){auto& order=i->second;auto*a=actorWorld_.actor(order.ownerId);if(!a||!a->registered||a->entityHandle!=order.lifetime||!a->object||!a->object->alive){i=actorMotion_.moves.erase(i);continue;}
  float x=0,y=0;if(!actorWorld_.location(a->id,x,y,error))return false;auto& move=localMovement_[a->entityHandle];
  // MoveAI::CalcGoalLocation VA1802e8960: Target::GetLocation2ebc40 (live Thing
  // location while it is in the World, else the stored location), then with a
  // target Thing: +TargetHeadingShift along its body angle, and LiveOffsetFromId
  // > 0 adds Distance at wrap(wrap(atan2(-(t.y-f.y), t.x-f.x)) + Angle).
  // APPROXIMATION: a removed target keeps the computed Move location here;
  // native keeps the Target's previous location field.
  if(order.targetId){const auto* t=actorWorld_.actor(order.targetId);
   if(t&&t->registered&&t->object&&t->entityHandle==order.targetLifetime){
    float gx=0,gy=0;if(!actorWorld_.location(t->id,gx,gy,error))return false;
    if(order.targetHeadingShift>0){gx+=std::cos(t->object->angle)*order.targetHeadingShift;gy+=-(std::sin(t->object->angle)*order.targetHeadingShift);}
    if(order.liveOffsetFromId>0){const auto* f=actorWorld_.actor(order.liveOffsetFromId);
     if(f&&f->registered&&f->object&&f->object->active){float fx=0,fy=0;if(!actorWorld_.location(f->id,fx,fy,error))return false;
      constexpr float tau=6.2831855f;
      float tlx=0,tly=0;if(!actorWorld_.location(t->id,tlx,tly,error))return false;
      float angle=std::atan2((tly-fy)*-1.f,tlx-fx);angle-=std::floor(angle/tau)*tau;angle+=order.liveOffsetAngle;angle-=std::floor(angle/tau)*tau;
      gx+=std::cos(angle)*order.liveOffsetDistance;gy+=-(std::sin(angle)*order.liveOffsetDistance);}}
    order.goalX=gx;order.goalY=gy;}}
  // Same strict success check as default Move; precise changes only its
  // radius to 32 (FindPath VA1802e8da0..2e8e27), before CalcPath and RNG.
  const float goalDx=order.goalX-x,goalDy=order.goalY-y;
  if(goalDx*goalDx+goalDy*goalDy<order.pathConfig.successDistance*order.pathConfig.successDistance){move.speedFraction=0;i=actorMotion_.moves.erase(i);continue;}
  if(!a->physics)throw std::runtime_error("Script Move actual Physics allocation missing");
  float targetX=order.goalX,targetY=order.goalY;
  bool directFallback=false;
  {
   auto& path=order.path;const auto& c=order.pathConfig;path.repathRemaining-=dt;actorMotion_.pathRepathDelays[a->entityHandle]=path.repathRemaining;
   if(path.rotationPending){if(a->object->angle!=move.goalAngle){++i;continue;}path.rotationPending=false;}
   bool freshPath=false;
   const float movedX=path.lastGoalX-order.goalX,movedY=path.lastGoalY-order.goalY;
   if(!path.initialized||path.nodes.empty()||movedX*movedX+movedY*movedY>=c.repathTargetMoveDistance*c.repathTargetMoveDistance){
    if(path.repathRemaining<=0){
     const auto validateNode=[&](float nx,float ny,bool& blocked,std::string& detail){return unitPathNodeBlocked(a->id,nx,ny,c.avoidLocationDistance>0,blocked,detail);};
     if(!hades::planUnitPath(x,y,order.goalX,order.goalY,c,validateNode,path.nodes,error))return false;
     freshPath=true;
     double draw=0;if(!nextRecoveredEngineRandom(state_,draw))throw std::runtime_error("Native MoveAI repath RNG unavailable");
     volatile float scaled=float(draw)*(c.repathDelayMax-c.repathDelayMin);path.repathRemaining=scaled+c.repathDelayMin;
     actorMotion_.pathRepathDelays[a->entityHandle]=path.repathRemaining;
     path.lastGoalX=order.goalX;path.lastGoalY=order.goalY;path.initialized=true;
     hades::log(hades::Severity::Info,"HadesLua","NATIVE_UNIT_PATH owner=%d nodes=%zu from=%.2f,%.2f goal=%.2f,%.2f",a->id,path.nodes.size(),x,y,order.goalX,order.goalY);
    }
   }
   // FindPath2e8f01: an exhausted search is not an engine failure. Authored
   // MoveWithoutPath admits direct MoveInput only when the real list is empty.
   if(path.nodes.empty()){if(!c.moveWithoutPath){++i;continue;}directFallback=true;}
   else{
    // SetNextDestination2e8f90: retain current node until strict arrival;
    // after arrival or a fresh search, try the native swept-polygon shortcut
    // to the first(end) node. Collision-disabled actors take that shortcut.
    auto node=path.nodes.back();const float ndx=float(node.x)-x,ndy=float(node.y)-y;
    const bool arrived=ndx*ndx+ndy*ndy<c.nodeSuccessDistance*c.nodeSuccessDistance;
    if(arrived)path.nodes.pop_back();
    if(freshPath||arrived){if(!path.nodes.empty()){
      bool clear=!boolean(a->id,"CollideWithObstacles");
      if(!clear&&!unitLineOfMovement(a->id,float(path.nodes.front().x),float(path.nodes.front().y),c.avoidLocationDistance>0,clear,error))return false;
      if(clear){const auto end=path.nodes.front();path.nodes.assign(1,end);}
      node=path.nodes.back();
    }}
    targetX=float(node.x);targetY=float(node.y);
   }
  }
  const float dx=targetX-x,dy=targetY-y,distance=std::hypot(dx,dy);
  if(!selfEffects_.refreshUnit(a->id,error))return false;bool canMove=false;if(!actorWorld_.unitCanMove(a->id,config,canMove,error))return false;
  float elapsed=0;if(!hades::physicsComponentElapsed(dt,worldSpeedModifier_,a->timeModifierFraction,a->timeMultiplier,elapsed,error))return false;
  hades::UnitMoveData data;data.canMove=canMove;data.onlyForward=boolean(a->id,"CanOnlyMoveForward");data.playerControlled=a->object->playerControlled;data.speed=a->speed;data.speedAddition=a->speedAddition;data.speedModifier=a->speedModifier;data.playerSpeedMultiplier=config;data.acceleration=number(a->id,"Acceleration");data.ownerAngle=a->object->angle;
  constexpr float tau=6.2831854820251465f;const float raw=std::atan2(-dy,dx),angle=raw-std::floor(raw/tau)*tau;
  // Native SetNextDestination uses source speed, angle and elapsed in its
  // SlowToRotate fraction; the empty-list MoveWithoutPath branch skips it.
  const float maximum=(data.speed+data.speedAddition)*data.speedModifier*(data.playerControlled?config:1.f);const float step=maximum*elapsed;float fraction=step>distance&&step>0?distance/step:1.f;
  if(!directFallback){const auto& c=order.pathConfig;const float difference=std::abs(std::remainder(angle-a->object->angle,tau));
   bool rotateOnly=c.stopToRotate&&difference>.0001f;
   if(!c.slowToRotate)fraction=1;
   else if(step<=distance&&maximum>0){float rotation=number(a->id,"RotationSpeed");if(a->status)for(const auto handle:a->status->snapshot().pending){const auto* effect=selfEffects_.effectData(handle,error);if(!effect)return false;const auto multiplier=effect->fields.find("RotationMultiplier");if(multiplier==effect->fields.end())throw std::runtime_error("Native MoveAI pending rotation modifier unavailable");rotation*=float(std::get<double>(multiplier->second));}rotateOnly=rotateOnly||(rotation>0&&difference/rotation>distance/maximum*.5f);}
   if(rotateOnly){if(!unitGoalAngleSound(a->id,angle,error))return false;move.goalAngle=angle;order.path.rotationPending=true;++i;continue;}
  }
  if(directFallback)fraction=1;
  if(!unitGoalAngleSound(a->id,angle,error))return false;
  move.goalAngle=angle;
  // ApplyDirectForce 0x1802bcffb multiplies ThingData+0x1fc = mSpinFactor (Thing.SpinFactor); no ImpactRotation field exists. Evidence: .local/reports/native-impact-rotation-default-recovery.json
  hades::PhysicsForceOwner force;force.impactRotation=number(a->id,"Thing.SpinFactor");hades::PhysicsForceCallbacks callbacks;callbacks.onForce=[this,a](float,std::string&detail){return onActorForce(a->id,true,detail);};callbacks.addComponent=[a](std::string&){a->physicsPending=true;return true;};
  if(!hades::applyUnitMoveInput(*a->physics,move,data,angle,fraction,elapsed,force,callbacks,error))return false;
  // Unit::MoveInput VA1802f63cc stores primary velocity/NativeMoveSpeed.
  if(canMove&&a->animation.active()){const auto definition=actorAnimationDefinitions_.find(a->animation.clip()->name);if(definition==actorAnimationDefinitions_.end())throw std::runtime_error("Script Move primary definition unavailable");const auto* native=definition->second.get("definition")->get("NativeMoveSpeed");if(native&&native->number()>0)a->animation.setPlaySpeedMultiplier(std::hypot(a->physics->velocity.x,a->physics->velocity.y)/float(native->number()));}
  ++i;
 }
 error.clear();return true;
 }catch(const std::exception&e){error=e.what();return false;}}
// ScriptAction::Move 0x274230 duration branch for Obstacle sources
// (.local/reports/hud-motion-Move.json, hud-motion-semantics.json item 4):
//  1802742c3 Angle(-1e6) != -1e6 && Speed != 0 -> TranslateComponent 0x180274c50.
//  1802743ae destinations GetActiveMapThings(DestinationIDs,DestinationNames).
//  1802743de ShiftThingsByOffset -> off=(OffsetX,OffsetY); elif destinations:
//  dest=sum(GetLocation)/count (180274480..529); elif Distance!=0&&Angle!=-1e6:
//  rad=float(double(Angle)*pi/180), off=(Distance*cosf, -(Distance*sinf)).
//  180274604..633 dest+=(OffsetX,OffsetY) (an empty destination set leaves
//  dest at the origin plus offsets). Unless shifting, sources centroid
//  (180274710..7b0) over every active source. Per source (1802748b8..990):
//  off==(0,0) ? loc-centroid+dest : loc+off. Then TimeModifierFraction(1),
//  EaseOut, EaseIn, SmoothStep, Speed, getDuration; MoveTo 180274b6f.
//  Obstacles skip the Unit MoveAI LiveOffset/SuccessDistance block (type
//  mask 0x200221/0x221 at 1802749af/1802749f9); Mode/destThing/args unused.
bool ScriptRuntime::obstacleMoveImpl(const hades::SourceValue&args,const std::vector<int32_t>&all,const std::vector<int32_t>&obstacles,const std::vector<int32_t>&destinations,std::string&error){try{
 const float angle=motionNumber(args,"Angle",-1e6f),speed=motionNumber(args,"Speed");
 if(angle!=-1e6f&&speed!=0)throw std::runtime_error("Native obstacle Move Angle+Speed TranslateComponent branch (0x180274c50) not reconstructed");
 for(const char*k:{"Names","DestinationNames"})if(args.get(k))throw std::runtime_error(std::string("Native obstacle Move name selection requires actual name-query provider: ")+k);
 if(motionFlag(args,"DestinationIsScreenRelative"))throw std::runtime_error("Native world obstacle Move DestinationIsScreenRelative camera branch not reconstructed");
 if(motionNumber(args,"MinDuration")!=0&&motionNumber(args,"MaxDuration")!=0)throw std::runtime_error("ScriptArgs::getDuration MinDuration/MaxDuration use thread-local sgg::Random (not reconstructed)");
 const bool shift=motionFlag(args,"ShiftThingsByOffset");float offX=0,offY=0,destX=0,destY=0;
 if(shift){offY=motionNumber(args,"OffsetY");offX=motionNumber(args,"OffsetX");}
 else if(!destinations.empty()){for(auto id:destinations){float x=0,y=0;if(!actorWorld_.location(id,x,y,error))return false;destX+=x;destY+=y;}const float n=float(destinations.size());destX/=n;destY/=n;}
 else if(const float distance=motionNumber(args,"Distance");distance!=0&&angle!=-1e6f){const float rad=float(double(angle)*0.017453292519943295);offY=-(distance*std::sin(rad));offX=std::cos(rad)*distance;}
 destY=motionNumber(args,"OffsetY")+destY;destX+=motionNumber(args,"OffsetX");
 float cx=0,cy=0;if(!shift&&!all.empty()){for(auto id:all){float x=0,y=0;if(!actorWorld_.location(id,x,y,error))return false;cx+=x;cy+=y;}const float n=float(all.size());cx/=n;cy/=n;}
 const float tmf=motionNumber(args,"TimeModifierFraction",1),easeOut=motionNumber(args,"EaseOut"),easeIn=motionNumber(args,"EaseIn"),duration=motionNumber(args,"Duration");const bool smooth=motionFlag(args,"SmoothStep");
 for(auto id:obstacles){float x=0,y=0;if(!actorWorld_.location(id,x,y,error))return false;float tx,ty;if(offX==0&&offY==0){tx=x-cx+destX;ty=y-cy+destY;}else{tx=offX+x;ty=offY+y;}
  if(!obstacleMoveTo(id,tx,ty,duration,speed,smooth,easeIn,easeOut,tmf,error))return false;}
 error.clear();return true;
 }catch(const std::exception&e){error=e.what();return false;}}
// Obstacle::MoveTo 0x2c5bf0 (.local/reports/hud-motion-Obstacle-MoveTo.json):
// Speed>0 -> duration=sqrtf(|mLocation-target|^2)/Speed (1802c5c25..c58). Both
// helpers active -> SetNewTarget(X/Y) else Reset(mLocation->target, duration).
// EaseIn==0&&EaseOut==0 ? (SmoothStep -> smooth flags) : copy EaseIn/EaseOut
// (1802c5cfd..d55); +0x5a8/+0x5f8 = TimeModifierFraction; then the obstacle
// joins ObstacleManager's pending update set (1802c5db4..dda).
bool ScriptRuntime::obstacleMoveTo(int32_t id,float tx,float ty,float duration,float speed,bool smooth,float easeIn,float easeOut,float tmf,std::string&error){try{
 auto*a=actorWorld_.actor(id);if(!a||!a->registered||!a->object||!a->definition||a->definition->kind!="Obstacle")throw std::runtime_error("Obstacle::MoveTo requires registered actual Obstacle");
 if(a->attachedParent)throw std::runtime_error("Obstacle::MoveTo on an attached obstacle reads raw mLocation; attached raw-location model not reconstructed");
 for(float v:{tx,ty,duration,speed,easeIn,easeOut,tmf})if(!std::isfinite(v))throw std::runtime_error("Nonfinite native obstacle MoveTo argument");
 const float x0=a->object->x,y0=a->object->y;
 if(speed>0){const float dx=x0-tx,dy=y0-ty;duration=std::sqrt(dx*dx+dy*dy)/speed;}
 auto&m=actorMotion_.obstacles[a->entityHandle];m.ownerId=id;m.lifetime=a->entityHandle;
 if(m.x.active&&m.y.active){axisSetNewTarget(m.x,tx,duration);axisSetNewTarget(m.y,ty,duration);}else{axisReset(m.x,x0,tx,duration);axisReset(m.y,y0,ty,duration);}
 if(easeIn==0&&easeOut==0){if(smooth)m.x.smooth=m.y.smooth=true;}else{m.x.easeIn=m.y.easeIn=easeIn;m.x.easeOut=m.y.easeOut=easeOut;}
 m.x.tmf=m.y.tmf=tmf;error.clear();return true;
 }catch(const std::exception&e){error=e.what();return false;}}
// Obstacle::ShiftLocationWithChildren 0x2c3510: LIFO from own handle with a
// visited set (insert only gates child pushes; the popped node is shifted
// either way), HandleManager<Obstacle>::Get (non-obstacles skipped),
// Thing::ShiftLocation 0x2e0a10 (zero delta -> nothing; else virtual
// SetLocation(GetLocation()+delta)), then mChildren(+0x648) pushed last-to-first
// if not yet visited. Obstacle::SetLocation 0x2c26f0: mLocation, Polygon::Offset
// (here refreshGeometry from the new location), attachment-ID OnAttachedToMoved
// (attached Things resolve parent+offset on read; AttachedAnimations stage marks
// them dirty), LocationChanged(false). The +0x3b8/+0x98 integer-rectangle
// accumulator has no Android consumer.
bool ScriptRuntime::shiftObstacleWithChildren(int32_t root,float dx,float dy,std::string&error){try{
 std::vector<int32_t> stack{root},visited;
 while(!stack.empty()){const int32_t id=stack.back();stack.pop_back();if(std::find(visited.begin(),visited.end(),id)==visited.end())visited.push_back(id);
  auto*a=actorWorld_.actor(id);if(!a||!a->registered||!a->object||a->isDisposed||!a->definition||a->definition->kind!="Obstacle")continue;
  if(dx!=0||dy!=0){if(a->attachedParent)throw std::runtime_error("Thing::ShiftLocation of an attached obstacle (parent+offset SetLocation) not reconstructed");
   a->object->x+=dx;a->object->y+=dy;a->object->graphicsDirty=a->object->geometryDirty=true;
   if(!actorWorld_.refreshGeometry(id,error)||!actorWorld_.obstacleLocationChanged(id,false,error))return false;}
  for(auto i=a->children.rbegin();i!=a->children.rend();++i)if(std::find(visited.begin(),visited.end(),*i)==visited.end())stack.push_back(*i);
 }
 error.clear();return true;
 }catch(const std::exception&e){error=e.what();return false;}}

// Unit::SetGoalAngle VA1802f85f0 sound tail (.local/reports/combat-SetGoalAngle-static.json):
// ThingData RotationSoundThreshold(+0x138, read in degrees x pi/180 by reader
// VA180205cf0) > 0 && Unit::CanRotate2f6050 -> wrapped |body angle - goal|
// (shorter way round) > threshold plays RotationSound(+0x110) through
// PlayCue3D4a690(owner=Unit, empty parameters, play=true).
// CanRotate: primary DisableRotation, GetRotationSpeed>0 (RotationSpeed times
// pending RotationMultiplier, as Unit.MoveInput uses), rotate timer <= 0, and no
// pending/active effect with DisableRotate.
bool ScriptRuntime::unitGoalAngleSound(int32_t id,float goal,std::string& error){try{
 auto* a=actorWorld_.actor(id);if(!a||!a->registered||!a->object||!a->definition||a->definition->kind!="Unit")throw std::runtime_error("SetGoalAngle requires actual Unit");
 const auto* thresholdValue=actorWorld_.currentProperty(id,"Thing.RotationSoundThreshold");if(!thresholdValue)throw std::runtime_error("SetGoalAngle RotationSoundThreshold unavailable");
 const float threshold=float(double(thresholdValue->number())*0.017453292519943295);
 if(!(threshold>0)){error.clear();return true;}
 bool canRotate=true;
 if(a->animation.active())canRotate=!(a->animation.clip()->flags&hades::ActorDisableRotation);
 else if(a->primaryAnimationLifetime||a->primaryEffectAnimation)throw std::runtime_error("SetGoalAngle CanRotate non-HAA primary DisableRotation not reconstructed");
 float rotation=0;{const auto* r=actorWorld_.currentProperty(id,"RotationSpeed");if(!r)throw std::runtime_error("SetGoalAngle RotationSpeed unavailable");rotation=float(r->number());}
 if(a->status){const auto st=a->status->snapshot();
  for(const auto h:st.pending){const auto* d=selfEffects_.effectData(h,error);if(!d)return false;const auto dis=d->fields.find("DisableRotate"),mul=d->fields.find("RotationMultiplier");if(dis==d->fields.end()||mul==d->fields.end())throw std::runtime_error("SetGoalAngle pending rotation effect fields unavailable");if(std::get<bool>(dis->second))canRotate=false;rotation*=float(std::get<double>(mul->second));}
  for(const auto h:st.active){const auto* d=selfEffects_.effectData(h,error);if(!d)return false;const auto dis=d->fields.find("DisableRotate");if(dis==d->fields.end())throw std::runtime_error("SetGoalAngle active rotation effect fields unavailable");if(std::get<bool>(dis->second))canRotate=false;}
  if(a->status->timers.rotate>0)canRotate=false;}
 if(!(rotation>0))canRotate=false;
 if(!canRotate){error.clear();return true;}
 constexpr float tau=6.2831855f;
 float body=a->object->angle;body-=std::floor(body/tau)*tau;float g=goal-std::floor(goal/tau)*tau;
 const float lo=std::min(g,body),hi=std::max(g,body);float diff=hi-lo;diff=std::min(tau-diff,diff);
 if(!(diff>threshold)){error.clear();return true;}
 const auto* sound=actorWorld_.currentProperty(id,"Thing.RotationSound");if(!sound)throw std::runtime_error("SetGoalAngle RotationSound unavailable");
 std::string cue;if(std::holds_alternative<std::string>(sound->value))cue=sound->text();else if(const auto* n=sound->get("name"))cue=n->text();
 if(cue.empty()||cue=="null"){error.clear();return true;}
 float x=0,y=0,z=0;uint32_t channel=0;if(!actorWorld_.location(id,x,y,error)||!actorWorld_.zLocation(id,z,error))return false;
 return playAudioCue(cue,id,{x,y,z},channel,error);
 }catch(const std::exception& e){error=e.what();return false;}}
