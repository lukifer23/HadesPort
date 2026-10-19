#include "../app/movie_preload.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <zlib.h>
using namespace hades;
struct MemoryFile:AssetFile {
    std::vector<uint8_t> data;size_t pos=0;
    explicit MemoryFile(std::vector<uint8_t> d):data(std::move(d)){}
    size_t size()const override{return data.size();}size_t remaining()const override{return data.size()-pos;}
    size_t read(void* target,size_t n)override{n=std::min<size_t>({n,size_t(7),remaining()});std::memcpy(target,data.data()+pos,n);pos+=n;return n;}
};
struct MemoryStore:AssetStore {
    std::map<std::string,std::vector<uint8_t>> files;size_t opens=0;
    std::unique_ptr<AssetFile> open(std::string_view n)override{++opens;auto it=files.find(std::string(n));return it==files.end()?nullptr:std::make_unique<MemoryFile>(it->second);}
};
int main(){
    MemoryStore store;SourceValue::Object movies;
    auto add=[&](const char* name,size_t size,char h){std::string hash(64,h),path="movie-preloads/"+hash+".bik";std::vector<uint8_t> bytes(size,uint8_t(h));store.files[path]=bytes;uLong crc=::crc32(0,nullptr,0);crc=::crc32(crc,bytes.data(),uInt(bytes.size()));
        movies[name]=SourceValue{SourceValue::Object{{"filename",SourceValue{std::string(name)+".bik"}},{"path",SourceValue{path}},{"sha256",SourceValue{hash}},{"bytes",SourceValue{double(size)}},{"crc32",SourceValue{double(crc)}}}};};
    add("A",17,'a');add("A_mask",1,'b');add("B",1,'c');
    SourceValue catalog{SourceValue::Object{{"schema",SourceValue{std::string("hades-native-movie-preload-v1")}},{"mask_suffix",SourceValue{std::string("_mask")}},{"movies",SourceValue{movies}}}};
    MoviePreload cache(&store,64,64);std::string error;assert(cache.loadCatalog(catalog,error));MoviePreloadResult r;
    assert(cache.preload({"A","B","Missing"},"",false,r,error));assert(r.attempts.size()==5);
    assert(r.attempts[0].outcome==MoviePreloadOutcome::Loaded&&r.attempts[0].reservationBytes==32&&r.attempts[0].reservationOffset==0);
    assert(r.attempts[1].outcome==MoviePreloadOutcome::Loaded&&r.attempts[1].reservationBytes==16&&r.attempts[1].reservationOffset==32);
    assert(r.attempts[2].outcome==MoviePreloadOutcome::CacheFull);assert(r.attempts[3].outcome==MoviePreloadOutcome::MissingOptionalMask);assert(r.attempts[4].outcome==MoviePreloadOutcome::MissingBase);
    const auto reads=store.opens;assert(cache.preload({"A"},"",false,r,error));assert(r.attempts[0].outcome==MoviePreloadOutcome::AlreadyCached&&store.opens==reads);
    assert(cache.cached(MovieCacheKind::Stream,"A")->bytes==std::vector<uint8_t>(17,'a'));
    assert(cache.preload({"A"},"WeaponCache",false,r,error));assert(cache.cached(MovieCacheKind::Weapon,"A"));
    assert(cache.preload({},"weaponcache",true,r,error));assert(!cache.cached(MovieCacheKind::Stream,"A")&&cache.cached(MovieCacheKind::Weapon,"A"));
    // Failed ReadAllBytes consumes native arena reservation, without cache entry.
    store.files.erase("movie-preloads/"+std::string(64,'c')+".bik");
    assert(cache.preload({"B","B"},"",false,r,error));assert(r.attempts[0].outcome==MoviePreloadOutcome::ReadFailure&&r.attempts[0].reservationOffset==0);assert(r.attempts[2].outcome==MoviePreloadOutcome::ReadFailure&&r.attempts[2].reservationOffset==16);assert(!cache.cached(MovieCacheKind::Stream,"B"));
    // Corrupted copied payload is never published as the source movie.
    store.files["movie-preloads/"+std::string(64,'a')+".bik"][0]=0;
    assert(cache.preload({"A"},"",true,r,error));assert(r.attempts[0].outcome==MoviePreloadOutcome::IntegrityMismatch&&!cache.cached(MovieCacheKind::Stream,"A"));
    assert(!cache.preload({"../A"},"",false,r,error));
    std::cout<<"native compressed movie preload admission/reset/integrity smoke passed\n";
}
