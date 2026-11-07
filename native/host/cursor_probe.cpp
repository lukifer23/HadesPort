// Host-only audit driven by real pointer input on an original live screen.
// No resource/provider substitutions, callback calls or progression writes.
#include "../app/lua_runtime.hpp"
#include "../app/cursor_interaction.hpp"
#include "../app/platform_log.hpp"
#include "../app/input_runtime.hpp"
#include "../app/object_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
extern "C" {
#include <lua.h>
}

#ifndef __ANDROID__
namespace {
void checkCursor(bool result,const std::string& error){if(!result)throw std::runtime_error(error);}
bool cursorFlag(const hades::SourceValue& d,const char* key){const auto* p=d.get(key);return p&&p->boolean();}
float cursorNumber(const hades::SourceValue& d,const char* key){const auto* p=d.get(key);return p?float(p->number()):0.f;}
std::string cursorText(const hades::SourceValue& d,const char* key){const auto* p=d.get(key);return p?p->text():std::string{};}
}

struct ScriptRuntime::ScreenInputProbe {
 struct Target {hades::InteractionKey key;uint64_t primary=0,highlight=0;uint32_t sound=0;float interval=0,fade=0;bool floating=false;};
 struct Movie {uint64_t handle=0;bool dieWithOwner=false,detachedSeen=false,finished=false;};
 struct RewardMarker {hades::InteractionKey key;uint64_t primary=0,firstWorldFrame=0,lastWorldFrame=UINT64_MAX;unsigned samples=0;std::set<int32_t> terminalFrames;bool covered=false;};
 std::vector<Target> targets;std::vector<Movie> movies;std::set<uint64_t> beforeClick;
 std::vector<RewardMarker> rewardMarkers;unsigned rewardMarkerSamples=0;bool rewardMarkerCovered=false;
 size_t at=0;unsigned phase=0,periodic=0,canceled=0,cleared=0,audioTerminal=0;
 double started=0,entered=0,left=0;uint64_t pressFrame=0;uint32_t spawnCount=0;
 std::array<float,2> point{},outside{};
 bool notifyConsumed=false,notifyResultSeen=false;
 std::vector<std::string> notifyNames;
};

