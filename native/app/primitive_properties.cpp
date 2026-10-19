#include "primitive_properties.hpp"
#include <cmath>
#include <limits>
#include <type_traits>

namespace hades {
namespace {
bool equalAscii(const std::string& a,const char* b){size_t i=0;for(;i<a.size()&&b[i];++i){unsigned char c=a[i],d=b[i];if(c>='A'&&c<='Z')c+=32;if(d>='A'&&d<='Z')d+=32;if(c!=d)return false;}return i==a.size()&&b[i]==0;}
bool numeric(const PropertyInput& v){return v.index()>=2&&v.index()<=5;}
double number(const PropertyInput& v){return std::visit([](const auto& n)->double{using T=std::decay_t<decltype(n)>;if constexpr(std::is_arithmetic_v<T>&&!std::is_same_v<T,bool>)return double(n);else return 0;},v);}
bool floatValue(const PropertyInput& v,float& n){n=float(number(v));return std::isfinite(n);}
// The original x86 CVTT* out-of-range result is INT_MIN. State it explicitly
// instead of relying on undefined C++ floating-point-to-integer conversion.
int32_t truncateInt(double n){if(!std::isfinite(n)||n>=2147483648.0||n< -2147483648.0)return INT32_MIN;return int32_t(n);}
int32_t intValue(const PropertyInput& v){if(const auto* n=std::get_if<int64_t>(&v))return int32_t(uint32_t(uint64_t(*n)));return truncateInt(number(v));}
PropertyMutation fail(PropertyMutation s,std::string& e,const char* message){e=message;return s;}
}
PropertyChange parsePropertyChange(const std::string& s){if(equalAscii(s,"Multiply"))return PropertyChange::Multiply;if(equalAscii(s,"MultiplyBase"))return PropertyChange::MultiplyBase;if(equalAscii(s,"Add"))return PropertyChange::Add;if(equalAscii(s,"ReplaceIfNull"))return PropertyChange::ReplaceIfNull;return PropertyChange::Absolute;}
bool propertyInputFromSource(const SourceValue& v,PropertyInput& out,std::string& error){
    if(std::holds_alternative<std::monostate>(v.value))out=std::monostate{};
    else if(const auto* x=std::get_if<bool>(&v.value))out=*x;
    else if(const auto* x=std::get_if<double>(&v.value)){const float n=float(*x);if(!std::isfinite(n)){error="Nonfinite native property input is outside the portable scalar subset";return false;}out=n;}
    else if(const auto* x=std::get_if<std::string>(&v.value)){if(x->find('\0')!=std::string::npos){error="Embedded NUL native property input is outside the scalar subset";return false;}out=*x;}
    else {error="Native table/function property value needs its own typed handler";return false;}
    error.clear();return true;
}
PropertyMutation mutatePrimitiveProperty(const PropertyPrimitive& current,const PropertyInput& input,PropertyChange change,PropertyPrimitive& out,std::string& error,const std::optional<PropertyInput>& delta){
    if(change==PropertyChange::MultiplyBase){if(!delta)return fail(PropertyMutation::UnsupportedChange,error,"MultiplyBase requires the recovered native additive base-data lookup result");return mutatePrimitiveProperty(current,*delta,PropertyChange::Add,out,error);}
    if(std::holds_alternative<bool>(current)){
        if(const auto* b=std::get_if<bool>(&input))out=*b;
        else if(const auto* s=std::get_if<std::string>(&input))out=equalAscii(*s,"true");
        else return fail(PropertyMutation::RejectedType,error,"Native reflected bool accepts bool or string only");
    }else if(const auto* n=std::get_if<float>(&current)){
        float v=0;
        if(change==PropertyChange::Absolute){if(std::holds_alternative<int64_t>(input)||(!numeric(input)&&!std::holds_alternative<std::monostate>(input)))return fail(PropertyMutation::RejectedType,error,"Native reflected float accepts float, double, int32 or empty any only");if(!floatValue(input,v))return fail(PropertyMutation::InvalidValue,error,"Nonfinite native float input is outside the portable subset");}
        else if(change==PropertyChange::Multiply||change==PropertyChange::Add){float q=0;if(!floatValue(input,q))return fail(PropertyMutation::InvalidValue,error,"Nonfinite native arithmetic input is outside the portable subset");v=change==PropertyChange::Multiply?*n*q:*n+q;}
        else return fail(PropertyMutation::UnsupportedChange,error,"Native numeric reflection does not apply ReplaceIfNull");
        if(!std::isfinite(v))return fail(PropertyMutation::InvalidValue,error,"Nonfinite native float result is outside the portable subset");out=v;
    }else if(const auto* n=std::get_if<int32_t>(&current)){
        int32_t v=0;
        if(change==PropertyChange::Absolute){if(!numeric(input)&&!std::holds_alternative<std::monostate>(input))return fail(PropertyMutation::RejectedType,error,"Native reflected int rejects nonnumeric typed input");v=intValue(input);}
        else if(change==PropertyChange::Multiply){float q=0;if(!floatValue(input,q))return fail(PropertyMutation::InvalidValue,error,"Nonfinite native arithmetic input is outside the portable subset");v=truncateInt(std::round(float(*n)*q));}
        else if(change==PropertyChange::Add)v=int32_t(uint32_t(*n)+uint32_t(intValue(input)));
        else return fail(PropertyMutation::UnsupportedChange,error,"Native numeric reflection does not apply ReplaceIfNull");
        out=v;
    }else{
        if(std::holds_alternative<std::monostate>(input))out=std::string{};
        else if(const auto* s=std::get_if<std::string>(&input))out=*s;
        else return fail(PropertyMutation::RejectedType,error,"Native reflected string accepts string or empty any only");
    }
    error.clear();return PropertyMutation::Applied;
}
SourceValue propertyPrimitiveToSource(const PropertyPrimitive& v){SourceValue out;std::visit([&](const auto& x){using T=std::decay_t<decltype(x)>;if constexpr(std::is_same_v<T,int32_t>||std::is_same_v<T,float>)out.value=double(x);else out.value=x;},v);return out;}
}
