#pragma once
#include "../app/asset_store.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace hades {
class FileAsset final : public AssetFile {
    std::ifstream stream_;
    size_t size_,position_=0;
public:
    explicit FileAsset(const std::filesystem::path& path):stream_(path,std::ios::binary),size_(std::filesystem::file_size(path)){
        if(!stream_)throw std::runtime_error("Asset open failed");
    }
    size_t size()const override{return size_;}
    size_t remaining()const override{return size_-position_;}
    size_t read(void* target,size_t count)override{
        count=std::min(count,remaining());stream_.read(static_cast<char*>(target),static_cast<std::streamsize>(count));
        const auto got=static_cast<size_t>(stream_.gcount());position_+=got;return got;
    }
    bool seek(size_t offset)override{
        if(offset>size_)return false;
        stream_.clear();stream_.seekg(static_cast<std::streamoff>(offset));
        if(!stream_)return false;position_=offset;return true;
    }
};
class FileAssets final : public AssetStore {
    std::filesystem::path root_;
public:
    explicit FileAssets(const std::filesystem::path& root):root_(std::filesystem::canonical(root)){}
    std::unique_ptr<AssetFile> open(std::string_view name)override{
        const std::filesystem::path relative(name);
        if(relative.is_absolute())throw std::runtime_error("Absolute asset path rejected");
        for(const auto& component:relative)if(component=="..")throw std::runtime_error("Asset traversal rejected");
        const auto candidate=root_/relative;
        if(!std::filesystem::exists(candidate))return nullptr;
        const auto path=std::filesystem::canonical(candidate);
        const auto inside=path.lexically_relative(root_);
        if(inside.empty()||*inside.begin()==".."||!std::filesystem::is_regular_file(path))
            throw std::runtime_error("Asset path leaves its root");
        return std::make_unique<FileAsset>(path);
    }
};
}
