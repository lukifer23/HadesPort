#pragma once
#include <cstddef>
#include <memory>
#include <string_view>

namespace hades {
class AssetFile {
public:
    virtual ~AssetFile() = default;
    virtual size_t size() const = 0;
    virtual size_t remaining() const = 0;
    virtual size_t read(void* target,size_t bytes) = 0;
    virtual bool seek(size_t offset) { (void)offset; return false; }
};
class AssetStore {
public:
    virtual ~AssetStore() = default;
    virtual std::unique_ptr<AssetFile> open(std::string_view name) = 0;
};
}
