// Host-only registered-action diagnostic. Reads genuine exported package data
// and uses the original Main.lua dispatch; it does not advance the game world.
#include "../app/lua_runtime.hpp"
#include "../app/platform_log.hpp"
#include <algorithm>
#include <cstring>
#include <functional>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#ifndef __ANDROID__
namespace {
struct RestorePackageProbe {
    std::function<void()> restore;
    ~RestorePackageProbe(){restore();}
};
}

bool ScriptRuntime::probePackageRequests(){
    if(!state_||!mainLoaded_||!gameLoaded_||!unitPackageNamesKnown_){error_="Package probe requires the initialized original scripts and package mapping";return false;}
    if(packages_.pending()){error_="Package probe requires no outstanding asset work; run before room boot";return false;}
    lua_State* const L=state_;const int top=lua_gettop(L);
    int originalEvent=LUA_NOREF,originalArgs=LUA_NOREF,context=LUA_NOREF;
    bool isolated=false;
    RestorePackageProbe cleanup{[&]{
        if(isolated){
            lua_getfield(L,LUA_REGISTRYINDEX,"hades.triggerRegistrations");
            lua_getfield(L,-1,"OnPackageLoaded");
            if(lua_istable(L,-1))for(size_t i=1,n=lua_rawlen(L,-1);i<=n;++i){
                lua_rawgeti(L,-1,int(i));lua_getfield(L,-1,"callback");
                luaL_unref(L,LUA_REGISTRYINDEX,int(lua_tointeger(L,-1)));lua_pop(L,2);
            }
            lua_pop(L,1);if(originalEvent==LUA_REFNIL)lua_pushnil(L);else lua_rawgeti(L,LUA_REGISTRYINDEX,originalEvent);lua_setfield(L,-2,"OnPackageLoaded");lua_pop(L,1);
            if(originalArgs==LUA_REFNIL)lua_pushnil(L);else lua_rawgeti(L,LUA_REGISTRYINDEX,originalArgs);lua_setglobal(L,"global_triggerArgs");
        }
        for(const int ref:{originalEvent,originalArgs,context})if(ref!=LUA_NOREF&&ref!=LUA_REFNIL)luaL_unref(L,LUA_REGISTRYINDEX,ref);
        lua_settop(L,top);
    }};
    try{
        // Read the catalog as data, just as initialization does. No invented
        // catalog entries, aliases, textures or resource-provider substitutes.
        hades::SourceValue catalog;
        {
            auto asset=assets_->open("packages.lua");
            if(!asset||asset->size()>32*1024*1024)throw std::runtime_error("Missing/oversized actual package catalog");
            std::vector<char> bytes(asset->size());size_t at=0;
            while(at<bytes.size()){const auto got=asset->read(bytes.data()+at,bytes.size()-at);if(!got)throw std::runtime_error("Truncated actual package catalog");at+=got;}
            if(luaL_loadbufferx(L,bytes.data(),bytes.size(),"@package_probe_catalog","t")!=LUA_OK||lua_pcall(L,0,1,0)!=LUA_OK)throw std::runtime_error(lua_tostring(L,-1));
            std::string detail;if(!hades::readSourceValue(L,-1,catalog,detail))throw std::runtime_error(detail);lua_settop(L,top);
        }
        const auto* sourcePackages=catalog.get("packages");const auto* sourceMapping=catalog.get("unit_package_names");
        if(!sourcePackages||!sourceMapping)throw std::runtime_error("Actual catalog lacks package/mapping data");
        std::vector<std::pair<uint64_t,std::string>> candidates;
        size_t serviceBound=8;
        std::function<uint64_t(const std::string&)> bytesFor=[&](const std::string& name){
            const auto* p=packages_.package(name);if(!p)throw std::runtime_error("Catalog/runtime package mismatch: "+name);
            uint64_t bytes=0;for(const auto& texture:p->textures)bytes+=uint64_t(texture.width)*texture.height*4;
            for(const auto& include:p->includes)bytes+=bytesFor(include);return bytes;
        };
        for(const auto& [name,value]:sourcePackages->object()){
            (void)value;const auto* p=packages_.package(name);if(!p)throw std::runtime_error("Catalog package missing from worker");
            serviceBound+=p->textures.size()+1;
            // Use package identities which standalone name mapping preserves.
            const auto alias=unitPackageNames_.find(name);
            if(alias==unitPackageNames_.end()||alias->second.empty()||alias->second==name)candidates.emplace_back(bytesFor(name),name);
        }
        if(candidates.size()<2)throw std::runtime_error("Probe needs two actual exported package identities");
        std::sort(candidates.begin(),candidates.end());const std::string a=candidates[0].second,b=candidates[1].second;
        std::string missing="HadesPackageProbeMissing";
        while(packages_.package(missing)||unitPackageNames_.contains(missing))missing+='X';
        auto caseVariant=[](const std::string& name,const std::function<bool(const std::string&)>& admissible){
            for(size_t i=0;i<name.size();++i){
                std::string varied=name;char& c=varied[i];
                if(c>='A'&&c<='Z')c=char(c-'A'+'a');else if(c>='a'&&c<='z')c=char(c-'a'+'A');else continue;
                if(admissible(varied))return varied;
            }
            return std::string{};
        };
        const auto wrong=caseVariant(a,[&](const std::string& name){return name!=a&&name!=b;});
        if(wrong.empty())throw std::runtime_error("Actual package has no distinct case-varying source filter");
        std::string aliasUnit,aliasPackage,aliasVariant;
        for(const auto& [unit,package]:sourceMapping->object()){
            const auto mapped=unitPackageNames_.find(unit);
            if(mapped==unitPackageNames_.end()||mapped->second!=package.text())throw std::runtime_error("Original unit mapping differs from runtime lookup");
            if(package.text().empty()||packages_.package(package.text()))continue;
            const auto variant=caseVariant(unit,[&](const std::string& name){return !unitPackageNames_.contains(name)&&!packages_.package(name);});
            if(variant.empty())continue;
            aliasUnit=unit;aliasPackage=package.text();aliasVariant=variant;break;
        }
        if(aliasUnit.empty())throw std::runtime_error("Probe needs an actual unexported unit mapping with an unmapped case variant");

        lua_getfield(L,LUA_REGISTRYINDEX,"hades.triggerRegistrations");
        lua_getfield(L,-1,"OnPackageLoaded");originalEvent=luaL_ref(L,LUA_REGISTRYINDEX);
        lua_newtable(L);lua_setfield(L,-2,"OnPackageLoaded");lua_pop(L,1);
        lua_getglobal(L,"global_triggerArgs");originalArgs=luaL_ref(L,LUA_REGISTRYINDEX);isolated=true;
        auto run=[&](const char* code,bool result=false){
            const int saved=lua_gettop(L);
            if(luaL_loadbufferx(L,code,std::strlen(code),"=registered_package_probe","t")!=LUA_OK)throw std::runtime_error(lua_tostring(L,-1));
            if(context==LUA_NOREF)lua_pushnil(L);else lua_rawgeti(L,LUA_REGISTRYINDEX,context);lua_pushlstring(L,a.data(),a.size());lua_pushlstring(L,b.data(),b.size());
            lua_pushlstring(L,missing.data(),missing.size());lua_pushlstring(L,wrong.data(),wrong.size());lua_pushlstring(L,aliasPackage.data(),aliasPackage.size());
            lua_pushlstring(L,aliasUnit.data(),aliasUnit.size());lua_pushlstring(L,aliasVariant.data(),aliasVariant.size());
            if(lua_pcall(L,8,result?1:0,0)!=LUA_OK){const std::string detail=lua_tostring(L,-1);lua_settop(L,saved);throw std::runtime_error(detail);}
            if(result)context=luaL_ref(L,LUA_REGISTRYINDEX);lua_settop(L,saved);
        };
        run(R"LUA(
local _,A,B,missing,wrong = ...
local P={exact=0,multi=0,other=0,invalid=0,chain=true,bad=false}
local function check(args)
  local co,isMain=coroutine.running()
  if type(args)~='table' or next(args)~=nil or args~=global_triggerArgs or not co or isMain then P.bad=true end
end
OnPackageLoaded{A,function(args)
  check(args);P.exact=P.exact+1
  if P.chain then P.chain=false;LoadPackages{Name=B} end
end}
OnPackageLoaded{A..' '..B,function(args) check(args);P.multi=P.multi+1 end}
OnPackageLoaded{B,function(args) check(args);P.other=P.other+1 end}
OnPackageLoaded{function() P.invalid=P.invalid+1 end}
OnPackageLoaded{'',function() P.invalid=P.invalid+1 end}
OnPackageLoaded{wrong,function() P.invalid=P.invalid+1 end}
return P
)LUA",true);
        auto unchanged=[&](const char* code){const auto before=packages_.dump();run(code);if(packages_.dump()!=before)throw std::runtime_error("Rejected/no-op LoadPackages mutated worker state");};
        unchanged(R"LUA(
local P,A,B,missing,wrong,mapped,unit,variant=...
local ok,why=pcall(LoadPackages,{Names={A,missing}})
assert(not ok and tostring(why):find('Source asset package not exported: '..missing,1,true), 'missing later name preflight')
ok,why=pcall(LoadPackages,{Name=unit})
assert(not ok and tostring(why):find('Source asset package not exported: '..mapped,1,true), 'exact original unit mapping')
ok,why=pcall(LoadPackages,{Name=variant})
assert(not ok and tostring(why):find('Source asset package not exported: '..variant,1,true), 'case variant must retain input')
LoadPackages{Names={},Name=missing}
LoadPackages{Names=123,Name=missing}
LoadPackages{Names='123',Name=missing}
LoadPackages{Names=false,Name=missing}
LoadPackages{Names=function() end,Name=missing}
assert(P.exact==0 and P.multi==0 and P.other==0 and P.invalid==0)
)LUA");
        run("local P,A=...;LoadPackages{Names={A,A}};assert(P.exact==0 and P.multi==0 and P.other==0,'completion must be deferred')");
        if(!serviceAssets(0))throw std::runtime_error(error_);
        run("local P=...;assert(P.exact==0 and P.multi==0 and P.other==0,'zero-budget callback')");
        if(!serviceAssets(serviceBound))throw std::runtime_error(error_);
        run("local P=...;assert(P.exact==2 and P.multi==2 and P.other==0 and P.invalid==0 and not P.bad,'distinct duplicate completions and exact filters')");
        if(!packages_.pending())throw std::runtime_error("Callback-created package request was lost/serviced inline");
        for(size_t i=0;packages_.pending();++i){if(i>=serviceBound)throw std::runtime_error("Actual package worker failed to drain within catalog bound");if(!serviceAssets(1))throw std::runtime_error(error_);}
        run("local P=...;assert(P.exact==2 and P.multi==3 and P.other==1 and P.invalid==0 and not P.bad,'callback-created request later service')");
        int expected=2;
        for(const char* code:{
            "local P,A,B,missing=...;LoadPackages{Names={A},Name=missing}",
            "local P,A,B,missing=...;LoadPackages{Names=A,Name=missing}",
            "local P,A,B,missing=...;LoadPackages{Names={[1]=A,[3]=missing}}",
            "local P,A=...;LoadPackages{NameFoo=A,NameIgnored=function() end}"
        }){
            run(code);
            lua_rawgeti(L,LUA_REGISTRYINDEX,context);lua_getfield(L,-1,"exact");const auto before=lua_tointeger(L,-1);lua_pop(L,2);
            if(before!=expected)throw std::runtime_error("Resident request completed inline");
            if(!serviceAssets(1))throw std::runtime_error(error_);++expected;
            lua_rawgeti(L,LUA_REGISTRYINDEX,context);lua_getfield(L,-1,"exact");const auto after=lua_tointeger(L,-1);lua_pop(L,2);
            if(after!=expected||packages_.pending())throw std::runtime_error("Names precedence/scalar/first-nil request mismatch");
        }
        run("local P=...;assert(P.exact==6 and P.multi==7 and P.other==1 and P.invalid==0 and not P.bad,'final callback payload/filter invariants')");
        hades::log(hades::Severity::Info,"HadesPackageProbe","PACKAGE_PROBE_OK scope=host_registered_actions packages=%s,%s callbacks=14 worldAdvance=0 gpuClaim=0",a.c_str(),b.c_str());
        error_.clear();return true;
    }catch(const std::exception& e){error_=std::string("Package action probe: ")+e.what();return false;}
}
#endif
