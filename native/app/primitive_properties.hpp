#pragma once
#include "source_value.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace hades {
// Native reflection types covered by the recovered scalar subset. The caller
// must resolve a real reflected member first; this module invents no key aliases.
using PropertyPrimitive=std::variant<bool,int32_t,float,std::string>;
using PropertyInput=std::variant<std::monostate,bool,int32_t,int64_t,float,double,std::string>;
enum class PropertyChange:uint8_t { Absolute=0,Multiply=1,MultiplyBase=2,Add=3,ReplaceIfNull=4 };
enum class PropertyMutation { Applied,RejectedType,UnsupportedChange,InvalidValue };
PropertyChange parsePropertyChange(const std::string&);
// Original LuaTableExtensions::GetValue boxes all Lua numbers as float32.
bool propertyInputFromSource(const SourceValue&,PropertyInput&,std::string& error);
// Leaves output untouched on failure. MultiplyBase requires the ADDITIVE delta
// returned by the actual native base-data lookup; an arbitrary catalog value is
// not equivalent. An empty optional means that lookup has not been implemented.
PropertyMutation mutatePrimitiveProperty(const PropertyPrimitive& current,
    const PropertyInput& input,PropertyChange,PropertyPrimitive& output,
    std::string& error,const std::optional<PropertyInput>& multipliedBaseDelta=std::nullopt);
SourceValue propertyPrimitiveToSource(const PropertyPrimitive&);
}
