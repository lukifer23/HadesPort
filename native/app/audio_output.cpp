#include "audio_output.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#ifdef __ANDROID__
#include <aaudio/AAudio.h>
#endif
namespace hades {
struct AudioOutput::Impl {
    static constexpr uint32_t Capacity=8192,Chunk=256,Target=2048;
    std::array<float,Capacity*2> ring{};
    std::atomic<uint64_t> write=0,read=0,submitted=0,underruns=0;
    std::atomic<bool> running=false,terminate=false;
    std::atomic<int> deviceError=0;
    std::thread worker;
    mutable std::mutex errorMutex;
    std::string renderError;
    Render render;
    int deviceRate=0,burst=0;
#ifdef __ANDROID__
    AAudioStream* stream=nullptr;
    static aaudio_data_callback_result_t callback(AAudioStream*,void* user,void* output,int32_t count){
        auto& self=*static_cast<Impl*>(user);auto* target=static_cast<float*>(output);
        if(count<0||!target||self.terminate.load(std::memory_order_acquire))return AAUDIO_CALLBACK_RESULT_STOP;
        uint64_t at=self.read.load(std::memory_order_relaxed),available=self.write.load(std::memory_order_acquire)-at;
        uint64_t remaining=std::min<uint64_t>(uint32_t(count),available),copied=remaining;
        while(remaining){
            const uint64_t offset=at%Capacity,n=std::min<uint64_t>(remaining,Capacity-offset);
            std::memcpy(target,self.ring.data()+offset*2,size_t(n)*2*sizeof(float));target+=n*2;at+=n;remaining-=n;
        }
        self.read.store(at,std::memory_order_release);
        if(copied<uint32_t(count)){
            const uint64_t missing=uint32_t(count)-copied;std::fill_n(target,size_t(missing)*2,0.0f);self.underruns.fetch_add(missing,std::memory_order_relaxed);
        }
        self.submitted.fetch_add(uint32_t(count),std::memory_order_relaxed);
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
    static void failed(AAudioStream*,void* user,aaudio_result_t value){
        auto& self=*static_cast<Impl*>(user);self.deviceError.store(value,std::memory_order_release);self.terminate.store(true,std::memory_order_release);self.running.store(false,std::memory_order_release);
    }
#endif
    void produce(){
        std::array<float,Chunk*2> data{};
        while(!terminate.load(std::memory_order_acquire)){
            const uint64_t at=write.load(std::memory_order_relaxed),queued=at-read.load(std::memory_order_acquire);
            if(queued+Chunk>Target){std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;}
            std::string error;
            bool ok=false;
            try{ok=render(data.data(),Chunk,error);}catch(...){error="Audio renderer threw";}
            if(ok&&!std::all_of(data.begin(),data.end(),[](float value){return std::isfinite(value);})){ok=false;error="Audio renderer returned nonfinite PCM";}
            if(!ok){std::lock_guard lock(errorMutex);renderError=error.empty()?"Audio renderer failed":error;terminate.store(true,std::memory_order_release);running.store(false,std::memory_order_release);break;}
            for(uint32_t frame=0;frame<Chunk;++frame){const size_t offset=size_t((at+frame)%Capacity)*2;ring[offset]=data[frame*2];ring[offset+1]=data[frame*2+1];}
            write.store(at+Chunk,std::memory_order_release);
        }
    }
};
AudioOutput::AudioOutput():impl_(std::make_unique<Impl>()){}
AudioOutput::~AudioOutput(){stop();}
bool AudioOutput::start(Render render,std::string& error){
    error.clear();stop();
#ifndef __ANDROID__
    (void)render;error="AAudio output requires an Android build; use offline renderer probes on the host";return false;
#else
    if(!render){error="Audio output requires a real renderer";return false;}
    impl_=std::make_unique<Impl>();impl_->render=std::move(render);
    AAudioStreamBuilder* builder=nullptr;aaudio_result_t result=AAudio_createStreamBuilder(&builder);
    if(result!=AAUDIO_OK||!builder){error="AAudio stream builder failed: "+std::to_string(result);return false;}
    AAudioStreamBuilder_setDirection(builder,AAUDIO_DIRECTION_OUTPUT);AAudioStreamBuilder_setChannelCount(builder,2);AAudioStreamBuilder_setSampleRate(builder,48000);AAudioStreamBuilder_setFormat(builder,AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setSharingMode(builder,AAUDIO_SHARING_MODE_SHARED);AAudioStreamBuilder_setPerformanceMode(builder,AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(builder,Impl::callback,impl_.get());AAudioStreamBuilder_setErrorCallback(builder,Impl::failed,impl_.get());
    result=AAudioStreamBuilder_openStream(builder,&impl_->stream);AAudioStreamBuilder_delete(builder);
    if(result!=AAUDIO_OK||!impl_->stream){error="AAudio stream open failed: "+std::to_string(result);stop();return false;}
    impl_->deviceRate=AAudioStream_getSampleRate(impl_->stream);impl_->burst=AAudioStream_getFramesPerBurst(impl_->stream);
    if(impl_->deviceRate!=48000||AAudioStream_getChannelCount(impl_->stream)!=2||AAudioStream_getFormat(impl_->stream)!=AAUDIO_FORMAT_PCM_FLOAT){error="AAudio did not provide the requested48k stereo float output";stop();return false;}
    impl_->running.store(true,std::memory_order_release);
    try{impl_->worker=std::thread([self=impl_.get()]{self->produce();});}
    catch(...){error="Could not start audio producer thread";stop();return false;}
    result=AAudioStream_requestStart(impl_->stream);
    if(result!=AAUDIO_OK){error="AAudio start failed: "+std::to_string(result);stop();return false;}
    if(impl_->terminate.load(std::memory_order_acquire)){error=this->error();if(error.empty())error="Audio output failed during startup";stop();return false;}
    return true;
#endif
}
void AudioOutput::stop(){
    impl_->terminate.store(true,std::memory_order_release);impl_->running.store(false,std::memory_order_release);
    if(impl_->worker.joinable())impl_->worker.join();
#ifdef __ANDROID__
    if(impl_->stream){AAudioStream_requestStop(impl_->stream);AAudioStream_close(impl_->stream);impl_->stream=nullptr;}
#endif
}
AudioOutput::Stats AudioOutput::stats()const{return {impl_->submitted.load(std::memory_order_relaxed),impl_->underruns.load(std::memory_order_relaxed),impl_->deviceRate,impl_->burst,impl_->running.load(std::memory_order_acquire)};}
std::string AudioOutput::error()const{
    const int device=impl_->deviceError.load(std::memory_order_acquire);if(device)return "AAudio device error: "+std::to_string(device);
    std::lock_guard lock(impl_->errorMutex);return impl_->renderError;
}
}
