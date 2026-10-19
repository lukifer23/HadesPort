#pragma once
#include "asset_store.hpp"
#include <android/asset_manager.h>
#include <algorithm>
#include <string>

namespace hades {
class AndroidAsset final : public AssetFile {
    AAsset* asset_;
public:
    explicit AndroidAsset(AAsset* asset):asset_(asset){}
    ~AndroidAsset() override {AAsset_close(asset_);}
    size_t size()const override{return static_cast<size_t>(AAsset_getLength64(asset_));}
    size_t remaining()const override{return static_cast<size_t>(AAsset_getRemainingLength64(asset_));}
    size_t read(void* target,size_t count)override{
        const int got=AAsset_read(asset_,target,std::min(count,size_t{1<<20}));
        return got>0?static_cast<size_t>(got):0;
    }
    bool seek(size_t offset)override{
        return offset<=size()&&AAsset_seek64(asset_,static_cast<off64_t>(offset),SEEK_SET)==static_cast<off64_t>(offset);
    }
};
class AndroidAssets final : public AssetStore {
    AAssetManager* manager_;
public:
    explicit AndroidAssets(AAssetManager* manager):manager_(manager){}
    std::unique_ptr<AssetFile> open(std::string_view name)override{
        const std::string path(name);
        AAsset* file=AAssetManager_open(manager_,path.c_str(),AASSET_MODE_STREAMING);
        return file?std::make_unique<AndroidAsset>(file):nullptr;
    }
};
}
