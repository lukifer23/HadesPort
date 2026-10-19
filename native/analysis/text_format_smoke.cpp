// Focused host debugger: run the actual initial UIScripts OnAnyLoad callback.
#include "../app/text_runtime.hpp"
#include "../app/object_runtime.hpp"
#include "../app/scene_runtime.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
void run(lua_State* state,const std::string& source,const char* name) {
    if(luaL_loadbuffer(state,source.data(),source.size(),name) || lua_pcall(state,0,0,0)) throw std::runtime_error(lua_tostring(state,-1));
}
std::string read(const std::string& path) {std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error(path);return {std::istreambuf_iterator<char>(file),{}};}
int main(int argc,char** argv) {
    if(argc!=3)return 64;
    lua_State* state=luaL_newstate();luaL_openlibs(state);
    try {
        registerSceneRuntime(state);registerObjectRuntime(state);registerTextRuntime(state);
        const auto bytes=read(argv[2]);std::string error;
        if(!loadSceneGroupsSGP1(state,std::vector<uint8_t>(bytes.begin(),bytes.end()),error))throw std::runtime_error(error);
        const std::string root=argv[1];
        run(state,read(root+"/Color.lua"),"@Color.lua");
        run(state,read(root+"/UIData.lua"),"@UIData.lua");
        run(state,"local keys={};for _,format in pairs(TextFormats) do for key in pairs(format) do keys[key]=true end end;local ordered={};for key in pairs(keys) do ordered[#ordered+1]=key end;table.sort(ordered);print('ORIGINAL_FORMAT_FIELDS ',table.concat(ordered,','))","=smoke-inventory");
        run(state,"OnAnyLoad=function(t) t[1]({name='RoomOpening'}) end","=smoke-dispatch");
        const auto source=read(root+"/UIScripts.lua");
        const size_t first=source.find("OnAnyLoad");const size_t last=source.find("function DeferredUIScripts",first);
        if(first==std::string::npos || last==std::string::npos)throw std::runtime_error("Original callback boundary missing");
        // GameState stays unset only in this isolated formatting debugger. The
        // production host runs original GameState and later shrine updates.
        run(state,source.substr(first,last-first),"@UIScripts.lua:42");
        const auto formats=snapshotTextFormats(state);
        if(formats.empty())throw std::runtime_error("No original text formats created");
        for(const auto& format:formats) {
            if(!format.ownerActive || format.name.empty())throw std::runtime_error("Broken owner/name state");
            if(format.name=="BaseFormat" && format.fontSize!=19)throw std::runtime_error("Native FontSize conversion mismatch");
        }
        // Seed real map metadata for the focused GetIds query debugger. Runtime
        // group members also participate; no map activation is asserted here.
        ObjectSnapshot firstObject;firstObject.id=51;firstObject.name="EnemyPoint";firstObject.group="SpawnPoints";firstObject.groups={"SpawnPoints","Scripting"};
        ObjectSnapshot second=firstObject;second.id=9;second.groups={"SpawnPoints"};
        ObjectSnapshot inactive=firstObject;inactive.id=12;inactive.active=false;
        std::string seedError;
        if(!seedObjectRuntime(state,{firstObject,second,inactive},seedError))throw std::runtime_error(seedError);
        run(state,"local a=GetIds{Name='SpawnPoints'};assert(a==IdsTable and #a==2 and a[1]==51 and a[2]==9);local b=GetIds{Names={'SpawnPoints','SpawnPoints'},Type='EnemyPoint'};assert(#b==4 and b[3]==51);local c=GetIds{Name='SpawnPoints',Type='Unit'};assert(#c==0);assert(#GetIds{Name='Scripting'}==1);assert(#GetIds{Name='UnknownGroup'}==0)","=getids-real-state");
        const size_t before=formats.size();
        run(state,"local ok,err=pcall(CreateTextBox,{Id=1000000,Name='Unsupported',AutoSetDataProperties=true,RawText='glyphs'});assert(not ok and err:find('font layout'))","=unsupported-visible-text");
        if(snapshotTextFormats(state).size()!=before)throw std::runtime_error("Failed call mutated text registry");
        std::cout<<dumpTextRuntime(state);
        lua_close(state);return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';lua_close(state);return 2;}
}
