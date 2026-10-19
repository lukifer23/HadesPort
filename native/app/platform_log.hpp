#pragma once
#include <cstdarg>
#ifdef __ANDROID__
#include <android/log.h>
#else
#include <cstdio>
#endif
namespace hades {
enum class Severity {Info,Error};
inline void log(Severity severity,const char* tag,const char* format,...) {
    va_list args;va_start(args,format);
#ifdef __ANDROID__
    __android_log_vprint(severity==Severity::Error?ANDROID_LOG_ERROR:ANDROID_LOG_INFO,tag,format,args);
#else
    std::fprintf(stderr,"%s %s: ",severity==Severity::Error?"ERROR":"INFO",tag);
    std::vfprintf(stderr,format,args);std::fputc('\n',stderr);
#endif
    va_end(args);
}
}
