#include "asset_packages.hpp"
#include <cmath>
#include <set>
#include <sstream>
#include <stdexcept>
namespace hades {
namespace {
const SourceValue& prop(const SourceValue& v,const std::string& key){const auto* p=v.get(key);if(!p)throw std::runtime_error("Missing package property: "+key);return *p;}
uint32_t dimension(const SourceValue& v){const auto n=v.number();if(n<1||n>16384||n!=std::trunc(n))throw std::runtime_error("Invalid atlas dimension");return uint32_t(n);}
bool hash(const std::string& s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
}
bool AssetPackages::loadCatalog(SourceValue root,std::string& error){
    try{
        if(!packages_.empty())throw std::runtime_error("Package catalog already loaded");
        if(prop(root,"schema").text()!="hades-native-asset-packages-v1")throw std::runtime_error("Unrecognized package catalog");
        std::map<std::string,PackageState> parsed;
        for(auto& [name,value]:std::get<SourceValue::Object>(std::get<SourceValue::Object>(root.value).at("packages").value)){
            if(name.empty()||name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::runtime_error("Invalid package key");
            PackageState p;p.name=name;p.manifestHash=prop(value,"manifest_sha256").text();p.packageHash=prop(value,"package_sha256").text();
            if(!hash(p.manifestHash)||!hash(p.packageHash))throw std::runtime_error("Invalid original package identity");
            for(const auto& inc:prop(value,"includes").array())p.includes.push_back(inc.text());
            auto& textures=std::get<SourceValue::Array>(std::get<SourceValue::Object>(value.value).at("textures").value);
            std::set<std::string> names;
            for(auto& src:textures){
                PackageTexture t;t.name=prop(src,"name").text();t.path=prop(src,"path").text();t.sha256=prop(src,"sha256").text();t.width=dimension(prop(src,"width"));t.height=dimension(prop(src,"height"));
                if(!hash(t.sha256)||t.path!="package-textures/"+t.sha256+".rgba"||!names.insert(t.name).second||uint64_t(t.width)*t.height>64*1024*1024)throw std::runtime_error("Invalid package texture data");
                t.sprites=std::move(std::get<SourceValue::Object>(src.value).at("sprites"));p.textures.push_back(std::move(t));
            }
            parsed.emplace(name,std::move(p));
        }
        for(const auto& [name,p]:parsed)for(const auto& inc:p.includes)if(!parsed.contains(inc))throw std::runtime_error("Package include missing: "+inc);
        packages_=std::move(parsed);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool AssetPackages::request(const std::string& name,std::string& error){
    if(!fault_.empty()){error=fault_;return false;}
    const auto i=packages_.find(name);if(i==packages_.end()){error="Source asset package not exported: "+name;return false;}
    auto& p=i->second;if(p.requested){error.clear();return true;}
    p.requested=true;for(const auto& inc:p.includes)if(!request(inc,error))return false;
    pending_.push_back(name);error.clear();return true;
}
bool AssetPackages::pump(size_t budget,std::string& error){
    if(!fault_.empty()){error=fault_;return false;}
    try{
        while(budget&&!pending_.empty()){
            auto& p=packages_.at(pending_.front());
            if(p.nextTexture==p.textures.size()){p.cpuReady=true;pending_.pop_front();continue;}
            auto& t=p.textures[p.nextTexture];
            const auto size=size_t(t.width)*t.height*4;
            if(const auto cached=resident_.find(t.path);cached!=resident_.end()){
                if(cached->second->size()!=size)throw std::runtime_error("Shared original atlas extent mismatch: "+t.name);
                t.rgba=cached->second;
            }else{
                auto asset=assets_->open(t.path);
                if(!asset||asset->size()!=size)throw std::runtime_error("Missing/truncated original atlas: "+t.name);
                auto bytes=std::make_shared<std::vector<uint8_t>>(size);size_t at=0;
                while(at<size){const auto got=asset->read(bytes->data()+at,size-at);if(!got)throw std::runtime_error("Original atlas read failed: "+t.name);at+=got;}
                t.rgba=std::move(bytes);resident_.emplace(t.path,t.rgba);
            }
            ++p.nextTexture;--budget;
            if(p.nextTexture==p.textures.size()){p.cpuReady=true;pending_.pop_front();}
        }
        error.clear();return true;
    }catch(const std::exception& e){fault_=e.what();error=fault_;return false;}
}
const PackageState* AssetPackages::package(const std::string& n)const{const auto i=packages_.find(n);return i==packages_.end()?nullptr:&i->second;}
const PackageTexture* AssetPackages::texture(const std::string& n)const{for(const auto& [name,p]:packages_)for(const auto& t:p.textures)if(t.name==n&&t.rgba&&!t.rgba->empty())return &t;return nullptr;}
AssetPackages::Residency AssetPackages::residency()const{Residency out;out.uniqueTextures=resident_.size();for(const auto& [path,bytes]:resident_)out.residentBytes+=bytes->size();for(const auto& [name,p]:packages_)for(const auto& t:p.textures)if(t.rgba)out.logicalResidentBytes+=t.rgba->size();return out;}
std::string AssetPackages::dump()const{const auto memory=residency();std::ostringstream out;out<<"ASSET_PACKAGES count="<<packages_.size()<<" pending="<<pending_.size()<<" fault="<<fault_<<" residentBytes="<<memory.residentBytes<<" logicalResidentBytes="<<memory.logicalResidentBytes<<" uniqueTextures="<<memory.uniqueTextures<<'\n';for(const auto& [name,p]:packages_){size_t bytes=0;for(const auto& t:p.textures)if(t.rgba)bytes+=t.rgba->size();out<<"PACKAGE "<<name<<" requested="<<p.requested<<" cpuReady="<<p.cpuReady<<" textures="<<p.nextTexture<<'/'<<p.textures.size()<<" residentBytes="<<bytes<<" residency=shared_logical gpuResidency=renderer_owned\n";}return out.str();}
}
