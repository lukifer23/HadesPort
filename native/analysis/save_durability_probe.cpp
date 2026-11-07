// Real filesystem/subprocess checks with copies of two actual owner saves.
// This is a storage diagnostic; it does not advance gameplay or prove that an
// original-script checkpoint/relaunch succeeds on Android.
#include "../app/save_runtime.hpp"
#include "../app/durable_file.hpp"
#include <cerrno>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
using Bytes=std::vector<uint8_t>;
void require(bool value,const std::string& error){if(!value)throw std::runtime_error(error);}
Bytes read(const std::string& path){Bytes bytes;std::string error;require(hades::save::readSaveFile(path,bytes,error),error);return bytes;}
void publish(const std::string& path,const Bytes& bytes){std::string error;require(hades::storage::publishFile(path,bytes.data(),bytes.size(),error),error);}
void save(const std::string& path,const Bytes& bytes){std::string error;require(hades::save::writeSaveFile(path,bytes,true,error),error);}
void valid(const Bytes& bytes){hades::save::SaveFile file;std::string lua,error;require(hades::save::decodeSave(bytes,true,file,error)&&hades::save::luabinsState(file,lua,error),error);}
void recovered(const std::string& path,bool backup){hades::save::SaveFile file;std::string actual,error;require(hades::save::readRecoverableSave(path,file,actual,error),error);require(actual==path+(backup?".bak":""),"Unexpected recovery candidate");}
void waitFor(pid_t child,int& status){pid_t got;do{got=::waitpid(child,&status,0);}while(got<0&&errno==EINTR);require(got==child,"waitpid failed");}
struct Child {
    pid_t pid;
    ~Child(){if(pid>0){::kill(pid,SIGKILL);int status;while(::waitpid(pid,&status,0)<0&&errno==EINTR){}}}
    int wait(){int status=0;waitFor(pid,status);pid=-1;return status;}
};
}
int main(int argc,char** argv){try{
    require(argc==4,"Usage: hades-save-durability-probe ACTUAL_SAVE_A ACTUAL_SAVE_B NEW_DIRECTORY");
    const auto a=read(argv[1]),b=read(argv[2]);valid(a);valid(b);require(a!=b,"Choose two distinct actual save files");
    const std::filesystem::path root(argv[3]);require(!std::filesystem::exists(root),"Probe directory already exists");
    require(std::filesystem::create_directories(root),"Cannot create probe directory");
    const auto path=(root/"Profile1.sav").string();
    save(path,a);require(read(path)==a,"First publication differs");
    save(path,b);require(read(path)==b&&read(path+".bak")==a,"Publication/backup differs");
    recovered(path,false); // Valid primary always wins over its own backup.
    publish(path,Bytes(b.begin(),b.begin()+b.size()/2));recovered(path,true);
    save(path,b);require(read(path+".bak")==a,"Damaged primary replaced known-valid backup");
    require(std::filesystem::remove(path),"Cannot remove owned test primary");recovered(path,true);
    require(hades::save::profileSaveExists(root.string(),"Profile1"),"Backup-only Continue unavailable");
    save(path,b);
    const auto backupBefore=read(path+".bak");
    // A true short/failed filesystem write: the child has a kernel file-size
    // limit below the actual save size. No fake writer/provider is installed.
    for(const bool backup:{false,true}){
        const pid_t limited=::fork();require(limited>=0,"fork failed");
        if(limited==0){
            ::signal(SIGXFSZ,SIG_IGN);const rlimit limit{1,1};
            if(::setrlimit(RLIMIT_FSIZE,&limit)!=0)_exit(3);
            std::string error;_exit(hades::save::writeSaveFile(path,a,backup,error)?2:0);
        }
        Child limitedChild{limited};const int status=limitedChild.wait();
        require(WIFEXITED(status)&&WEXITSTATUS(status)==0,"Kernel write limit did not reject publication");
        require(read(path)==b&&read(path+".bak")==backupBefore,"Failed publication changed retained files");
    }
    for(const auto& entry:std::filesystem::directory_iterator(root))require(entry.path().filename().string().find(".publish-")==std::string::npos,"Failed publication leaked owned temporary file");
    // Kill a real writer while its directory is changing. SIGKILL may race
    // rename; either complete old or complete new data is valid. Filesystem
    // power-loss guarantees remain an Android hardware acceptance gate.
    const auto killedPath=(root/"interrupted"/"Profile1.sav").string();
    save(killedPath,a);save(killedPath,b);
    require(read(killedPath)==b&&read(killedPath+".bak")==a,"Interrupted-writer seed lacks actual primary/backup");
    const pid_t child=::fork();require(child>=0,"fork failed");
    if(child==0){for(unsigned i=0;i<256;++i){std::string error;if(!hades::save::writeSaveFile(killedPath,(i&1)?a:b,true,error))_exit(3);}_exit(0);}
    Child writer{child};
    bool observed=false;
    for(unsigned attempts=0;attempts<100000&&!observed;++attempts){
        for(const auto& entry:std::filesystem::directory_iterator(root/"interrupted"))if(entry.path().filename().string().find(".publish-")!=std::string::npos){observed=true;break;}
        if(!observed)::usleep(10);
    }
    ::kill(child,SIGKILL);const int status=writer.wait();
    require(observed&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGKILL,"Did not observe/interrupt actual writer");
    const auto after=read(killedPath);require(after==a||after==b,"Interrupted writer exposed partial primary");valid(after);valid(read(killedPath+".bak"));
    // Reject damaged primary+backup rather than manufacturing a save state.
    const auto damaged=(root/"damaged.sav").string();publish(damaged,{a.begin(),a.begin()+8});publish(damaged+".bak",{b.begin(),b.begin()+8});
    hades::save::SaveFile file;std::string actual,error;
    require(!hades::save::readRecoverableSave(damaged,file,actual,error),"Damaged candidates accepted");
    // The validity marker is separately atomically published, unchanged on disk.
    const auto marker=(root/"Profile1.v.sav").string();const auto falseMarker=hades::save::encodeValidCheckpoint(false),trueMarker=hades::save::encodeValidCheckpoint(true);
    save(marker,falseMarker);save(marker,trueMarker);require(read(marker)==trueMarker&&read(marker+".bak")==falseMarker,"Validity marker/backup differs");
    std::cout<<"{\"ok\":true,\"checks\":11,\"actual_save_inputs\":2,\"kernel_write_failure\":true,\"subprocess_interrupted\":true,\"scope\":\"host durable publication and validated per-file backup recovery; no gameplay or power-loss acceptance\"}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"save durability probe failed: "<<error.what()<<'\n';return 1;}}
