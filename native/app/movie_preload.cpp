#include "movie_preload.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <zlib.h>
namespace hades {
namespace {
const SourceValue& field(const SourceValue& v,const char* key){const auto* p=v.get(key);if(!p)throw std::runtime_error(std::string("Missing movie catalog field: ")+key);return *p;}
uint64_t number(const SourceValue& v,uint64_t limit){const double n=v.number();if(!std::isfinite(n)||n<0||n!=std::trunc(n)||n>double(limit))throw std::runtime_error("Movie catalog integer outside bounds");return uint64_t(n);}
bool name(const std::string& s){return !s.empty()&&s.size()<=4096&&s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-/.")==std::string::npos&&s.front()!='/'&&s.find("..") ==std::string::npos;}
bool hash(const std::string& s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
}
const char* moviePreloadOutcomeName(MoviePreloadOutcome o){switch(o){case MoviePreloadOutcome::Loaded:return "compressed_loaded";case MoviePreloadOutcome::AlreadyCached:return "already_cached";case MoviePreloadOutcome::MissingBase:return "missing_base";case MoviePreloadOutcome::MissingOptionalMask:return "missing_optional_mask";case MoviePreloadOutcome::CacheFull:return "cache_full";case MoviePreloadOutcome::ReadFailure:return "read_failure";case MoviePreloadOutcome::IntegrityMismatch:return "integrity_mismatch";}return "unknown";}
MoviePreload::MoviePreload(AssetStore* a,uint64_t streamBytes,uint64_t weaponBytes):assets_(a){stream_.capacity=streamBytes;weapon_.capacity=weaponBytes;}
MoviePreload::Cache& MoviePreload::cache(MovieCacheKind k){return k==MovieCacheKind::Weapon?weapon_:stream_;}
const MoviePreload::Cache& MoviePreload::cache(MovieCacheKind k)const{return k==MovieCacheKind::Weapon?weapon_:stream_;}
bool MoviePreload::loadCatalog(const SourceValue& root,std::string& error){
    try{
        if(catalogLoaded_||!assets_)throw std::runtime_error("Movie catalog already loaded or no actual AssetStore");
        if(field(root,"schema").text()!="hades-native-movie-preload-v1"||field(root,"mask_suffix").text()!="_mask")throw std::runtime_error("Unrecognized original movie catalog");
        std::map<std::string,Asset> parsed;
        for(const auto& [key,record]:field(root,"movies").object()){
            Asset a;a.filename=field(record,"filename").text();a.path=field(record,"path").text();a.sha256=field(record,"sha256").text();a.bytes=number(field(record,"bytes"),std::numeric_limits<uint32_t>::max());a.crc32=uint32_t(number(field(record,"crc32"),std::numeric_limits<uint32_t>::max()));
            if(!name(key)||!name(a.filename)||!hash(a.sha256)||a.path!="movie-preloads/"+a.sha256+".bik")throw std::runtime_error("Invalid original movie identity/path");
            parsed.emplace(key,std::move(a));
        }
        catalog_=std::move(parsed);catalogLoaded_=true;error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
void MoviePreload::reset(MovieCacheKind kind){auto& c=cache(kind);c.files.clear();c.used=0;++c.generation;}
bool MoviePreload::admit(const std::string& requested,const std::string& movie,MovieCacheKind kind,bool optional,MoviePreloadResult& result,std::string& error){
    MoviePreloadAttempt a;a.requestedName=requested;a.movieName=movie;a.cache=kind;
    const auto src=catalog_.find(movie);
    if(src==catalog_.end()){a.outcome=optional?MoviePreloadOutcome::MissingOptionalMask:MoviePreloadOutcome::MissingBase;result.attempts.push_back(std::move(a));return true;}
    categories_[movie]=kind;const auto& source=src->second;a.filename=source.filename;a.bytes=source.bytes;
    auto& c=cache(kind);const auto old=c.files.find(source.filename);
    if(old!=c.files.end()){a.outcome=MoviePreloadOutcome::AlreadyCached;a.reservationOffset=old->second.reservationOffset;a.reservationBytes=old->second.reservationBytes;result.attempts.push_back(std::move(a));return true;}
    const uint64_t padded=(source.bytes+15)&~uint64_t(15);
    if(c.used>std::numeric_limits<uint64_t>::max()-padded||c.used+padded>=c.capacity){a.outcome=MoviePreloadOutcome::CacheFull;result.attempts.push_back(std::move(a));return true;}
    a.reservationOffset=c.used;a.reservationBytes=padded;c.used+=padded;
    // Native reserves before reading and never rolls back a failed read.
    auto file=assets_->open(source.path);
    if(!file){a.outcome=MoviePreloadOutcome::ReadFailure;result.attempts.push_back(std::move(a));return true;}
    if(file->size()!=source.bytes){a.outcome=MoviePreloadOutcome::IntegrityMismatch;result.attempts.push_back(std::move(a));return true;}
    MovieCompressedEntry entry;entry.name=movie;entry.filename=source.filename;entry.sourceSha256=source.sha256;entry.reservationOffset=a.reservationOffset;entry.reservationBytes=padded;entry.generation=c.generation;
    try{entry.bytes.resize(size_t(source.bytes));}catch(const std::exception& e){error=std::string("Actual movie cache allocation failed: ")+e.what();return false;}
    size_t at=0;uLong crc=::crc32(0,nullptr,0);
    while(at<entry.bytes.size()){
        const size_t limit=std::min<size_t>(64*1024,entry.bytes.size()-at);const size_t got=file->read(entry.bytes.data()+at,limit);
        if(!got||got>limit){a.outcome=MoviePreloadOutcome::ReadFailure;result.attempts.push_back(std::move(a));return true;}
        crc=::crc32(crc,entry.bytes.data()+at,uInt(got));at+=got;
    }
    if(uint32_t(crc)!=source.crc32){a.outcome=MoviePreloadOutcome::IntegrityMismatch;result.attempts.push_back(std::move(a));return true;}
    c.files.emplace(source.filename,std::move(entry));a.outcome=MoviePreloadOutcome::Loaded;result.attempts.push_back(std::move(a));return true;
}
bool MoviePreload::preload(const std::vector<std::string>& names,const std::string& cacheName,bool resetFirst,MoviePreloadResult& result,std::string& error){
    result={};if(!catalogLoaded_){error="Original movie catalog not loaded";return false;}
    if(cacheName.find('\0')!=std::string::npos||std::any_of(names.begin(),names.end(),[](const auto& n){return !name(n);})){error="Movie preload request has unsupported exact source text/path";return false;}
    const auto kind=cacheName=="WeaponCache"?MovieCacheKind::Weapon:MovieCacheKind::Stream;
    if(resetFirst)reset(kind);
    for(const auto& n:names){
        const bool baseExists=catalog_.contains(n);
        if(!admit(n,n,kind,false,result,error))return false;
        if(baseExists&&!admit(n,n+"_mask",kind,true,result,error))return false;
    }
    error.clear();return true;
}
const MovieCompressedEntry* MoviePreload::cached(MovieCacheKind k,const std::string& n)const{const auto src=catalog_.find(n);if(src==catalog_.end())return nullptr;const auto& c=cache(k);const auto it=c.files.find(src->second.filename);return it==c.files.end()?nullptr:&it->second;}
std::string MoviePreload::dump()const{std::ostringstream out;out<<"MOVIE_PRELOAD catalog="<<catalog_.size()<<" loaded="<<catalogLoaded_<<" decodedPixels=not_provided gpuResidency=not_provided\n";for(const auto kind:{MovieCacheKind::Stream,MovieCacheKind::Weapon}){const auto& c=cache(kind);out<<"MOVIE_CACHE category="<<(kind==MovieCacheKind::Weapon?"WeaponCache":"Stream")<<" count="<<c.files.size()<<" reserved="<<c.used<<" capacity="<<c.capacity<<" generation="<<c.generation<<'\n';}return out.str();}
}
