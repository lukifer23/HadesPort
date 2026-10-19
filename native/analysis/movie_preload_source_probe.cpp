#include "../app/movie_preload.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
using namespace hades;
struct File:AssetFile {
    std::ifstream stream;size_t length=0,pos=0;
    explicit File(const std::filesystem::path& p):stream(p,std::ios::binary){if(stream)length=size_t(std::filesystem::file_size(p));}
    size_t size()const override{return length;}size_t remaining()const override{return length-pos;}
    size_t read(void* out,size_t n)override{stream.read(static_cast<char*>(out),std::streamsize(n));const auto got=size_t(stream.gcount());pos+=got;return got;}
};
struct Store:AssetStore {
    std::filesystem::path root;explicit Store(std::filesystem::path p):root(std::move(p)){}
    std::unique_ptr<AssetFile> open(std::string_view name)override{auto f=std::make_unique<File>(root/std::string(name));return f->stream?std::move(f):nullptr;}
};
int main(int argc,char** argv){
    if(argc<4){std::cerr<<"source asset root, catalog.lua, stream names..., --weapon, weapon names... required\n";return 2;}
    Store store(argv[1]);MoviePreload cache(&store);std::string error;SourceValue catalog;
    lua_State* L=luaL_newstate();assert(L);
    if(luaL_loadfile(L,argv[2])||lua_pcall(L,0,1,0)||!readSourceValue(L,-1,catalog,error)||!cache.loadCatalog(catalog,error)){std::cerr<<"catalog failed "<<error<<'\n';lua_close(L);return 1;}lua_close(L);
    std::vector<std::string> stream,weapon;bool w=false;
    for(int i=3;i<argc;++i){if(std::string(argv[i])=="--weapon")w=true;else(w?weapon:stream).push_back(argv[i]);}
    bool complete=true;uint64_t actual=0;
    for(bool useWeapon:{false,true}){
        MoviePreloadResult result;if(!cache.preload(useWeapon?weapon:stream,useWeapon?"WeaponCache":"",false,result,error)){std::cerr<<error<<'\n';return 1;}
        for(const auto& a:result.attempts){std::cout<<"MOVIE_SOURCE_PROBE cache="<<(useWeapon?"WeaponCache":"Stream")<<" name="<<a.movieName<<" outcome="<<moviePreloadOutcomeName(a.outcome)<<" bytes="<<a.bytes<<" offset="<<a.reservationOffset<<'\n';if(a.outcome==MoviePreloadOutcome::Loaded)actual+=a.bytes;else if(a.outcome!=MoviePreloadOutcome::MissingOptionalMask&&a.outcome!=MoviePreloadOutcome::AlreadyCached)complete=false;}
    }
    std::cout<<cache.dump()<<"MOVIE_SOURCE_PROBE admittedOriginalCompressedBytes="<<actual<<" allBasesAvailable="<<complete<<'\n';return complete?0:1;
}
