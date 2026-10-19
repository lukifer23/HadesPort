// Focused lifetime debugger for the recovered Thing::mLuaTable/AttachLua branch.
// Uses real VM-owned obstacle entities, not fake action return values.
#include "../app/object_runtime.hpp"
#include "../app/scene_runtime.hpp"
#include <iostream>
#include <fstream>
#include <iterator>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
static void run(lua_State* state,const char* source) {
    if(luaL_loadstring(state,source)||lua_pcall(state,0,0,0))throw std::runtime_error(lua_tostring(state,-1));
}
int main(int argc,char** argv){
    if(argc!=2){std::cerr<<"Usage: object-lua-debug ORIGINAL_GROUPS.bin\n";return 64;}
    lua_State* state=luaL_newstate();luaL_openlibs(state);
    try{
        registerSceneRuntime(state);registerObjectRuntime(state);
        std::ifstream input(argv[1],std::ios::binary);if(!input)throw std::runtime_error("Missing original scene groups");
        const std::vector<uint8_t> groups{std::istreambuf_iterator<char>(input),{}};std::string error;
        if(!loadSceneGroupsSGP1(state,groups,error))throw std::runtime_error(error);
        markObjectMapMetadataLoaded(state);
        run(state,R"(
            OwnerId=SpawnObstacle{Name='BlankObstacle'}
            First={AlreadyFalse=false,AlreadyZero=0,Number=4,Nested={Value=7},[3]='three'}
            AttachLua{Id=OwnerId,Table=First}
        )");
        lua_getglobal(state,"OwnerId");const auto id=static_cast<int32_t>(lua_tointeger(state,-1));lua_pop(state,1);
        pushObjectLuaTable(state,id);lua_getglobal(state,"First");
        if(!lua_rawequal(state,-1,-2))throw std::runtime_error("AttachLua lost source table identity");lua_pop(state,2);
        run(state,R"(
            AttachLua{Id=OwnerId,OnlyNullKeys=true,Table={AlreadyFalse=true,AlreadyZero=9,Number=8,Missing='new',Nested={Value=99},[3]='replaced',[4]='four'}}
            assert(First.AlreadyFalse==false and First.AlreadyZero==0 and First.Number==4)
            assert(First.Missing=='new' and First.Nested.Value==7 and First[3]=='three' and First[4]=='four')
            Replacement={Shared={Value=12}}
            AttachLua{Id=OwnerId,Table=Replacement}
        )");
        pushObjectLuaTable(state,id);lua_getglobal(state,"Replacement");
        if(!lua_rawequal(state,-1,-2))throw std::runtime_error("AttachLua replacement copied instead of sharing");lua_pop(state,2);
        run(state,R"(
            AddToGroup{Id=OwnerId,Name='Ignored',Names={'DebugA','DebugB'}}
            AddToGroup{Id=OwnerId,Name='DebugC',DrawGroup=true}
            assert(#GetIds{Name='Ignored'}==0 and #GetIds{Name='DebugA'}==1)
            FlipHorizontal{Id=OwnerId};FlipHorizontal{Id=OwnerId}
        )");
        const auto objects=snapshotObjects(state);
        const std::vector<std::string> expected{"DebugC","DebugA","DebugB","Default"};
        if(objects.size()!=1||objects[0].groups!=expected||objects[0].group!="DebugC"||objects[0].flipHorizontal||!objects[0].geometryDirty)throw std::runtime_error("Group promotion/flip state mismatch");
        // Select a secondary group, exercising the actual action selector too.
        run(state,"Destroy{Name='DebugA'};assert(Replacement.Shared.Value==12)");
        pushObjectLuaTable(state,id);if(!lua_isnil(state,-1))throw std::runtime_error("Destroyed Thing retained its Lua handle");lua_pop(state,1);
        lua_close(state);std::cout<<"OBJECT_LUA_LIFETIME_DEBUG_OK identity=true shallowMissingKeys=true destroyedHandleReleased=true secondaryGroups=true flipState=true\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';lua_close(state);return 2;}
}
