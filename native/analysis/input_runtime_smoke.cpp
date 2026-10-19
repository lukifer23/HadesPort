// Focused Mac debugger for actual MapLoad/StartRoom input-block transitions.
#include "../app/input_runtime.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
void run(lua_State* state,const std::string& code,const char* name) {
    if(luaL_loadbuffer(state,code.data(),code.size(),name) || lua_pcall(state,0,0,0))throw std::runtime_error(lua_tostring(state,-1));
}
std::string originalLine(const std::string& path,unsigned line) {
    std::ifstream file(path);std::string result;for(unsigned n=1;n<=line;++n)if(!std::getline(file,result))throw std::runtime_error("Missing original source line");return result;
}
int main(int argc,char** argv) {
    if(argc!=2)return 64;lua_State* state=luaL_newstate();luaL_openlibs(state);
    try {
        registerInputRuntime(state);
        run(state,"assert(not IsInputAllowed{});assert(GetInputBlock==nil)","=no-fabricated-player");
        std::string error;
        if(!createRecoveredInputPlayer(state,1,error) || !createRecoveredInputPlayer(state,2,error))throw std::runtime_error(error);
        const std::string scripts=argv[1];
        const auto addMap=originalLine(scripts+"/DeathLoop.lua",79);
        const auto removeMap=originalLine(scripts+"/RoomManager.lua",225);
        const auto addStart=originalLine(scripts+"/RoomManager.lua",1906);
        const auto removeStart=originalLine(scripts+"/RoomManager.lua",2013);
        if(addMap.find("AddInputBlock")==std::string::npos || removeMap.find("RemoveInputBlock")==std::string::npos)throw std::runtime_error("Original call site changed");
        run(state,addMap,"@DeathLoop.lua:79");run(state,addMap,"@DeathLoop.lua:79");
        if(!hasRecoveredInputBlock(state,"MapLoad") || snapshotInputPlayers(state)[0].namedBlocks.size()!=1)throw std::runtime_error("Duplicate block changed set semantics");
        run(state,addStart,"@RoomManager.lua:1906");run(state,removeMap,"@RoomManager.lua:225");
        run(state,"assert(not IsInputAllowed{} and IsInputAllowed{PlayerIndex=2})","=independent-blocks-and-players");
        run(state,removeStart,"@RoomManager.lua:2013");run(state,"assert(IsInputAllowed{})","=all-blocks-removed");
        run(state,"AddInputBlock{Name='Ignored',Names={'First','Second'}};assert(not IsInputAllowed{});RemoveInputBlock{Name='First'};assert(IsInputAllowed{});AddInputBlock{Name='Case'};AddInputBlock{Name='case'};RemoveInputBlock{Name='Case'};assert(not IsInputAllowed{});RemoveInputBlock{Names='case'};assert(IsInputAllowed{});assert(not IsInputAllowed{PlayerIndex=0} and not IsInputAllowed{PlayerIndex=3});local ok=pcall(AddInputBlock,{Name='Unsafe',PlayerIndex=3});assert(not ok)","=native-selection-boundaries");
        if(!setRecoveredPlayerInputPermission(state,1,false,error))throw std::runtime_error(error);
        run(state,"assert(not IsInputAllowed{})","=player-permission-false");
        if(!setRecoveredPlayerInputPermission(state,1,true,error))throw std::runtime_error(error);
        run(state,removeMap,"@RoomManager.lua:225-remove-absent");
        if(!isRecoveredInputAllowed(state,1))throw std::runtime_error("Expected allowed local input state");
        std::cout<<"ORIGINAL_MAPLOAD_STARTROOM_TRANSITIONS_OK\n"<<dumpInputRuntime(state);
        lua_close(state);return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';lua_close(state);return 2;}
}
