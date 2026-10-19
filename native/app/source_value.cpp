#include "source_value.hpp"
#include <cmath>
#include <set>
#include <string_view>
#include <stdexcept>
extern "C" {
#include <lua.h>
}
namespace hades {
const SourceValue* SourceValue::get(const std::string& key)const{const auto* o=std::get_if<Object>(&value);if(!o)return nullptr;const auto i=o->find(key);return i==o->end()?nullptr:&i->second;}
const SourceValue* SourceValue::path(const std::string& key)const{const SourceValue* at=this;size_t start=0;while(at){const auto end=key.find('.',start);at=at->get(key.substr(start,end-start));if(end==std::string::npos)return at;start=end+1;}return nullptr;}
double SourceValue::number()const{if(const auto* v=std::get_if<double>(&value))return *v;throw std::runtime_error("Original data property requires number");}
bool SourceValue::boolean()const{if(const auto* v=std::get_if<bool>(&value))return *v;throw std::runtime_error("Original data property requires boolean");}
const std::string& SourceValue::text()const{if(const auto* v=std::get_if<std::string>(&value))return *v;throw std::runtime_error("Original data property requires string");}
const SourceValue::Object& SourceValue::object()const{if(const auto* v=std::get_if<Object>(&value))return *v;throw std::runtime_error("Original data property requires dictionary");}
const SourceValue::Array& SourceValue::array()const{if(const auto* v=std::get_if<Array>(&value))return *v;if(const auto* v=std::get_if<Object>(&value);v&&v->empty()){static const Array empty;return empty;}throw std::runtime_error("Original data property requires array");}
namespace {
SourceValue read(lua_State* L,int at,size_t depth,size_t& count,std::set<const void*>& visiting){
    if(depth>64||++count>2000000)throw std::runtime_error("Original data tree exceeds bounds");
    at=lua_absindex(L,at);SourceValue out;
    switch(lua_type(L,at)){
    case LUA_TNIL:return out;
    case LUA_TBOOLEAN:out.value=lua_toboolean(L,at)!=0;return out;
    case LUA_TNUMBER:{const auto n=lua_tonumber(L,at);if(!std::isfinite(n))throw std::runtime_error("Nonfinite original data value");out.value=double(n);return out;}
    case LUA_TSTRING:{size_t n=0;const char* s=lua_tolstring(L,at,&n);if(n>1048576||std::string_view(s,n).find('\0')!=std::string_view::npos)throw std::runtime_error("Original data string outside bounds");out.value=std::string(s,n);return out;}
    case LUA_TTABLE:{
        const auto* identity=lua_topointer(L,at);if(!visiting.insert(identity).second)throw std::runtime_error("Cyclic original data table");
        const size_t length=lua_rawlen(L,at);if(length>100000)throw std::runtime_error("Original data list outside bounds");
        if(length){SourceValue::Array a;a.reserve(length);size_t keys=0;lua_pushnil(L);while(lua_next(L,at)){++keys;const auto k=lua_tonumber(L,-2);if(lua_type(L,-2)!=LUA_TNUMBER||k<1||k>length||k!=std::trunc(k))throw std::runtime_error("Mixed original dictionary/list keys");lua_pop(L,1);}if(keys!=length)throw std::runtime_error("Sparse original data list");for(size_t i=1;i<=length;++i){lua_rawgeti(L,at,int(i));a.push_back(read(L,-1,depth+1,count,visiting));lua_pop(L,1);}out.value=std::move(a);}
        else {SourceValue::Object o;lua_pushnil(L);while(lua_next(L,at)){if(lua_type(L,-2)!=LUA_TSTRING)throw std::runtime_error("Original dictionary key requires string");size_t n=0;const char* s=lua_tolstring(L,-2,&n);if(n>65536)throw std::runtime_error("Original data key outside bounds");o.emplace(std::string(s,n),read(L,-1,depth+1,count,visiting));lua_pop(L,1);}out.value=std::move(o);}
        visiting.erase(identity);return out;
    }
    default:throw std::runtime_error("Non-data value in original source catalog");
    }
}
}
bool readSourceValue(lua_State* L,int at,SourceValue& output,std::string& error){const int top=lua_gettop(L);try{size_t count=0;std::set<const void*> visited;auto parsed=read(L,at,0,count,visited);output=std::move(parsed);lua_settop(L,top);error.clear();return true;}catch(const std::exception& e){lua_settop(L,top);error=e.what();return false;}}
void pushSourceValue(lua_State* L,const SourceValue& data){
    if(std::holds_alternative<std::monostate>(data.value))lua_pushnil(L);
    else if(const auto* b=std::get_if<bool>(&data.value))lua_pushboolean(L,*b);
    else if(const auto* n=std::get_if<double>(&data.value))lua_pushnumber(L,*n);
    else if(const auto* s=std::get_if<std::string>(&data.value))lua_pushlstring(L,s->data(),s->size());
    else if(const auto* a=std::get_if<SourceValue::Array>(&data.value)){lua_createtable(L,int(a->size()),0);for(size_t i=0;i<a->size();++i){pushSourceValue(L,(*a)[i]);lua_rawseti(L,-2,int(i+1));}}
    else {const auto& o=std::get<SourceValue::Object>(data.value);lua_createtable(L,0,int(o.size()));for(const auto& [k,v]:o){pushSourceValue(L,v);lua_setfield(L,-2,k.c_str());}}
}
}
