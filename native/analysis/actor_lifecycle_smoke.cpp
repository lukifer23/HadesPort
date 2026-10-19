#include "../app/actor_runtime.hpp"
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace hades;
void require(bool value,const std::string& error){if(!value)throw std::runtime_error(error);}
void load(ActorWorld& world,const char* path){
    lua_State* lua=luaL_newstate();if(!lua)throw std::runtime_error("Lua allocation");
    if(luaL_loadfilex(lua,path,"t")||lua_pcall(lua,0,1,0)){std::string error=lua_tostring(lua,-1);lua_close(lua);throw std::runtime_error(error);}
    SourceValue catalog;std::string error;const bool read=readSourceValue(lua,-1,catalog,error);lua_close(lua);
    require(read,error);require(world.loadCatalog(std::move(catalog),error),error);
}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("actor_lifecycle_smoke ACTUAL_ACTORS_LUA");
    ActorWorld world;load(world,argv[1]);std::string error;
    ObjectSnapshot objects[4];
    for(int i=0;i<4;++i){require(world.initializeDynamicObstacle(100+i,"BlankObstacle",0,0,"",objects[i],{},error),error);require(world.registerActor(100+i,objects[i],error),error);}
    require(world.linkStructuralChild(100,101,error),error);
    require(world.linkStructuralChild(101,102,error),error);
    require(world.linkStructuralChild(101,103,error),error);
    require(world.queueObstacleDestroy(101,error),error);
    require(world.actor(101)->registered&&!world.actor(101)->isDisposed&&world.actor(101)->isMarkedDead,"Destroy disposed synchronously");
    std::vector<int32_t> order;size_t stages=0;
    const auto callback=[&](const ActorDisposalEvent& event,std::string& detail){
        ++stages;const auto* a=world.actor(event.id);
        if(event.stage==ActorDisposalStage::AttachedAnimationsPreDispose){order.push_back(event.id);require(a->registered&&!a->isDisposed,"Premature retirement");}
        if(event.stage==ActorDisposalStage::WorldOnDisposed)require(a->entityHandle==event.lifetime&&!a->isDisposed,"World disposal lifetime boundary");
        if(event.stage==ActorDisposalStage::ObstacleTail)require(a->entityHandle==0&&!a->isDisposed,"Obstacle override boundary");
        if(event.stage==ActorDisposalStage::FreeRemainingComponents)require(a->isDisposed&&event.hasBody,"Component storage boundary");
        if(event.stage==ActorDisposalStage::RetireEntity)require(!event.hasBody&&!a->registered,"ECS retirement boundary");
        detail.clear();return true;
    };
    require(world.advanceObstacleDestruction(callback,error),error);
    require(order==std::vector<int32_t>{101,102,103}&&stages==48,"Structural disposal FIFO/stages");
    require(world.actor(100)->registered&&world.actor(100)->children.empty(),"Surviving structural parent mutation");
    require(world.advanceObstacleDestruction(callback,error)&&stages==48,"Disposal repeated");
    require(world.queueObstacleDestroy(100,error),error);size_t failures=0;
    require(!world.advanceObstacleDestruction([&](const ActorDisposalEvent& event,std::string& detail){++failures;if(event.stage==ActorDisposalStage::ReleasePrimaryAnimation){detail="test actual provider rejection";return false;}return true;},error),"Provider failure swallowed");
    require(world.disposalFaulted()&&world.actor(100)->registered&&!world.actor(100)->isDisposed,"Failed disposal state lost");
    const auto prior=failures;require(!world.advanceObstacleDestruction(callback,error)&&failures==prior,"Sticky failure retried callbacks");
    std::cout<<"{\"result\":\"passed\",\"source_catalog\":true,\"deferred_destroy\":true,\"fifo_descendants\":3,\"stages\":48,\"sticky_failure\":true,\"original_execution_observed\":false}\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
