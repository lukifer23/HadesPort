#include "../host/file_assets.hpp"
#include "../app/scene_primary.hpp"
#include <iostream>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
using namespace hades;
static void need(bool ok,const std::string& error){if(!ok)throw std::runtime_error(error);}
class Assets:public AssetStore {
 FileAssets base_,extra_;
public:Assets(const char* base,const char* extra):base_(base),extra_(extra){}
 std::unique_ptr<AssetFile> open(std::string_view name)override {if(name=="primary-child-frames.bin")return extra_.open(name);return base_.open(name);}
};
int main(int argc,char** argv){try{
 need(argc==3,"scene_primary_smoke STAGED_ASSETS PRIMARY_ASSETS");std::string error;Assets assets(argv[1],argv[2]);lua_State* L=luaL_newstate();need(luaL_loadfile(L,(std::string(argv[2])+"/primaries.lua").c_str())==0,"Source primary Lua load");need(lua_pcall(L,0,1,0)==0,"Source primary Lua evaluate");SourceValue catalog;need(readSourceValue(L,-1,catalog,error),error);lua_close(L);
 auto registry=std::make_shared<AnimationRegistry>();ScenePrimaryWorld world;need(world.load(assets,catalog,registry,error),error);size_t doubles=0,bounded=0,roots=0,pending=0,children=0;EffectAnimationRandom random=[&](double& v,std::string&){v=.5;++doubles;return true;};EffectBoundedRandom integers=[&](uint32_t b,uint32_t& value,std::string&){need(b>0,"Actual source Book random bound");value=0;++bounded;return true;};uint64_t door=0;
 for(const auto& row:catalog.get("Recipes")->array()){
  if(!row.get("Active")->boolean()||!row.get("DoCreate")->boolean())continue;const int32_t id=int32_t(row.get("OwnerId")->number());const auto* recipe=world.recipe(id);need(recipe,"Actual source recipe lookup");if(recipe->externalHaa||ScenePrimaryWorld::externalEffect(recipe->graphic))continue;
  EffectOwnerState owner;owner.lifetime=uint64_t(id)+1;owner.scale=1;uint64_t h=0;need(world.create(id,recipe->graphic,owner,random,integers,h,error),error);const auto* s=world.state(h);const auto* a=registry->find(h);need(s&&a&&a->initialized&&!a->managerMember&&a->ownership==AnimationOwnership::ThingPrimary,"Actual primary lifecycle with no ManagerAdd");need(!s->ownerHasNoCollision,"Selected source OwnerHasNoCollision false default");if(s->name=="DoorLocked")door=h;++roots;
 }
 for(const auto& [id,s]:world.states()){pending+=s.graphicsPending;children+=s.parent!=0;const auto* a=registry->find(id);if(!s.parent&&s.type==NativeAnimationType::Book&&s.onlyWhenVisible&&s.loop)need(a->skipUpdate,"Original initial OnlyWhenVisible/Loop SkipUpdate gate");if(s.parent)need(!a->skipUpdate,"Child is not passed to owner OnAnimationSet initial SkipUpdate gate");}
 need(roots==1023&&world.size()==1024&&children==1&&registry->allocationCount()==1024&&registry->managerCount(0)==0&&registry->allAllocationsInitialized()&&bounded==205&&doubles==102,"Actual selected first-room source primary counts/RNG order");
 const auto previousChild=world.state(door)->child;const auto ownerId=world.state(door)->ownerId;EffectOwnerState owner;owner.lifetime=uint64_t(ownerId)+1;owner.scale=1;uint64_t next=0;need(world.replace(door,"DoorLocked_MetaReward",owner,random,integers,next,error),error);const auto* parent=world.state(next);const auto* child=world.state(parent->child);need(!registry->find(door)&&!registry->find(previousChild)&&child&&child->name=="RoomRewardUnavailable_MetaReward"&&child->hsv[0]==-.5f&&child->resource==PrimaryResourceKind::Auxiliary&&world.size()==1024&&registry->managerCount(0)==0,"Original reward variant parent/child allocation/resource/hue, release old graph");
 const auto image=world.auxiliary()->sample(child->resourceIndex,0,0,false);need(!image.rgba.empty(),"Original reward child pixels CRC");need(world.advance(.016f,error),error);
 if(catalog.get("DynamicGraphics")){
  need(pending==49,"Only explicitly unresolved original3DGhostIdle remains pending");EffectOwnerState dynamicOwner;dynamicOwner.lifetime=700001;dynamicOwner.scale=1;uint64_t vignette=0;
  need(world.create(700000,"VignetteOverlay",dynamicOwner,random,integers,vignette,error),error);const auto* v=world.state(vignette);need(v&&v->type==NativeAnimationType::Constant&&v->resource==PrimaryResourceKind::Auxiliary&&v->loop&&v->duration==1&&v->durationRemaining==1,"Actual dynamic source vignette definition and lifetime");const auto original=world.auxiliary()->sample(v->resourceIndex,0,0,false);need(original.width==1920&&original.height==1080&&original.rgba.size()==8294400,"Full original vignette source pixels/CRC");need(world.destroy(vignette,error),error);
  uint64_t invalid=0;need(!world.create(700001,"UnverifiedDynamicGraphic",dynamicOwner,random,integers,invalid,error),"Unknown dynamic source cannot invent a map recipe");
 }
 std::cout<<"{\"result\":\"passed\",\"actual_primary_roots\":"<<roots<<",\"actual_primary_and_child_lifetimes\":"<<world.size()<<",\"source_book_start_pcg_draws\":"<<bounded<<",\"source_random_play_speed_draws\":"<<doubles<<",\"explicit_pending_graphics\":"<<pending<<",\"actual_manager_additions\":0,\"door_meta_original_child\":true,\"full_room_or_android_execution_observed\":false}\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
