#include "primitive_properties.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace hades;
int main(){
    PropertyPrimitive out=std::string("unchanged");std::string error;
    assert(parsePropertyChange("mUlTiPlY")==PropertyChange::Multiply);
    assert(parsePropertyChange("unsupported")==PropertyChange::Absolute);
    assert(mutatePrimitiveProperty(true,2.0f,PropertyChange::Absolute,out,error)==PropertyMutation::RejectedType);
    assert(std::get<std::string>(out)=="unchanged");
    assert(mutatePrimitiveProperty(false,std::string("TRUE"),PropertyChange::Add,out,error)==PropertyMutation::Applied&&std::get<bool>(out));
    assert(mutatePrimitiveProperty(true,std::string("unknown"),PropertyChange::Absolute,out,error)==PropertyMutation::Applied&&!std::get<bool>(out));
    assert(mutatePrimitiveProperty(3.0f,std::string("2"),PropertyChange::Absolute,out,error)==PropertyMutation::RejectedType);
    assert(mutatePrimitiveProperty(3.0f,int64_t(2),PropertyChange::Absolute,out,error)==PropertyMutation::RejectedType);
    assert(mutatePrimitiveProperty(3.0f,std::string("2"),PropertyChange::Multiply,out,error)==PropertyMutation::Applied&&std::get<float>(out)==0);
    assert(mutatePrimitiveProperty(int32_t(-3),0.5f,PropertyChange::Multiply,out,error)==PropertyMutation::Applied&&std::get<int32_t>(out)==-2);
    assert(mutatePrimitiveProperty(INT32_MAX,1.0f,PropertyChange::Add,out,error)==PropertyMutation::Applied&&std::get<int32_t>(out)==INT32_MIN);
    assert(mutatePrimitiveProperty(3.0f,2.0f,PropertyChange::ReplaceIfNull,out,error)==PropertyMutation::UnsupportedChange);
    assert(mutatePrimitiveProperty(3.0f,2.0f,PropertyChange::MultiplyBase,out,error)==PropertyMutation::UnsupportedChange);
    assert(mutatePrimitiveProperty(3.0f,2.0f,PropertyChange::MultiplyBase,out,error,PropertyInput{5.0f})==PropertyMutation::Applied&&std::get<float>(out)==8);
    SourceValue source;source.value=16777217.0;PropertyInput input;
    assert(propertyInputFromSource(source,input,error)&&std::get<float>(input)==16777216.0f);
    source.value=std::numeric_limits<double>::infinity();assert(!propertyInputFromSource(source,input,error));
    source.value=SourceValue::Object{};assert(!propertyInputFromSource(source,input,error));
    assert(mutatePrimitiveProperty(std::string("old"),std::monostate{},PropertyChange::ReplaceIfNull,out,error)==PropertyMutation::Applied&&std::get<std::string>(out).empty());
    std::cout<<"Native scalar reflection subset passed; synthetic portable observer only\n";
}
