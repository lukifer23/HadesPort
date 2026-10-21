#include "content_store.hpp"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <vector>

namespace hades {
namespace {
constexpr uint32_t K[64]={
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
inline uint32_t rotr(uint32_t x,int n){return (x>>n)|(x<<(32-n));}
bool hexDigest(std::string_view s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string_view::npos;}

class PosixFile final : public AssetFile {
    std::FILE* file_;
    size_t size_,position_=0;
public:
    PosixFile(std::FILE* file,size_t size):file_(file),size_(size){}
    ~PosixFile()override{std::fclose(file_);}
    size_t size()const override{return size_;}
    size_t remaining()const override{return size_-position_;}
    size_t read(void* target,size_t count)override{
        count=std::min(count,remaining());const auto got=std::fread(target,1,count,file_);position_+=got;return got;
    }
    bool seek(size_t offset)override{
        if(offset>size_||fseeko(file_,static_cast<off_t>(offset),SEEK_SET)!=0)return false;position_=offset;return true;
    }
};
std::unique_ptr<AssetFile> openFile(const std::string& path,uint64_t expected){
    std::FILE* f=std::fopen(path.c_str(),"rb");if(!f)return nullptr;
    struct stat st{};if(::fstat(fileno(f),&st)!=0||uint64_t(st.st_size)!=expected){std::fclose(f);return nullptr;}
    return std::make_unique<PosixFile>(f,size_t(st.st_size));
}
// Size + modification time identify a previously hashed blob in the stamp.
std::string fileIdentity(const std::string& path){
    struct stat st{};if(::stat(path.c_str(),&st)!=0)return {};
#if defined(__APPLE__)
    const long long sec=st.st_mtimespec.tv_sec,nsec=st.st_mtimespec.tv_nsec;
#else
    const long long sec=st.st_mtim.tv_sec,nsec=st.st_mtim.tv_nsec;
#endif
    return std::to_string((long long)st.st_size)+" "+std::to_string(sec)+"."+std::to_string(nsec);
}
std::mutex stampMutex;
std::map<std::string,std::set<std::string>> verifiedBlobs; // root -> "sha size mtime"
}

Sha256::Sha256(){h_={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};}
void Sha256::compress(const uint8_t* p){
    uint32_t w[64];
    for(int i=0;i<16;++i)w[i]=uint32_t(p[i*4])<<24|uint32_t(p[i*4+1])<<16|uint32_t(p[i*4+2])<<8|p[i*4+3];
    for(int i=16;i<64;++i){const uint32_t s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3),s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}
    uint32_t a=h_[0],b=h_[1],c=h_[2],d=h_[3],e=h_[4],f=h_[5],g=h_[6],h=h_[7];
    for(int i=0;i<64;++i){
        const uint32_t t1=h+(rotr(e,6)^rotr(e,11)^rotr(e,25))+((e&f)^(~e&g))+K[i]+w[i];
        const uint32_t t2=(rotr(a,2)^rotr(a,13)^rotr(a,22))+((a&b)^(a&c)^(b&c));
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    h_[0]+=a;h_[1]+=b;h_[2]+=c;h_[3]+=d;h_[4]+=e;h_[5]+=f;h_[6]+=g;h_[7]+=h;
}
void Sha256::update(const void* data,size_t n){
    auto* p=static_cast<const uint8_t*>(data);bytes_+=n;
    if(used_){const size_t take=std::min(n,64-used_);std::memcpy(block_.data()+used_,p,take);used_+=take;p+=take;n-=take;if(used_==64){compress(block_.data());used_=0;}}
    while(n>=64){compress(p);p+=64;n-=64;}
    if(n){std::memcpy(block_.data(),p,n);used_=n;}
}
std::array<uint8_t,32> Sha256::finish(){
    const uint64_t bits=bytes_*8;const uint8_t pad=0x80;update(&pad,1);const uint8_t zero=0;
    while(used_!=56)update(&zero,1);
    uint8_t length[8];for(int i=0;i<8;++i)length[i]=uint8_t(bits>>(56-8*i));update(length,8);
    std::array<uint8_t,32> out{};for(int i=0;i<8;++i)for(int j=0;j<4;++j)out[i*4+j]=uint8_t(h_[i]>>(24-8*j));return out;
}
std::string Sha256::hex(const std::array<uint8_t,32>& d){static const char* x="0123456789abcdef";std::string s;for(auto b:d){s+=x[b>>4];s+=x[b&15];}return s;}

bool ContentStore::configure(std::string root,std::string_view requirement,std::string stampPath,std::string& error){
    try{
        entries_.clear();id_.clear();totalBytes_=0;verified_=false;root_=std::move(root);stampPath_=std::move(stampPath);
        std::istringstream req{std::string(requirement)};std::string schema,id;
        if(!std::getline(req,schema)||schema!="hades-content-requirement-v1"||!std::getline(req,id)||!hexDigest(id))throw std::runtime_error("APK content requirement is malformed");
        const std::string manifestPath=root_+"/manifests/"+id+".txt";
        std::ifstream in(manifestPath,std::ios::binary);
        if(!in)throw std::runtime_error("External content "+id.substr(0,16)+" is not installed at "+root_+" (push the bundle's hades-content directory there)");
        const std::string bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
        if(bytes.size()>64*1024*1024)throw std::runtime_error("Content manifest exceeds bound");
        Sha256 sha;sha.update(bytes.data(),bytes.size());
        if(Sha256::hex(sha.finish())!=id)throw std::runtime_error("Content manifest "+id.substr(0,16)+" is corrupt (SHA-256 mismatch)");
        std::istringstream lines(bytes);std::string line;size_t count=0;
        if(!std::getline(lines,line)||line!="hades-content-store-v1"||!std::getline(lines,line))throw std::runtime_error("Unrecognized content manifest");
        count=std::stoull(line);
        std::string previous;
        while(std::getline(lines,line)){
            const auto a=line.find(' '),b=line.find(' ',a+1);
            if(a!=64||b==std::string::npos)throw std::runtime_error("Malformed content manifest row");
            Entry e;e.sha256=line.substr(0,64);e.bytes=std::stoull(line.substr(a+1,b-a-1));std::string path=line.substr(b+1);
            if(!hexDigest(e.sha256)||path.empty()||path.front()=='/'||path.find("..")!=std::string::npos||path.find('\\')!=std::string::npos||(!previous.empty()&&path<=previous))throw std::runtime_error("Invalid content manifest path: "+path);
            previous=path;totalBytes_+=e.bytes;
            struct stat st{};const auto blob=blobPath(e);
            if(::stat(blob.c_str(),&st)!=0)throw std::runtime_error("External content incomplete: blob for "+path+" missing ("+e.sha256.substr(0,16)+")");
            if(uint64_t(st.st_size)!=e.bytes)throw std::runtime_error("External content blob for "+path+" has wrong size");
            entries_.emplace(std::move(path),std::move(e));
        }
        if(entries_.size()!=count)throw std::runtime_error("Content manifest count mismatch");
        id_=id;
        std::lock_guard lock(stampMutex);auto& known=verifiedBlobs[root_];
        if(std::ifstream stamp{stampPath_}){std::string head;if(std::getline(stamp,head)&&head=="hades-content-verified-v1")while(std::getline(stamp,line))known.insert(line);}
        error.clear();return true;
    }catch(const std::exception& e){entries_.clear();id_.clear();error=e.what();return false;}
}
bool ContentStore::verify(std::string& error,const std::function<void(uint64_t,uint64_t)>& progress){
    if(!configured()){error="External content store not configured";return false;}
    if(verified_){error.clear();return true;}
    uint64_t done=0;std::set<std::string> seen;std::vector<uint8_t> buffer(4<<20);
    std::lock_guard lock(stampMutex);auto& known=verifiedBlobs[root_];
    std::ofstream stamp;
    for(const auto& [path,e]:entries_){
        if(!seen.insert(e.sha256).second)continue;
        const auto blob=blobPath(e);const auto key=e.sha256+" "+fileIdentity(blob);
        if(!known.contains(key)){
            auto file=openFile(blob,e.bytes);if(!file){error="External content blob unreadable: "+path;return false;}
            Sha256 sha;size_t got;while((got=file->read(buffer.data(),buffer.size()))>0){sha.update(buffer.data(),got);if(progress)progress(done+=got,totalBytes_);}
            if(Sha256::hex(sha.finish())!=e.sha256){error="External content blob corrupt (SHA-256 mismatch): "+path+" — re-push the content bundle";return false;}
            known.insert(key);
            if(!stampPath_.empty()){
                if(!stamp.is_open()){const bool fresh=!std::filesystem::exists(stampPath_);stamp.open(stampPath_,std::ios::app);if(fresh)stamp<<"hades-content-verified-v1\n";}
                stamp<<key<<'\n';
            }
        }else done+=e.bytes;
    }
    verified_=true;error.clear();return true;
}
std::unique_ptr<AssetFile> ContentStore::open(std::string_view name)const{
    const auto found=entries_.find(name);if(found==entries_.end())return nullptr;
    if(!verified_)throw std::runtime_error("External content opened before verification: "+std::string(name));
    auto file=openFile(blobPath(found->second),found->second.bytes);
    if(!file)throw std::runtime_error("External content blob vanished or changed size: "+std::string(name));
    return file;
}

void ContentAssets::configure(const std::string& storeRoot,const std::string& stampPath){
    required_=false;requirementError_.clear();
    auto file=bootstrap_.open("content-requirement.txt");if(!file)return; // legacy single-package root
    required_=true;std::string text(file->size(),'\0');size_t at=0;
    while(at<text.size()){const auto got=file->read(text.data()+at,text.size()-at);if(!got)break;at+=got;}
    if(at!=text.size()||text.size()>4096){requirementError_="APK content requirement unreadable";return;}
    if(storeRoot.empty()){requirementError_="External content directory unavailable (Context.getExternalFilesDir returned none)";return;}
    store_.configure(storeRoot,text,stampPath,requirementError_);
}
void ContentAssets::ensureReady(const std::function<void(uint64_t,uint64_t)>& progress){
    if(!required_)return;
    if(!requirementError_.empty())throw std::runtime_error(requirementError_);
    std::string error;if(!store_.verify(error,progress))throw std::runtime_error(error);
}
std::unique_ptr<AssetFile> ContentAssets::open(std::string_view name){
    if(required_&&store_.contains(name))return store_.open(name);
    return bootstrap_.open(name);
}
}
