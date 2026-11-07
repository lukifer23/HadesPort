#include "durable_file.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace hades::storage {
namespace {
struct FileDescriptor {
    int value=-1;
    explicit FileDescriptor(int fd):value(fd){}
    ~FileDescriptor(){if(value>=0)::close(value);}
    bool close(std::string& error){const int fd=value;value=-1;if(::close(fd)==0)return true;error="Close publication file: "+std::string(std::strerror(errno));return false;}
};
bool fail(const char* operation,std::string& error){error=std::string(operation)+": "+std::strerror(errno);return false;}
bool synchronize(int fd,std::string& error){
    int result;do{result=::fsync(fd);}while(result<0&&errno==EINTR);
    return result==0||fail("Synchronize publication",error);
}
bool directory(const std::filesystem::path& path,std::string& error){
    std::error_code ec;
    if(std::filesystem::is_directory(path,ec))return true;
    if(ec&&ec!=std::errc::no_such_file_or_directory){error="Inspect publication directory: "+ec.message();return false;}
    const auto parent=path.parent_path().empty()?std::filesystem::path("."):path.parent_path();
    if(parent==path){error="Publication directory unavailable";return false;}
    if(!directory(parent,error))return false;
    if(::mkdir(path.c_str(),0700)!=0&&errno!=EEXIST)return fail("Create publication directory",error);
    if(!std::filesystem::is_directory(path,ec)){error="Publication parent is not a directory";return false;}
    FileDescriptor fd(::open(parent.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC));
    return fd.value>=0?synchronize(fd.value,error):fail("Open publication parent",error);
}
}
bool publishFile(const std::string& path,const void* data,size_t size,std::string& error){
    error.clear();
    if(path.empty()||path.find('\0')!=std::string::npos||(!data&&size)){error="Invalid publication path/data";return false;}
    const std::filesystem::path destination(path);
    if(destination.filename().empty()||destination.filename()=="."||destination.filename()==".."){error="Publication requires a file name";return false;}
    const auto parent=destination.parent_path().empty()?std::filesystem::path("."):destination.parent_path();
    if(!directory(parent,error))return false;
    FileDescriptor parentFd(::open(parent.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC));
    if(parentFd.value<0)return fail("Open publication directory",error);
    std::string pattern=(parent/(destination.filename().string()+".publish-XXXXXX")).string();
    std::vector<char> temporary(pattern.begin(),pattern.end());temporary.push_back(0);
    FileDescriptor output(::mkstemp(temporary.data()));
    if(output.value<0)return fail("Create publication file",error);
    struct RemoveTemporary {const char* path;~RemoveTemporary(){::unlink(path);}} remove{temporary.data()};
    if(::fcntl(output.value,F_SETFD,FD_CLOEXEC)<0)return fail("Set publication close-on-exec",error);
    const auto* bytes=static_cast<const unsigned char*>(data);
    size_t at=0;
    while(at<size){
        const auto count=std::min(size-at,static_cast<size_t>(std::numeric_limits<ssize_t>::max()));
        const ssize_t wrote=::write(output.value,bytes+at,count);
        if(wrote<0&&errno==EINTR)continue;
        if(wrote<=0){if(wrote==0)errno=EIO;return fail("Write publication file",error);}
        at+=static_cast<size_t>(wrote);
    }
    if(!synchronize(output.value,error)||!output.close(error))return false;
    if(::rename(temporary.data(),path.c_str())!=0)return fail("Rename publication file",error);
    if(synchronize(parentFd.value,error))return true;
    error="Complete file renamed; directory durability not confirmed: "+error;
    return false;
}
}
