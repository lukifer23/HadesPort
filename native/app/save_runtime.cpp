#include "save_runtime.hpp"
#include <lz4.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

namespace hades::save {
namespace {
constexpr uint32_t kAdlerModulo=65521;
constexpr size_t kAdlerBlock=0x15b0; // sgg::adler32 NMAX 5552

// BinarySaveData stream writes are raw little-endian copies (MemoryFileIO).
void putU32(std::vector<uint8_t>& out,uint32_t v){for(int i=0;i<4;++i)out.push_back(uint8_t(v>>(8*i)));}
void putU64(std::vector<uint8_t>& out,uint64_t v){for(int i=0;i<8;++i)out.push_back(uint8_t(v>>(8*i)));}
// BinarySaveData::saveString: u32 length, then the bytes (no terminator).
bool putString(std::vector<uint8_t>& out,const std::string& s,std::string& error){
    if(s.size()>std::numeric_limits<uint32_t>::max()){error="Save string exceeds u32 length";return false;}
    putU32(out,uint32_t(s.size()));out.insert(out.end(),s.begin(),s.end());return true;
}

struct Reader {
    const std::vector<uint8_t>& bytes;size_t at;
    bool u8(uint8_t& v){if(at+1>bytes.size())return false;v=bytes[at++];return true;}
    bool u32(uint32_t& v){if(at+4>bytes.size())return false;v=0;for(int i=0;i<4;++i)v|=uint32_t(bytes[at+i])<<(8*i);at+=4;return true;}
    bool i32(int32_t& v){uint32_t u;if(!u32(u))return false;std::memcpy(&v,&u,4);return true;}
    bool u64(uint64_t& v){if(at+8>bytes.size())return false;v=0;for(int i=0;i<8;++i)v|=uint64_t(bytes[at+i])<<(8*i);at+=8;return true;}
    bool boolean(bool& v){uint8_t b;if(!u8(b))return false;v=b!=0;return true;}
    // BinaryLoadData::loadString (magic/cppCompat stream): u32 length + bytes.
    bool string(std::string& v){uint32_t n;if(!u32(n)||n>bytes.size()-at)return false;v.assign(reinterpret_cast<const char*>(bytes.data()+at),n);at+=n;return true;}
    bool stringList(std::vector<std::string>& v){uint32_t n;if(!u32(n)||n>bytes.size()-at)return false;v.clear();for(uint32_t i=0;i<n;++i){std::string s;if(!string(s))return false;v.push_back(std::move(s));}return true;}
};
}

uint32_t adler32(const uint8_t* data,size_t length){
    uint32_t a=1,b=0;
    while(length){
        const size_t block=length<kAdlerBlock?length:kAdlerBlock;length-=block;
        for(size_t i=0;i<block;++i){a+=*data++;b+=a;}
        a%=kAdlerModulo;b%=kAdlerModulo;
    }
    return (b<<16)|a;
}

bool encodeSave(const SaveInfo& info,const std::string& currentMap,const std::string& nextMap,const std::string& lua,std::vector<uint8_t>& out,std::string& error){
    // BinarySaveData(0x300000, checksum=true): an 8-byte header is reserved and
    // filled by PrepareForWrite after the payload is complete.
    out.assign(8,0);
    putU32(out,uint32_t(kVersion));
    putU64(out,info.timestamp);
    if(!putString(out,info.location,error))return false;
    putU32(out,uint32_t(info.completedRuns));
    putU32(out,uint32_t(info.accumulatedMetaPoints));
    putU32(out,uint32_t(info.spentShrinePoints));
    out.push_back(info.easyMode?1:0);
    out.push_back(info.hardMode?1:0);
    if(info.notable.size()>std::numeric_limits<uint32_t>::max()){error="Notable list too long";return false;}
    putU32(out,uint32_t(info.notable.size()));
    for(const auto& s:info.notable)if(!putString(out,s,error))return false;
    if(!putString(out,currentMap,error)||!putString(out,nextMap,error))return false;
    // GatherSaveData: the stream position is the full buffer position, header included.
    if(out.size()>kGatherHeaderLimit){error="Save header exceeds 0x4000; original skips Lua state and reports failure";return false;}
    if(lua.size()>=kLuaStateLimit){error="Uncompressed lua state exceeded max allowable";return false;}
    std::vector<char> compressed(kLuaStateLimit);
    const int size=LZ4_compress_default(lua.data(),compressed.data(),int(lua.size()),int(compressed.size()));
    if(size<=0){error="Unable to compress lua state";return false;}
    if(size_t(size)>kCompressedLuaLimit){error="Compressed lua state exceeded max allowable";return false;}
    putU32(out,uint32_t(size));
    out.insert(out.end(),compressed.begin(),compressed.begin()+size);
    // PrepareForWrite: seek 0, write magic, then adler32(buffer + 8, position - 8).
    const uint32_t checksum=adler32(out.data()+8,out.size()-8);
    for(int i=0;i<4;++i){out[i]=uint8_t(kMagic>>(8*i));out[4+i]=uint8_t(checksum>>(8*i));}
    return true;
}

bool decodeSave(const std::vector<uint8_t>& bytes,bool verifyChecksum,SaveFile& file,std::string& error){
    Reader r{bytes,0};uint32_t magic=0;
    // BinaryLoadData::fromStream: without the magic the engine rewinds and uses
    // the pre-cppCompat layout (different string encoding); not reconstructed.
    if(!r.u32(magic)||magic!=kMagic){error="Didn't find cppCompatVersion magic number";return false;}
    uint32_t stored=0;if(!r.u32(stored)){error="Save header truncated";return false;}
    if(verifyChecksum&&adler32(bytes.data()+8,bytes.size()-8)!=stored){error="saveDevice.Load checksum failed";return false;}
    file=SaveFile{};
    if(!r.i32(file.version)){error="Save version missing";return false;}
    const int v=file.version;
    if(v<kOldestVersion||v>kVersion){error="Save game is unsupported version, expecting 1 - 16, got "+std::to_string(v);return false;}
    // ProgressManager::Restore field order by version.
    bool ok=true;int32_t skip=0;bool skipBool=false;std::string skipString;
    if(v>=16)ok=ok&&r.u64(file.info.timestamp);
    if(v>=11)ok=ok&&r.string(file.info.location);
    if(v>=10){
        ok=ok&&r.i32(file.info.completedRuns)&&r.i32(file.info.accumulatedMetaPoints);
        if(v>=12&&v<14)ok=ok&&r.i32(skip);
        if(v>=14)ok=ok&&r.i32(file.info.spentShrinePoints)&&r.boolean(file.info.easyMode)&&r.boolean(file.info.hardMode);
        if(v>=13)ok=ok&&r.stringList(file.info.notable);
    }else{
        ok=ok&&r.string(skipString)&&r.string(skipString);
        if(v>=5)ok=ok&&r.string(skipString);
        ok=ok&&r.i32(skip)&&r.i32(skip);
        if(v>=2)ok=ok&&r.i32(skip)&&r.i32(skip);
        if(v>=7)ok=ok&&r.boolean(skipBool);
    }
    if(v>=6&&v<=12)ok=ok&&r.i32(skip)&&r.i32(skip);
    ok=ok&&r.string(file.currentMap)&&r.string(file.nextMap);
    // LoadLua: loadString of the Lua block.
    ok=ok&&r.string(file.luaBlock);
    if(!ok){error="Save payload truncated";return false;}
    return true;
}

bool luabinsState(const SaveFile& file,std::string& out,std::string& error){
    if(file.version<=14){out=file.luaBlock;return true;}
    if(file.luaBlock.size()>size_t(std::numeric_limits<int>::max())){error="Compressed save too large";return false;}
    out.resize(kLuaStateLimit);
    const int size=LZ4_decompress_safe(file.luaBlock.data(),out.data(),int(file.luaBlock.size()),int(out.size()));
    if(size<=0){out.clear();error="Unable to decompress save. Error code: "+std::to_string(size);return false;}
    out.resize(size_t(size));return true;
}

std::vector<uint8_t> encodeValidCheckpoint(bool valid){
    std::vector<uint8_t> out;
    putU32(out,kMagic);              // checksum disabled: 4-byte header only
    putU32(out,uint32_t(kVersion));
    out.push_back(valid?1:0);
    return out;
}

bool decodeValidCheckpoint(const std::vector<uint8_t>& bytes,bool& valid,std::string& error){
    Reader r{bytes,0};uint32_t magic=0;int32_t version=0;
    if(!r.u32(magic)||magic!=kMagic||!r.i32(version)||version<kOldestVersion||version>kVersion||!r.boolean(valid)){error="Invalid checkpoint validity file";return false;}
    return true;
}

bool fileExists(const std::string& path){std::error_code ec;return std::filesystem::is_regular_file(path,ec);}

bool readSaveFile(const std::string& path,std::vector<uint8_t>& bytes,std::string& error){
    std::ifstream in(path,std::ios::binary);
    if(!in){error="Unable to find save file: "+path;return false;}
    in.seekg(0,std::ios::end);const auto size=in.tellg();in.seekg(0);
    if(size<0||size_t(size)>64u*1024*1024){error="Save file size outside bounds";return false;}
    bytes.resize(size_t(size));
    if(size&&!in.read(reinterpret_cast<char*>(bytes.data()),size)){error="Save File ("+path+") is Corrupt";return false;}
    return true;
}

bool writeSaveFile(const std::string& path,const std::vector<uint8_t>& bytes,bool backup,std::string& error){
    std::error_code ec;
    const auto parent=std::filesystem::path(path).parent_path();
    if(!parent.empty()){std::filesystem::create_directories(parent,ec);if(ec){error="Cannot create save directory: "+ec.message();return false;}}
    if(backup&&fileExists(path)){
        std::filesystem::copy_file(path,path+kBackupSuffix,std::filesystem::copy_options::overwrite_existing,ec);
        if(ec){error="Save backup copy failed: "+ec.message();return false;}
    }
    std::FILE* f=std::fopen(path.c_str(),"wb");
    if(!f){error=std::string("Failed to save: ")+std::strerror(errno);return false;}
    const bool wrote=bytes.empty()||std::fwrite(bytes.data(),1,bytes.size(),f)==bytes.size();
    const bool flushed=std::fflush(f)==0;
    const bool closed=std::fclose(f)==0;
    if(!wrote||!flushed||!closed){error="Save corrupt";return false;}
    return true;
}

}
