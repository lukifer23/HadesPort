#pragma once
// Android input adaptation: track physical owners before merging logical
// controls, so one key, hat, or device cannot release another owner's input.
#include "input_bindings.hpp"
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace hades::input {
class HardwareInput {
public:
    // kind distinguishes key events from axis-generated button events.
    using Source = std::tuple<int32_t,int32_t,int32_t>; // device, kind, code
private:
    std::map<Source,std::set<std::string>> sources_;
    std::set<std::string> held_;
    std::map<Source,std::set<std::string>> pressed_;
    void rebuild(){
        held_.clear();
        for(const auto& [source,names]:sources_)held_.insert(names.begin(),names.end());
    }
public:
    bool down(Source source)const{return sources_.contains(source);}
    const std::set<std::string>& held()const{return held_;}
    std::set<std::string> takePressed(){
        std::set<std::string> result;
        for(const auto& [source,names]:pressed_)result.insert(names.begin(),names.end());
        pressed_.clear();return result;
    }
    std::vector<std::string> update(Source source,const ControlList& controls,bool down){
        if(!down){sources_.erase(source);rebuild();return {};}
        if(sources_.contains(source))return {};
        std::vector<std::string> edges;
        auto& names=sources_[source];
        for(size_t i=0;i<controls.count;++i){
            const std::string name(controls.names[i]);names.insert(name);
            if(held_.insert(name).second){pressed_[source].insert(name);edges.push_back(name);}
        }
        return edges;
    }
    std::set<int32_t> devices()const{
        std::set<int32_t> result;
        for(const auto& [source,names]:sources_)result.insert(std::get<0>(source));
        for(const auto& [source,names]:pressed_)result.insert(std::get<0>(source));
        return result;
    }
    void releaseDevice(int32_t device){
        // Discard taps from the disconnected device, preserving any controls
        // still held by other devices. Releasing all input also discards taps.
        for(auto it=sources_.begin();it!=sources_.end();){
            if(std::get<0>(it->first)==device)it=sources_.erase(it);
            else ++it;
        }
        for(auto it=pressed_.begin();it!=pressed_.end();){
            if(std::get<0>(it->first)==device)it=pressed_.erase(it);
            else ++it;
        }
        rebuild();
    }
    void clear(){sources_.clear();held_.clear();pressed_.clear();}
};
}
