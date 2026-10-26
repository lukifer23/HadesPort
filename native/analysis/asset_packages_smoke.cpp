#include "../app/asset_packages.hpp"
#include "../host/file_assets.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace {
void need(bool ok,const std::string& error){if(!ok)throw std::runtime_error(error);}
}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("asset_packages_smoke ACTUAL_STAGED_ASSETS");
    hades::FileAssets assets(argv[1]);auto file=assets.open("packages.lua");
    need(file&&file->size()<=32*1024*1024,"Missing/oversized original package catalog");
    std::string script(file->size(),'\0');size_t at=0;
    while(at<script.size()){const auto n=file->read(script.data()+at,script.size()-at);need(n>0,"Truncated original package catalog");at+=n;}
    std::unique_ptr<lua_State,decltype(&lua_close)> vm(luaL_newstate(),lua_close);need(bool(vm),"Lua allocation failed");
    auto* L=vm.get();
    if(luaL_loadbufferx(L,script.data(),script.size(),"@packages.lua","t")||lua_pcall(L,0,1,0))throw std::runtime_error(lua_tostring(L,-1)?lua_tostring(L,-1):"Original catalog evaluation failed");
    hades::SourceValue catalog;std::string error;
    need(hades::readSourceValue(L,-1,catalog,error),error);
    const auto* definitions=catalog.get("packages");need(definitions,"Original package definitions missing");
    std::vector<std::string> names;for(const auto& [name,value]:definitions->object()){(void)value;names.push_back(name);}
    hades::AssetPackages packages(&assets);need(packages.loadCatalog(std::move(catalog),error),error);
    for(const auto& name:names)need(packages.request(name,error),error);
    while(packages.pending())need(packages.pump(1,error),error);
    std::map<std::string,std::shared_ptr<const std::vector<uint8_t>>> unique;
    size_t logical=0,resident=0,references=0,duplicates=0;
    for(const auto& name:names){
        const auto* package=packages.package(name);need(package&&package->requested&&package->cpuReady,"Original package did not become ready");
        need(package->nextTexture==package->textures.size(),"Original package texture ordering incomplete");
        for(const auto& texture:package->textures){
            need(texture.rgba&&texture.rgba->size()==size_t(texture.width)*texture.height*4,"Original texture extent mismatch");
            logical+=texture.rgba->size();++references;
            const auto [it,inserted]=unique.emplace(texture.path,texture.rgba);
            if(!inserted){need(it->second==texture.rgba,"Duplicate original atlas has distinct resident storage");++duplicates;continue;}
            resident+=texture.rgba->size();auto source=assets.open(texture.path);
            need(source&&source->size()==texture.rgba->size(),"Original atlas source extent mismatch");
            std::array<uint8_t,16384> bytes{};size_t offset=0;
            while(offset<texture.rgba->size()){
                const auto n=source->read(bytes.data(),std::min(bytes.size(),texture.rgba->size()-offset));
                need(n>0&&std::equal(bytes.begin(),bytes.begin()+n,texture.rgba->begin()+offset),"Resident atlas differs from original exported bytes");offset+=n;
            }
        }
    }
    const auto memory=packages.residency();
    need(duplicates>0,"Actual package catalog has no duplicate references to exercise sharing");
    need(memory.residentBytes==resident&&memory.logicalResidentBytes==logical&&memory.uniqueTextures==unique.size(),"Aggregate package residency accounting mismatch");
    need(resident<logical,"Original atlas sharing saved no resident storage");
    std::cout<<"{\"result\":\"passed\",\"references\":"<<references<<",\"unique_textures\":"<<unique.size()<<",\"resident_bytes\":"<<resident<<",\"logical_bytes\":"<<logical<<",\"saved_bytes\":"<<logical-resident<<",\"original_execution_observed\":false}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
