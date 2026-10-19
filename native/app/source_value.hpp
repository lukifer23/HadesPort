#pragma once
#include <map>
#include <string>
#include <variant>
#include <vector>
struct lua_State;

namespace hades {
// Original source data and proven defaults. Missing properties remain missing.
struct SourceValue {
    using Array=std::vector<SourceValue>;
    using Object=std::map<std::string,SourceValue>;
    std::variant<std::monostate,bool,double,std::string,Array,Object> value;
    const SourceValue* get(const std::string&) const;
    const SourceValue* path(const std::string&) const;
    double number() const;
    bool boolean() const;
    const std::string& text() const;
    const Object& object() const;
    const Array& array() const;
};
bool readSourceValue(lua_State*,int index,SourceValue&,std::string& error);
void pushSourceValue(lua_State*,const SourceValue&);
}