bool ScriptRuntime::probeLiveScreenInput(std::vector<std::string>& controls,bool& complete,std::string& error){try{
 if(!screenInputProbe_)screenInputProbe_=std::make_shared<ScreenInputProbe>();auto& p=*screenInputProbe_;complete=p.phase==8;
 if(complete){error.clear();return true;}
 // Observe the original reward before the menu-only phase gate. No reward,
 // animation, input, callback or progression state is created by this check.
 for(const auto& object:snapshotObjects(state_)){
  if(object.name!="AthenaUpgrade"||object.queryProxy||object.backend!=ObjectBackend::Actor||!object.entityHandle)continue;
  const auto* actor=actorWorld_.actor(object.id);if(!actor||!actor->registered||actor->entityHandle!=object.entityHandle||!actor->object||actor->object->entityHandle!=object.entityHandle)continue;
  const auto* root=actionEffects_.state(actor->primaryAnimationLifetime);if(!root||root->name!="BoonDropAthena")continue;
  const hades::InteractionKey key{object.id,object.entityHandle};
  auto found=std::find_if(p.rewardMarkers.begin(),p.rewardMarkers.end(),[&](const auto& r){return r.key==key&&r.primary==root->handle;});
  if(found==p.rewardMarkers.end()){checkCursor(p.rewardMarkers.size()<16,"Actual reward marker observation bound");p.rewardMarkers.push_back({key,root->handle,worldFrame_});found=std::prev(p.rewardMarkers.end());}
  auto& observed=*found;if(observed.lastWorldFrame==worldFrame_)continue;
  std::set<uint64_t> chain;uint64_t handle=root->handle;const hades::ActionEffectState* terminal=nullptr;
  while(handle){
   checkCursor(chain.insert(handle).second&&chain.size()<=16,"Actual reward marker child cycle/depth");
   const auto* node=actionEffects_.state(handle);const auto* life=effects_.registry()->find(handle);
   checkCursor(node&&life&&node->ownerId==key.id&&node->ownerLifetime==key.lifetime&&!node->detached&&node->type==hades::NativeAnimationType::Book&&life->child==node->child,"Actual reward marker Book/owner/registry lifetime mismatch");
   const auto dataFile=cursorText(node->definition,"FrameDataFile");checkCursor(dataFile.empty()||dataFile=="null","Actual reward marker diagnostic reached a source frame dataset");
   const auto sourceChild=cursorText(node->definition,"ChildAnimation");
   if(node->child){const auto* child=actionEffects_.state(node->child);checkCursor(child&&child->name==sourceChild,"Actual reward marker child differs from original source dependency");}
   else checkCursor(sourceChild.empty()||sourceChild=="null","Actual reward marker source child was not instantiated");
   terminal=node;handle=node->child;
  }
  checkCursor(chain.size()==5&&terminal&&terminal->name=="BoonDropAthenaIcon","Original reward marker five-Book source graph incomplete");
  const float rawX=terminal->attachOffset?(*terminal->attachOffset)[0]:terminal->randomX,rawY=terminal->attachOffset?(*terminal->attachOffset)[1]:terminal->randomY;
  checkCursor(rawX==0&&rawY==0,"Original reward marker terminal raw attachment differs from source-zero branch");
  std::array<float,3> actual{};checkCursor(effectMarker(key.id,"weapon_2_TRACKING",actual,error),error);
  float x=0,y=0,z=0;checkCursor(actorWorld_.location(key.id,x,y,error)&&actorWorld_.zLocation(key.id,z,error),error);
  checkCursor(actual[0]==x&&actual[1]==y&&actual[2]==z,"Actual reward no-dataset marker fallback differs from real attached owner location");
  checkCursor(std::isfinite(terminal->frame)&&terminal->frame>=cursorNumber(terminal->definition,"StartFrame")&&terminal->frame<cursorNumber(terminal->definition,"EndFrame")+1,"Actual reward marker terminal frame outside original source window");
  observed.lastWorldFrame=worldFrame_;++observed.samples;++p.rewardMarkerSamples;observed.terminalFrames.insert(int32_t(terminal->frame));
  if(!observed.covered&&observed.samples>=4&&worldFrame_>=observed.firstWorldFrame+3&&observed.terminalFrames.size()>=2){
   observed.covered=true;p.rewardMarkerCovered=true;
   hades::log(hades::Severity::Info,"HadesCursorProbe","REWARD_MARKER_AUDIT_OK owner=%d lifetime=%llu primary=%llu samples=%u source_child_nodes=%zu terminal_frames=%zu first_world_frame=%llu last_world_frame=%llu marker=weapon_2_TRACKING dataset_present=0 owner_location_match=1 callbacks_injected=0",key.id,(unsigned long long)key.lifetime,(unsigned long long)root->handle,observed.samples,chain.size(),observed.terminalFrames.size(),(unsigned long long)observed.firstWorldFrame,(unsigned long long)observed.lastWorldFrame);
  }
 }
 if(!p.phase&&!screenInputRequested()){error.clear();return true;}
 controls.clear(); // Real idle controls while the pointer visits the live UI.
 const auto snapshot=[&](hades::InteractionKey key){hades::InteractionSnapshot out;checkCursor(interactions_.snapshot(key,out,error),error);return out;};
 const auto sourcePoint=[&](hades::InteractionKey key){
  std::vector<std::array<float,2>> points;std::array<int32_t,4> area;checkCursor(screenAnimations_.interactionGeometry(key.id,points,area,error),error);checkCursor(points.size()>=3,"Live cursor source polygon has no interior");
  std::array<float,2> point{};for(const auto& v:points){point[0]+=v[0]/float(points.size());point[1]+=v[1]/float(points.size());}
  if(hades::cursorPointInPolygon(points,point[0],point[1]))return point;
  for(size_t i=1;i+1<points.size();++i){point={(points[0][0]+points[i][0]+points[i+1][0])/3,(points[0][1]+points[i][1]+points[i+1][1])/3};if(hades::cursorPointInPolygon(points,point[0],point[1]))return point;}
  throw std::runtime_error("Actual live cursor polygon has no derived test interior");
 };
 const auto move=[&](hades::InteractionKey key){p.point=sourcePoint(key);checkCursor(setLocalCursor(p.point[0],p.point[1],false,error),error);};
 const auto pendingContains=[&](int32_t id){for(const auto& n:worldNotifies_)if(n.interact&&std::find(n.sources.begin(),n.sources.end(),id)!=n.sources.end())return true;return false;};
 const auto detectMovies=[&]{
  if(p.phase<6)return;
  const auto key=p.targets.front().key;for(const auto&[handle,movie]:actionEffects_.states()){
   if(!movie.movie||movie.ownerId!=key.id||movie.ownerLifetime!=key.lifetime||p.beforeClick.contains(handle))continue;
   checkCursor(movie.useScreenLocation,"Actual callback movie lost original screen-coordinate flag");
   if(std::none_of(p.movies.begin(),p.movies.end(),[&](const auto& m){return m.handle==handle;})){checkCursor(p.movies.size()<16,"Live callback movie observation bound");p.movies.push_back({handle,cursorFlag(movie.definition,"DieWithOwner"),false,false});}
  }
  const auto* owner=screenAnimations_.thing(key.id);const bool disposed=!owner||!owner->body||owner->body->entityHandle!=key.lifetime;
  for(auto& movie:p.movies){const auto* state=actionEffects_.state(movie.handle);if(!state){movie.finished=true;continue;}checkCursor(state->useScreenLocation,"Retained callback movie changed screen-coordinate flag");if(disposed&&!movie.dieWithOwner){checkCursor(state->detached,"Retained screen callback movie still points to disposed owner");movie.detachedSeen=true;}}
 };
 if(!p.phase){
  hades::InteractionUserFrame user;bool player=false;for(const auto& input:snapshotInputPlayers(state_))if(input.playerIndex==1&&input.activeUnitLifetime){checkCursor(interactionUserFrame({input.activeUnitId,input.activeUnitLifetime},user,error),error);player=true;break;}checkCursor(player,"Live cursor audit requires the actual original player");
  std::set<hades::InteractionKey> selected;const auto providers=interactionProviders();
  for(const auto& n:worldNotifies_)if(n.interact)for(const auto id:n.sources){
   const auto* owner=screenAnimations_.thing(id);if(!owner||owner->dead||!owner->body||!owner->body->entityHandle)continue;
   const hades::InteractionKey key{id,owner->body->entityHandle};hades::InteractionSnapshot state;std::string missing;if(!interactions_.snapshot(key,state,missing)||!state.enabled||!cursorFlag(state.definition,"CursorOnly"))continue;
   hades::InteractionOwnerFrame frame;checkCursor(interactionOwnerFrame(key,frame,error),error);bool eligible=false;checkCursor(interactions_.isUseableBy(key,frame,user,{worldTime_,int32_t(worldFrame_)},true,providers,eligible,error),error);if(!eligible||!frame.firstGroupVisible||!selected.insert(key).second)continue;
   checkCursor(!state.hasBeenUsed,"Original live screen was already used before pointer audit");checkCursor(p.targets.size()<32,"Live cursor target bound");p.targets.push_back({key,owner->primary,0,0,0,0,cursorFlag(state.definition,"FloatHighlightAnimation")});
  }
  checkCursor(!p.targets.empty(),"Original live interaction notification has no eligible screen targets");
  p.started=screenTime_;move(p.targets[0].key);p.phase=1;error.clear();return true;
 }
 checkCursor(std::isfinite(screenTime_)&&screenTime_-p.started<=60,"Live screen input audit exceeded bounded source-clock duration");detectMovies();
 auto& target=p.targets[p.at];
 if(p.phase==1||p.phase==5){
  checkCursor(screenCursor_.target==target.key,"Actual native cursor did not select the derived live button interior");const auto* owner=screenAnimations_.thing(target.key.id);checkCursor(owner&&owner->body&&owner->body->entityHandle==target.key.lifetime,"Live cursor visit lost actual owner lifetime");
  if(target.floating){
   checkCursor(owner->primary==target.primary,"Floating highlight replaced the actual button primary");const auto h=cursorHighlights_.find(target.key);checkCursor(h!=cursorHighlights_.end()&&h->second!=target.primary,"Actual floating highlight allocation missing/separate lifetime lost");const auto* highlight=actionEffects_.state(h->second);const auto* life=effects_.registry()->find(h->second);
   checkCursor(highlight&&life&&life->managerMember&&!life->canceled&&highlight->ownerId==target.key.id&&highlight->ownerLifetime==target.key.lifetime,"Actual highlight manager/owner lifetime mismatch");
   if(p.phase==5)checkCursor(h->second!=target.highlight,"Reentered button reused its canceled highlight lifetime");target.highlight=h->second;target.sound=highlight->sound;target.interval=std::max(cursorNumber(highlight->definition,"VisualFxIntervalMin"),cursorNumber(highlight->definition,"VisualFxIntervalMax"));target.fade=cursorNumber(highlight->definition,"StopSoundOnFinishFade");p.spawnCount=highlight->totalSpawned;
   checkCursor(std::isfinite(target.interval)&&target.interval<=10&&std::isfinite(target.fade)&&target.fade<=10,"Original highlight timer exceeds live audit bound");
  }
  if(p.phase==1){p.entered=screenTime_;p.phase=2;error.clear();return true;}
  checkCursor(!snapshot(target.key).hasBeenUsed,"Pointer revisit unexpectedly activated original button");
  for(const auto& n:worldNotifies_)if(n.interact&&std::find(n.sources.begin(),n.sources.end(),target.key.id)!=n.sources.end()){
   lua_rawgeti(state_,LUA_REGISTRYINDEX,n.arguments);lua_getfield(state_,-1,"Notify");if(lua_isstring(state_,-1))p.notifyNames.emplace_back(lua_tostring(state_,-1));lua_pop(state_,2);
  }
  checkCursor(!p.notifyNames.empty(),"Actual live button has no original NotifyOnInteract waiter");
  for(const auto&[handle,state]:actionEffects_.states()){(void)state;p.beforeClick.insert(handle);}p.pressFrame=worldFrame_+1;
  // A genuine down/up between fixed ticks must survive as one queued Select.
  checkCursor(setLocalCursor(p.point[0],p.point[1],true,error),error);checkCursor(setLocalCursor(p.point[0],p.point[1],false,error),error);p.phase=6;error.clear();return true;
 }
 if(p.phase==2){
  checkCursor(screenCursor_.target==target.key,"Idle pointer lost its actual button target");const auto state=snapshot(target.key);checkCursor(!state.hasBeenUsed,"Idle pointer fabricated button activation");
  const auto* highlight=target.floating?actionEffects_.state(target.highlight):nullptr;if(target.floating)checkCursor(highlight&&cursorHighlights_.contains(target.key)&&cursorHighlights_.at(target.key)==target.highlight,"Idle pointer lost retained floating highlight lifetime");const bool periodic=highlight&&!cursorText(highlight->definition,"VisualFx").empty()&&cursorText(highlight->definition,"VisualFx")!="null";
  if(screenTime_-p.entered<target.interval||(periodic&&highlight->totalSpawned<=p.spawnCount)){error.clear();return true;}if(periodic)++p.periodic;
  float right=-std::numeric_limits<float>::infinity(),bottom=right;
  for(const auto& live:interactions_.snapshots())if(cursorFlag(live.definition,"CursorOnly")){
   const auto* owner=screenAnimations_.thing(live.key.id);if(!owner||owner->dead||!owner->body||owner->body->entityHandle!=live.key.lifetime)continue;
   std::vector<std::array<float,2>> points;std::array<int32_t,4> area;checkCursor(screenAnimations_.interactionGeometry(live.key.id,points,area,error),error);for(const auto& v:points){right=std::max(right,v[0]);bottom=std::max(bottom,v[1]);}
   hades::InteractionOwnerFrame frame;checkCursor(interactionOwnerFrame(live.key,frame,error),error);for(const auto& r:frame.textAreas){right=std::max(right,float(int64_t(r.x)+r.width));bottom=std::max(bottom,float(int64_t(r.y)+r.height));}
  }
  p.outside={std::nextafter(right,std::numeric_limits<float>::infinity()),std::nextafter(bottom,std::numeric_limits<float>::infinity())};checkCursor(setLocalCursor(p.outside[0],p.outside[1],false,error),error);p.phase=3;error.clear();return true;
 }
 if(p.phase==3){
  checkCursor(screenCursor_.target.id<0,"Pointer outside all actual source geometry retained a cursor target");
  if(target.floating){const auto* life=effects_.registry()->find(target.highlight);checkCursor(!cursorHighlights_.contains(target.key)&&life&&life->canceled,"Moving away did not cancel the retained floating highlight");++p.canceled;}
  p.left=screenTime_;p.phase=4;error.clear();return true;
 }
 if(p.phase==4){
  if(target.floating)checkCursor(!actionEffects_.state(target.highlight)&&!effects_.registry()->find(target.highlight),"Actual manager phase retained canceled floating highlight");
  if(screenTime_-p.left<target.fade){error.clear();return true;}
  if(target.sound){hades::AudioChannelState cue;if(audio_.channelState(target.sound,cue))checkCursor(cue.finished||cue.stopped||cue.volume<=.0001f,"Actual highlight cue did not reach natural/faded terminal state");++p.audioTerminal;}
  ++p.cleared;hades::log(hades::Severity::Info,"HadesCursorProbe","CURSOR_AUDIT_VISIT owner=%d floating=%d periodic=%u manager_cleared=%u",target.key.id,int(target.floating),p.periodic,p.cleared);
  if(++p.at<p.targets.size()){move(p.targets[p.at].key);p.phase=1;}else{p.at=0;move(p.targets[0].key);p.phase=5;}error.clear();return true;
 }
 if(p.phase==6){
  const auto used=snapshot(target.key);checkCursor(used.hasBeenUsed&&used.lastUseFrame==int32_t(p.pressFrame),"Queued pointer Select did not execute actual Interact.Use on the original target");
  checkCursor(!screenCursor_.pointerDown&&std::binary_search(rawControlsPressed_.begin(),rawControlsPressed_.end(),std::string("Select")),"Quick pointer release lost its native Select edge");
  p.notifyConsumed=!pendingContains(target.key.id);checkCursor(p.notifyConsumed,"Actual Interact.Use did not satisfy the original NotifyOnInteract waiter");
  lua_getglobal(state_,"NotifyResultsTable");if(lua_istable(state_,-1))for(const auto& name:p.notifyNames){lua_getfield(state_,-1,name.c_str());p.notifyResultSeen=p.notifyResultSeen||(lua_isnumber(state_,-1)&&lua_tointeger(state_,-1)==target.key.id);lua_pop(state_,1);}lua_pop(state_,1);
  p.phase=7;error.clear();return true;
 }
 if(p.phase==7){
  bool disposed=true;for(const auto& original:p.targets){const auto* owner=screenAnimations_.thing(original.key.id);const bool gone=!owner||!owner->body||owner->body->entityHandle!=original.key.lifetime;disposed=disposed&&gone;if(gone)checkCursor(!interactions_.contains(original.key)&&!cursorHighlights_.contains(original.key),"Actual menu disposal retained stale Interact/highlight lifetime");}
  if(!disposed||screenInputRequested()){error.clear();return true;}
  bool finiteMovieLive=false;unsigned detached=0,retired=0;for(const auto& movie:p.movies){detached+=movie.detachedSeen;retired+=movie.finished;if(const auto* live=actionEffects_.state(movie.handle))finiteMovieLive=finiteMovieLive||(!cursorFlag(live->definition,"Loop")&&!cursorFlag(live->definition,"HoldLastFrame"));}
  if(finiteMovieLive){error.clear();return true;}
  checkCursor(p.rewardMarkerCovered&&p.rewardMarkerSamples>=4,"Completed screen audit never covered actual original reward marker across live Book frame advances");
  cancelLocalCursor();p.phase=8;complete=true;
  hades::log(hades::Severity::Info,"HadesCursorProbe","CURSOR_AUDIT_OK targets=%zu periodic_highlights=%u canceled_highlights=%u cleared_visits=%u terminal_audio=%u pointer_select_frame=%llu notify_consumed=%d notify_result_seen=%d owner_movies=%zu detached_movies=%u retired_movies=%u source_context_disposed=1 script_callbacks_injected=0 device_claim=0",p.targets.size(),p.periodic,p.canceled,p.cleared,p.audioTerminal,(unsigned long long)p.pressFrame,int(p.notifyConsumed),int(p.notifyResultSeen),p.movies.size(),detached,retired);
 }
 error.clear();return true;
 }catch(const std::exception& e){error=std::string("Live screen pointer audit: ")+e.what();return false;}}
#endif
