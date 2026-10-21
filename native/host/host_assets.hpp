#pragma once
// Host asset root reading the same layout as the Android build:
//  - a flat staged directory (legacy single-package root), or
//  - a content bundle produced by `build_native.py --content-bundle OUT`:
//      OUT/apk-assets/   exactly the slim APK's assets (ui/, content-requirement.txt)
//      OUT/hades-content/ the external store pushed to getExternalFilesDir()
// The bundle is verified (SHA-256, stamp-cached) before the first open.
#include "file_assets.hpp"
#include "../app/content_store.hpp"
#include <iostream>
#include <memory>

namespace hades {
class HostAssets final : public AssetStore {
    std::unique_ptr<FileAssets> bootstrap_;
    std::unique_ptr<ContentAssets> content_;
public:
    explicit HostAssets(const std::filesystem::path& root){
        const auto apk=root/"apk-assets";
        if(std::filesystem::is_regular_file(apk/"content-requirement.txt")){
            bootstrap_=std::make_unique<FileAssets>(apk);content_=std::make_unique<ContentAssets>(*bootstrap_);
            const auto store=std::filesystem::canonical(root/"hades-content");
            content_->configure(store.string(),(store/"host-verified.txt").string());
            content_->ensureReady();
            std::cerr<<"HOST_CONTENT_STORE id="<<content_->store().id()<<" files="<<content_->store().files()<<" bytes="<<content_->store().totalBytes()<<'\n';
        }else bootstrap_=std::make_unique<FileAssets>(root);
    }
    std::unique_ptr<AssetFile> open(std::string_view name)override{return content_?content_->open(name):bootstrap_->open(name);}
};
}
