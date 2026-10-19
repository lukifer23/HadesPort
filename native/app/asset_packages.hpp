#pragma once
#include "asset_store.hpp"
#include "source_value.hpp"
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>
namespace hades {
struct PackageTexture {
    std::string name,path,sha256;
    uint32_t width=0,height=0;
    SourceValue sprites;
    std::vector<uint8_t> rgba;
};
struct PackageState {
    std::string name,manifestHash,packageHash;
    std::vector<std::string> includes;
    std::vector<PackageTexture> textures;
    size_t nextTexture=0;
    bool requested=false,cpuReady=false;
};
// Native package requests remain asynchronous. This platform provider reads
// decoded original RGBA incrementally; GPU residency is owned by the renderer.
class AssetPackages {
    AssetStore* assets_;
    std::map<std::string,PackageState> packages_;
    std::deque<std::string> pending_;
    std::string fault_;
public:
    explicit AssetPackages(AssetStore* a):assets_(a){}
    bool loadCatalog(SourceValue,std::string& error);
    bool request(const std::string&,std::string& error);
    bool pump(size_t textureBudget,std::string& error);
    const PackageState* package(const std::string&)const;
    const PackageTexture* texture(const std::string&)const;
    bool pending()const{return !pending_.empty();}
    std::string dump()const;
};
}
