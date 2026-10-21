#pragma once
// Versioned external content store (Android adaptation; no original engine
// counterpart). The original game reads loose Content/ files; on Android the
// derived packs exceed the APK's ZIP32 bound, so they live in the app-specific
// external files dir (Context.getExternalFilesDir, no permission needed,
// writable by `adb push`) as content-addressed blobs:
//
//   <root>/manifests/<id>.txt   "hades-content-store-v1\n<count>\n" then one
//                               "<sha256> <bytes> <logical path>\n" per file,
//                               sorted by path; <id> = SHA-256 of these bytes
//   <root>/blobs/<sha256>       the file bytes
//
// The APK carries only a bootstrap: ui/ and content-requirement.txt
// ("hades-content-requirement-v1\n<id>\n"). With a requirement present every
// manifest path is served ONLY from verified blobs; anything else (ui/) comes
// from the APK. A missing/mismatched store is a load failure with an explicit
// reason, never a fallback. The same layout is read by the host FileAssets.
#include "asset_store.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace hades {
class Sha256 {
    std::array<uint32_t,8> h_{};
    std::array<uint8_t,64> block_{};
    uint64_t bytes_=0;
    size_t used_=0;
    void compress(const uint8_t*);
public:
    Sha256();
    void update(const void*,size_t);
    std::array<uint8_t,32> finish();
    static std::string hex(const std::array<uint8_t,32>&);
};

class ContentStore {
public:
    struct Entry { std::string sha256; uint64_t bytes=0; };
private:
    std::string root_,id_,stampPath_;
    std::map<std::string,Entry,std::less<>> entries_;
    uint64_t totalBytes_=0;
    bool verified_=false;
    std::string blobPath(const Entry& e)const{return root_+"/blobs/"+e.sha256;}
public:
    // Reads + hash-checks the manifest named by the requirement and checks
    // that every blob exists with its recorded size. Cheap; no content hash.
    bool configure(std::string root,std::string_view requirementText,std::string stampPath,std::string& error);
    // Full SHA-256 of every blob, skipped when the stamp written by a previous
    // full verification still matches each blob's size and modification time.
    bool verify(std::string& error,const std::function<void(uint64_t done,uint64_t total)>& progress={});
    bool configured()const{return !id_.empty();}
    bool verified()const{return verified_;}
    bool contains(std::string_view name)const{return entries_.find(name)!=entries_.end();}
    std::unique_ptr<AssetFile> open(std::string_view name)const;
    const std::string& id()const{return id_;}
    const std::string& root()const{return root_;}
    size_t files()const{return entries_.size();}
    uint64_t totalBytes()const{return totalBytes_;}
};

// Bootstrap (APK or host directory) + optional required external store.
class ContentAssets final : public AssetStore {
    AssetStore& bootstrap_;
    ContentStore store_;
    std::string requirementError_;
    bool required_=false;
public:
    explicit ContentAssets(AssetStore& bootstrap):bootstrap_(bootstrap){}
    // Reads content-requirement.txt from the bootstrap. Without one the
    // bootstrap is the complete asset root (legacy single-APK build).
    void configure(const std::string& storeRoot,const std::string& stampPath);
    // Throws std::runtime_error with an actionable reason when required
    // content is missing, mismatched or fails verification.
    void ensureReady(const std::function<void(uint64_t,uint64_t)>& progress={});
    bool required()const{return required_;}
    const ContentStore& store()const{return store_;}
    std::unique_ptr<AssetFile> open(std::string_view name)override;
};
}
