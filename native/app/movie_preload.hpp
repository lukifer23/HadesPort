#pragma once
#include "asset_store.hpp"
#include "source_value.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace hades {
enum class MovieCacheKind:uint8_t {Stream=0,Weapon=1};
enum class MoviePreloadOutcome {Loaded,AlreadyCached,MissingBase,MissingOptionalMask,CacheFull,ReadFailure,IntegrityMismatch};
struct MoviePreloadAttempt {
    std::string requestedName,movieName,filename;
    MovieCacheKind cache=MovieCacheKind::Stream;
    MoviePreloadOutcome outcome=MoviePreloadOutcome::MissingBase;
    uint64_t bytes=0,reservationOffset=0,reservationBytes=0;
};
struct MoviePreloadResult {std::vector<MoviePreloadAttempt> attempts;};
struct MovieCompressedEntry {
    std::string name,filename,sourceSha256;
    uint64_t reservationOffset=0,reservationBytes=0,generation=0;
    std::vector<uint8_t> bytes;
};
class MoviePreload {
    struct Asset {std::string filename,path,sha256;uint64_t bytes=0;uint32_t crc32=0;};
    struct Cache {uint64_t capacity=0,used=0,generation=1;std::map<std::string,MovieCompressedEntry> files;};
    AssetStore* assets_;
    std::map<std::string,Asset> catalog_;
    std::map<std::string,MovieCacheKind> categories_;
    Cache stream_,weapon_;
    bool catalogLoaded_=false;
    bool admit(const std::string&,const std::string&,MovieCacheKind,bool,MoviePreloadResult&,std::string&);
    Cache& cache(MovieCacheKind);
    const Cache& cache(MovieCacheKind)const;
public:
    static constexpr uint64_t NativeStreamBytes=0x23000000,NativeWeaponBytes=0x09600000;
    explicit MoviePreload(AssetStore* assets,uint64_t streamBytes=NativeStreamBytes,uint64_t weaponBytes=NativeWeaponBytes);
    bool loadCatalog(const SourceValue&,std::string&);
    // Exact case-sensitive WeaponCache selects1; all other cache strings select0.
    // Reset precedes the request list. Each base is followed by optional _mask.
    // Missing/full/read failures are retained attempts, matching native void
    // nonfatal admission. Malformed catalog/request returns false.
    bool preload(const std::vector<std::string>& names,const std::string& cacheName,
        bool reset,MoviePreloadResult&,std::string&);
    void reset(MovieCacheKind);
    // Borrowed compressed bytes only; invalidated by reset of that category.
    const MovieCompressedEntry* cached(MovieCacheKind,const std::string& name)const;
    std::string dump()const;
};
const char* moviePreloadOutcomeName(MoviePreloadOutcome);
}
