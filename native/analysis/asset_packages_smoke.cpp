#include "../app/asset_packages.hpp"
#include "../host/file_assets.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace {
void need(bool ok,const std::string& error){if(!ok)throw std::runtime_error(error);}
struct TemporaryAssets {
    std::filesystem::path path;
    TemporaryAssets(){
        std::random_device random;
        for(unsigned attempt=0;attempt<32;++attempt){
            const auto candidate=std::filesystem::temp_directory_path()/("hades-package-fault-"+std::to_string(random())+"-"+std::to_string(random()));
            if(std::filesystem::create_directory(candidate)){path=candidate;return;}
        }
        throw std::runtime_error("Cannot create isolated package failure assets");
    }
    ~TemporaryAssets(){std::error_code error;std::filesystem::remove_all(path,error);}
};
void failedRead(const hades::SourceValue& catalog,const std::string& name,const std::filesystem::path& root){
    hades::FileAssets files(root);hades::AssetPackages packages(&files);std::string error;
    need(packages.loadCatalog(catalog,error),error);
    uint64_t first=0,duplicate=0;
    need(packages.request(name,true,first,error)&&packages.request(name,true,duplicate,error),error);
    need(first!=duplicate&&packages.drainCompletions().empty(),"Failed-read requests lost distinct deferred identities");
    need(packages.pump(0,error)&&packages.drainCompletions().empty(),"Failed-read request bypassed service budget");
    need(!packages.pump(1,error)&&error.find("Missing/truncated original atlas")!=std::string::npos,"Missing/truncated actual atlas was admitted");
    const auto fault=error;
    need(!packages.package(name)->cpuReady&&packages.package(name)->nextTexture==0&&packages.residency().residentBytes==0,"Failed read published readiness or resident bytes");
    need(packages.drainCompletions().empty(),"Failed package request emitted completion");
    need(!packages.pump(1,error)&&error==fault,"Package read fault was not retained");
    uint64_t rejected=123;
    need(!packages.request(name,true,rejected,error)&&rejected==0&&error==fault&&packages.drainCompletions().empty(),"Faulted worker admitted a later request");
}
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
    need(!names.empty(),"Original package catalog is empty");
    // Exercise the real filesystem provider with missing data and a truncated
    // copy of one actual atlas. The canonical stage is never modified.
    std::string leaf,atlas;
    for(const auto& [name,value]:definitions->object())if(value.get("includes")->array().empty()&&!value.get("textures")->array().empty()){
        leaf=name;atlas=value.get("textures")->array().front().get("path")->text();break;
    }
    need(!leaf.empty(),"Original catalog has no leaf atlas for failure verification");
    {
        TemporaryAssets damaged;failedRead(catalog,leaf,damaged.path);
        auto original=assets.open(atlas);need(original&&original->size()>1,"Actual atlas unavailable for truncation check");
        std::vector<uint8_t> prefix(std::min(size_t(31),original->size()/2));need(original->read(prefix.data(),prefix.size())==prefix.size(),"Cannot read actual atlas prefix");
        const auto target=damaged.path/atlas;std::filesystem::create_directories(target.parent_path());
        std::ofstream out(target,std::ios::binary);out.write(reinterpret_cast<const char*>(prefix.data()),prefix.size());out.close();need(bool(out),"Cannot write isolated truncated atlas");
        failedRead(catalog,leaf,damaged.path);
    }
    hades::AssetPackages packages(&assets);
    // Damage a copy of the real catalog, then prove rejection publishes no
    // partial state: the unchanged actual catalog must still load afterwards.
    auto cyclic=catalog;
    auto& cyclicPackages=std::get<hades::SourceValue::Object>(std::get<hades::SourceValue::Object>(cyclic.value).at("packages").value);
    auto& includeValue=std::get<hades::SourceValue::Object>(cyclicPackages.at(names.front()).value).at("includes");
    auto includes=includeValue.array();includes.push_back(hades::SourceValue{names.front()});includeValue.value=std::move(includes);
    need(!packages.loadCatalog(std::move(cyclic),error)&&error.find("include cycle")!=std::string::npos,"Cyclic original package dependency was admitted");
    need(packages.loadCatalog(std::move(catalog),error),error);
    std::vector<hades::AssetPackages::Completion> expected,completed;
    uint64_t lastRequestId=0;
    auto request=[&](const std::string& name){uint64_t id=0;need(packages.request(name,true,id,error),error);need(id>lastRequestId,"Package request identity was reused");lastRequestId=id;expected.push_back({id,name});};
    request(names.front());
    for(const auto& name:names){need(packages.request(name,error),error);request(name);}
    request(names.front()); // A pending duplicate retains a distinct callback.
    need(packages.drainCompletions().empty(),"Request insertion emitted a completion");
    need(packages.pump(0,error)&&packages.drainCompletions().empty(),"Zero-budget pump serviced a request");
    while(packages.pending()){
        need(packages.pump(1,error),error);
        for(auto& done:packages.drainCompletions()){
            const auto* package=packages.package(done.name);
            need(package&&package->cpuReady,"Package completion preceded actual CPU reads");
            for(const auto& include:package->includes)need(packages.package(include)->cpuReady,"Package completion preceded included reads");
            completed.push_back(std::move(done));
        }
    }
    need(completed.size()==expected.size(),"Duplicate package request completion was lost");
    for(size_t i=0;i<expected.size();++i)need(completed[i].requestId==expected[i].requestId&&completed[i].name==expected[i].name,"Package callback request order changed");
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
    expected.clear();request(names.front());need(packages.request(names.front(),error),error);request(names.front());
    need(packages.pending()&&packages.drainCompletions().empty(),"Resident duplicate callback was synchronous");
    need(packages.pump(0,error)&&packages.drainCompletions().empty(),"Resident duplicate bypassed worker service");
    need(packages.pump(1,error),error);auto ready=packages.drainCompletions();
    need(!packages.pending()&&ready.size()==expected.size(),"Resident duplicate request completion missing");
    for(size_t i=0;i<ready.size();++i)need(ready[i].requestId==expected[i].requestId&&ready[i].name==expected[i].name,"Resident duplicate completion order changed");
    need(packages.residency().residentBytes==resident,"Resident duplicate allocated atlas storage");
    std::string missing="__AssetPackageSmokeMissing";while(packages.package(missing))missing+='_';
    uint64_t rejected=123;need(!packages.request(missing,true,rejected,error)&&rejected==0&&!packages.pending()&&packages.drainCompletions().empty(),"Missing package request mutated the queue");
    std::cout<<"{\"result\":\"passed\",\"references\":"<<references<<",\"unique_textures\":"<<unique.size()<<",\"resident_bytes\":"<<resident<<",\"logical_bytes\":"<<logical<<",\"saved_bytes\":"<<logical-resident<<",\"callback_requests\":"<<completed.size()+ready.size()<<",\"failed_read_cases\":2,\"original_execution_observed\":false}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
