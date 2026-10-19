#include "text_runtime.hpp"
#include "object_runtime.hpp"
#include "platform_log.hpp"
#include "control_symbols.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <map>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <set>
#include <sstream>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace {
// GUIComponentTextBox reconstruction. Contract and provenance:
// native/analysis/text-box-contract.json (.local/reports/textbox-v2/*).
//  - Defaults: GUIComponentTextBox ctor 0x173ae0, Clear 0x1740d0,
//    ComponentData::Defaults 0xa2a30 (textbox data at component+0xa8):
//    TextSymbolScale 1, FontSize 0, Width 0, Justification/Vertical CENTER,
//    LocationZWithOwner true, ScaleWithOwner(+0xc9) false, OutlineColor
//    -1 sentinel, ShadowOffset 0,0, ShadowColor 0,0,0,0, fade/scale/color
//    speeds 4/s, FadeTarget 1.
//  - CreateTextBox 0x2701e0: Scale clamp[0,1], RawText>Text(ShowDisplayName),
//    Color, LuaKey/LuaValue (SetLuaParam), FadeOpacity default 0, FadeTarget 1,
//    ScaleModifier 1, ScaleTarget 1. AutoSetDataProperties (default false)
//    true: reflection of every argument key, explicit block skipped. false:
//    only Font, FontSize(int), TextSymbolScale, Width, Height, OffsetX/Y(int),
//    Justification, VerticalJustification, LocationZWithOwner,
//    OutlineThickness, ShadowBlur(50), language modifiers, ScaleDuration,
//    VariableAutoFormat. Both: LineSpacing*, CharacterFadeColor,
//    OutlineColor(RGB), ShadowColor(RGBA), ShadowOffset, DataProperties, Hide.
//    Other keys (ShadowRed, ShadowAlpha, ShadowOffsetY...) are not read.
//  - TextComponent::ModifyTextBox 0x2be2d0: AffectText/ExcludeText, Text,
//    Color (immediate), FadeTarget/ScaleTarget (>=0), *Duration -> speed 1/d,
//    ColorTarget, VariableAutoFormat, AutoSetDataProperties default false,
//    Font, LuaKey/LuaValue. Other keys are not read.
//  - GUIComponent::Update 0x1689b0 / textbox Update 0x175300: FadeOpacity and
//    ScaleModifier step toward targets by speed*dt; StepTowardColor 0x1ead60
//    steps each byte channel in [0,1] by speed*dt, truncating back to bytes.
//  - GetLines 0x17b150 sets each LuaKey global to its LuaValue, then Parse
//    0x181a00 / ParseMarkup 0x17f1c0 / ParseLua 0x17cf30 substitute markup.
constexpr const char* registryKey = "hades.textFormatRuntime";
constexpr const char* ownerMetatable = "hades.textFormatRuntime.owner";
using Color = std::array<float,4>;
struct TextRunStyle {
    std::string font;float size=0,textSymbolScale=1;
    Color color{1,1,1,1},shadowColor{0,0,0,0},outlineColor{-1,-1,-1,-1};
    float shadowX=0,shadowY=0,shadowBlur=50,outlineThickness=0;
    bool emphasized=false,blockEmphasized=false;
};
struct TextBody {
    uint64_t ownerLifetime=0;bool destroyed=false,autoSet=false,hide=false;
    std::string textKey,rawText;bool raw=false,useDescription=false;
    std::string font;int32_t fontSize=0;float textSymbolScale=1,width=0,height=0,offsetX=0,offsetY=0;
    hades::TextJustification horizontal=hades::TextJustification::Center,vertical=hades::TextJustification::Center;
    float outlineThickness=0,shadowBlur=50,shadowX=0,shadowY=0;Color outlineColor{-1,-1,-1,-1},shadowColor{0,0,0,0};
    std::string variableAutoFormat;bool opacityWithOwner=false,locationZWithOwner=true,graft=false,emphasized=false,blockEmphasized=false;
    float scale=1,scaleModifier=1,scaleTarget=1,scaleSpeed=4,fadeOpacity=0,fadeTarget=1,fadeSpeed=4,colorSpeed=4;
    Color color{1,1,1,1},colorTarget{1,1,1,1};
    std::map<std::string,int> luaParams; // registry refs, GUIComponentTextBox+0xd08
    std::map<std::string,std::string> controlLabels;
    bool dirty=true;std::string resolved;std::vector<hades::TextPiece> pieces;
    std::vector<TextRunStyle> styles;
};
struct TextState { std::vector<TextFormatSnapshot> formats;std::map<uint64_t,TextBody> bodies;std::map<int32_t,uint64_t> components;std::shared_ptr<const hades::TextFontBank> fonts;TextControlLabelProvider controlLabel; uint64_t nextSequence = 1; };
TextState* lookup(lua_State* state) {
    lua_getfield(state,LUA_REGISTRYINDEX,registryKey);
    auto* result=static_cast<TextState*>(lua_touserdata(state,-1)); lua_pop(state,1); return result;
}
std::string stringAt(lua_State* state,int index) {
    if(lua_type(state,index)!=LUA_TSTRING) throw std::runtime_error("Text format requires string value");
    size_t size=0; const char* bytes=lua_tolstring(state,index,&size);
    if(size>65536 || std::memchr(bytes,0,size)) throw std::runtime_error("Text format string outside supported bounds");
    return std::string(bytes,size);
}
double numberAt(lua_State* state,int index) {
    if(!lua_isnumber(state,index)) throw std::runtime_error("Text format requires numeric value");
    const double value=lua_tonumber(state,index);
    if(!std::isfinite(value) || std::abs(value)>std::numeric_limits<float>::max()) throw std::runtime_error("Text format numeric value outside finite float range");
    return value;
}
const std::set<std::string> strings={"Name","Font","Justification","VerticalJustification"};
const std::set<std::string> booleans={"Graft","UseEmphasizedFont","BlockEmphasizedFont","LocationZWithOwner"};
// These are actual original UIData TextFormats fields, also present in native
// ComponentDataDef reflection / CreateTextBox explicit property reads.
const std::set<std::string> numbers={
    "FontSize","TextSymbolScale","Width","OffsetX","OffsetY","OutlineThickness",
    "ShadowAlpha","ShadowBlur","ShadowRed","ShadowGreen","ShadowBlue","ShadowOffsetX","ShadowOffsetY","LineSpacingBottom","Height","Scale","FadeOpacity","FadeTarget","FadeDuration","CharacterFadeTime","CharacterFadeInterval",
    "LangCnScaleModifier","LangKoScaleModifier","LangJaScaleModifier",
    "LangFrScaleModifier","LangItScaleModifier","LangDeScaleModifier",
    "LangEsScaleModifier","LangPlScaleModifier","LangPtBrScaleModifier",
    "LangRuScaleModifier","NonEnglishScaleModifier"};
TextFormatProperty propertyAt(lua_State* state,const std::string& name,int at) {
    TextFormatProperty result;
    if(strings.contains(name)) { result.kind=TextFormatProperty::Kind::String; result.string=stringAt(state,at); }
    else if(booleans.contains(name)) {
        if(!lua_isboolean(state,at)) throw std::runtime_error(name+" requires boolean");
        result.kind=TextFormatProperty::Kind::Boolean; result.boolean=lua_toboolean(state,at);
    } else if(numbers.contains(name)) { result.number=numberAt(state,at); }
    else if(name=="Color" || name=="ShadowColor" || name=="OutlineColor" || name=="ShadowOffset") {
        if(!lua_istable(state,at)) throw std::runtime_error(name+" requires numeric array");
        const size_t expected=name=="ShadowOffset"?2:4;
        if(lua_rawlen(state,at)!=expected) throw std::runtime_error(name+" has unsupported array length");
        result.kind=TextFormatProperty::Kind::NumericArray;
        const int absolute=lua_absindex(state,at);
        for(size_t i=1;i<=expected;++i) { lua_rawgeti(state,absolute,static_cast<int>(i)); result.array.push_back(numberAt(state,-1)); lua_pop(state,1); }
    } else throw std::runtime_error("CreateTextBox unsupported format property: "+name);
    return result;
}

// LuaTableExtensions::String/Float/Int/Bool: absent or wrong type -> default.
std::string argText(lua_State*L,int t,const char*key,const std::string&fallback=""){lua_getfield(L,t,key);std::string out=fallback;if(lua_type(L,-1)==LUA_TSTRING||lua_type(L,-1)==LUA_TNUMBER){lua_pushvalue(L,-1);size_t n=0;const char*b=lua_tolstring(L,-1,&n);if(n>65536||std::memchr(b,0,n)){lua_pop(L,2);throw std::runtime_error(std::string("Text argument bound: ")+key);}out.assign(b,n);lua_pop(L,1);}lua_pop(L,1);return out;}
bool hasArg(lua_State*L,int t,const char*key){lua_getfield(L,t,key);const bool present=!lua_isnil(L,-1);lua_pop(L,1);return present;}
float argFloat(lua_State*L,int t,const char*key,float fallback){lua_getfield(L,t,key);float out=fallback;if(lua_isnumber(L,-1)){const double v=lua_tonumber(L,-1);if(!std::isfinite(v)||std::abs(v)>std::numeric_limits<float>::max()){lua_pop(L,1);throw std::runtime_error(std::string("Text numeric argument bound: ")+key);}out=float(v);}lua_pop(L,1);return out;}
int32_t argInt(lua_State*L,int t,const char*key,int32_t fallback){lua_getfield(L,t,key);int32_t out=fallback;if(lua_isnumber(L,-1)){const double v=std::trunc(lua_tonumber(L,-1));if(!(v>=INT32_MIN&&v<=INT32_MAX)){lua_pop(L,1);throw std::runtime_error(std::string("Text integer argument bound: ")+key);}out=int32_t(v);}lua_pop(L,1);return out;}
bool argBool(lua_State*L,int t,const char*key,bool fallback){lua_getfield(L,t,key);const bool out=lua_isboolean(L,-1)?lua_toboolean(L,-1):fallback;lua_pop(L,1);return out;}
// LuaTableExtensions::GetColor / ScriptArgs::ColorByName over original arrays.
bool argColor(lua_State*L,int t,const char*key,Color&out){
 lua_getfield(L,t,key);if(lua_isnil(L,-1)){lua_pop(L,1);return false;}
 if(!lua_istable(L,-1)){lua_pop(L,1);throw std::runtime_error(std::string("Original named/packed text color unreconstructed: ")+key);}
 // LuaTableExtensions.GetColor2497bf..2498f6 reads RGB independently with
 // default0, then optional alpha with default1. A three-channel source table
 // is valid; RGB selects normalized/byte conversion independently of alpha.
 const int a=lua_absindex(L,-1);std::vector<double>c{0,0,0,1};
 for(int i=1;i<=4;++i){lua_rawgeti(L,a,i);if(lua_isnumber(L,-1))c[size_t(i-1)]=lua_tonumber(L,-1);lua_pop(L,1);}
 lua_pop(L,1);out=hades::textSourceColor(c);return true;
}
hades::TextJustification horizontalJustification(const std::string&v){std::string u=v;std::transform(u.begin(),u.end(),u.begin(),[](unsigned char c){return char(std::toupper(c));});if(u=="LEFT")return hades::TextJustification::Start;if(u=="RIGHT")return hades::TextJustification::End;if(u=="UNKNOWN")throw std::runtime_error("Original UNKNOWN text justification unreconstructed");return hades::TextJustification::Center;}
hades::TextJustification verticalJustification(const std::string&v){std::string u=v;std::transform(u.begin(),u.end(),u.begin(),[](unsigned char c){return char(std::toupper(c));});if(u=="TOP")return hades::TextJustification::Start;if(u=="BOTTOM")return hades::TextJustification::End;return hades::TextJustification::Center;}
// SetLuaParam 0x175c80: key -> any (LuaValue), dirty.
void setLuaParam(lua_State*L,TextBody&body,int t){const std::string key=argText(L,t,"LuaKey");if(key.empty())return;lua_getfield(L,t,"LuaValue");const int ref=luaL_ref(L,LUA_REGISTRYINDEX);if(auto i=body.luaParams.find(key);i!=body.luaParams.end()){luaL_unref(L,LUA_REGISTRYINDEX,i->second);i->second=ref;}else{if(body.luaParams.size()>=64){luaL_unref(L,LUA_REGISTRYINDEX,ref);throw std::runtime_error("Text Lua parameter bound");}body.luaParams.emplace(key,ref);}body.dirty=true;}
void releaseBody(lua_State*L,TextBody&body){for(auto&[k,ref]:body.luaParams)luaL_unref(L,LUA_REGISTRYINDEX,ref);body.luaParams.clear();}
// ParseLua: global then '.'-separated fields (numeric -> integer index);
// ReflectionHelper::ConvertAnyToString: integral double "%d", else "%g".
std::string luaMarkupValue(lua_State*L,const std::string&path){
    if(path.empty()||path.find_first_of(":(){}[]<>,;#$%^&*+?@!\\' \t")!=std::string::npos)throw std::runtime_error("Original Lua markup form unreconstructed: "+path);
    const int top=lua_gettop(L);size_t at=0;bool first=true;
    while(at<=path.size()){const size_t dot=std::min(path.find('.',at),path.size());const std::string part=path.substr(at,dot-at);if(part.empty()){lua_settop(L,top);throw std::runtime_error("Original Lua markup empty field: "+path);}
        if(first){lua_getglobal(L,part.c_str());first=false;}
        else{if(!lua_istable(L,-1)){lua_settop(L,top);throw std::runtime_error("Original Lua text value unavailable: "+path);}char*end=nullptr;const float index=std::strtof(part.c_str(),&end);if(*end==0)lua_rawgeti(L,-1,int(index));else lua_getfield(L,-1,part.c_str());lua_remove(L,-2);}
        at=dot+1;}
    std::string out;
    if(lua_type(L,-1)==LUA_TSTRING){size_t n=0;const char*b=lua_tolstring(L,-1,&n);out.assign(b,n);}
    else if(lua_type(L,-1)==LUA_TNUMBER){const double v=lua_tonumber(L,-1);char b[64];if(std::abs(v-double(int32_t(v)))>0)std::snprintf(b,sizeof b,"%g",v);else std::snprintf(b,sizeof b,"%d",int32_t(v));out=b;}
    else if(lua_isboolean(L,-1))out=lua_toboolean(L,-1)?"true":"false";
    else{lua_settop(L,top);throw std::runtime_error("Original Lua text value unavailable: "+path);}
    lua_settop(L,top);return out;}
TextRunStyle runStyle(const TextBody&body){TextRunStyle out;out.font=body.font;out.size=float(body.fontSize);out.textSymbolScale=body.textSymbolScale;out.color=body.color;out.shadowColor=body.shadowColor;out.outlineColor=body.outlineColor;out.shadowX=body.shadowX;out.shadowY=body.shadowY;out.shadowBlur=body.shadowBlur;out.outlineThickness=body.outlineThickness;out.emphasized=body.emphasized;out.blockEmphasized=body.blockEmphasized;return out;}
void appendText(std::vector<hades::TextPiece>&pieces,const std::string&text,uint32_t style){if(text.empty())return;if(pieces.empty()||pieces.back().symbol||pieces.back().style!=style)pieces.push_back({false,text,style});else pieces.back().text+=text;}
// GetFormatter178980 resolves the actual Lua FormatContainerIds owner, then
// selects the live TextComponent widget by ComponentData.Name. An unknown
// format leaves the current line unchanged in ParseFormatting17c690.
const TextBody* findFormatter(lua_State*L,const std::string&name){auto*registry=lookup(L);if(!registry)return nullptr;const int top=lua_gettop(L);lua_getglobal(L,"FormatContainerIds");if(!lua_istable(L,-1)){lua_settop(L,top);return nullptr;}lua_getfield(L,-1,name.c_str());const double id=lua_isnumber(L,-1)?lua_tonumber(L,-1):0;lua_settop(L,top);if(!std::isfinite(id)||id<=0||id>INT32_MAX)return nullptr;const auto*owner=findObjectSnapshot(L,int32_t(id));if(!owner||!owner->active||!owner->alive)return nullptr;for(const auto&format:registry->formats){if(format.ownerId!=int32_t(id)||format.name!=name)continue;const auto&body=registry->bodies.at(format.sequence);if(!body.destroyed&&body.ownerLifetime==owner->entityHandle)return &body;}return nullptr;}
void selectFormat(lua_State*L,TextBody&body,const std::string&name,uint32_t&current,std::vector<uint32_t>&stack){
    if(name=="PreviousFormat"){current=stack.empty()?0:stack.back();if(!stack.empty())stack.pop_back();return;}
    const auto*formatter=findFormatter(L,name);if(!formatter)return;
    TextRunStyle style=runStyle(*formatter);
    if(formatter->graft){
        // GraftFrom1787e0 copies the current ComponentData, selectively replaces
        // font/nonzero size/true emphasis flags, then chooses template's packed
        // FontOverride unless it is -1 (white), otherwise the current override.
        style=body.styles.at(current);if(!formatter->font.empty())style.font=formatter->font;if(formatter->fontSize)style.size=float(formatter->fontSize);style.emphasized|=formatter->emphasized;style.blockEmphasized|=formatter->blockEmphasized;
        if(formatter->color!=Color{1,1,1,1})style.color=formatter->color;
        stack.push_back(current);
    }
    if(style.emphasized&&!style.blockEmphasized)throw std::runtime_error("Original emphasized font remap provider unavailable for format: "+name);
    if(body.styles.size()>=1024)throw std::runtime_error("Original text formatter stack bound");body.styles.push_back(std::move(style));current=uint32_t(body.styles.size()-1);
}
// ParseMarkup17f1c0 / ParseFormatting17c690 preserve style changes through
// recursive Lua localization. Format breaks retain horizontal continuation.
void parseMarkup(lua_State*L,const hades::TextFontBank&fonts,TextBody&body,const std::string&s,std::vector<hades::TextPiece>&pieces,int depth,uint32_t&current,std::vector<uint32_t>&stack){
    if(depth>8)throw std::runtime_error("Original text markup recursion bound");std::string run;
    for(size_t i=0;i<s.size();++i){const char c=s[i];
        if(c=='{'){const size_t end=s.find('}',i+1);if(end==std::string::npos)throw std::runtime_error("Unterminated original text markup");std::string inner=s.substr(i+1,end-i-1);
            const size_t lead=inner.find_first_not_of(" \t");inner=lead==std::string::npos?"":inner.substr(lead);
            const auto control=hades::input::controlForTextSymbol(inner);
            if(!control.empty()){
                auto*registry=lookup(L);std::string label,error;
                if(!registry||!registry->controlLabel||!registry->controlLabel(std::string(control),label,error))throw std::runtime_error(error.empty()?"Control prompt requires actual platform input label: "+std::string(control):error);
                body.controlLabels[std::string(control)]=label;appendText(pieces,run,current);run.clear();appendText(pieces,label,current);i=end;continue;
            }
            if(inner.empty()||(inner[0]!='$'&&inner[0]!='!'&&inner[0]!='#'))throw std::runtime_error("Original text markup unreconstructed: {"+inner+"}");
            appendText(pieces,run,current);run.clear();
            if(inner[0]=='#'){selectFormat(L,body,inner.substr(1),current,stack);i=end;continue;}
            const std::string value=luaMarkupValue(L,inner.substr(1));
            if(inner[0]=='!'){if(value.empty())throw std::runtime_error("Original empty text symbol path");pieces.push_back({true,value,current});}
            else{const bool autoFormat=!body.variableAutoFormat.empty();if(autoFormat)selectFormat(L,body,body.variableAutoFormat,current,stack);const std::string*display=fonts.displayName(value);parseMarkup(L,fonts,body,display?*display:value,pieces,depth+1,current,stack);if(autoFormat)selectFormat(L,body,"PreviousFormat",current,stack);}
            i=end;continue;}
        if(c=='@'||c=='\\'||c=='[')throw std::runtime_error(std::string("Original text token unreconstructed: ")+c);
        run.push_back(c);}
    appendText(pieces,run,current);}
void applyLayoutStyles(const TextBody&body,hades::TextLayoutRequest&request){for(const auto&style:body.styles)request.styles.push_back({style.font,style.size,style.textSymbolScale});}
// ShowDisplayName 0x174550: GetTextData(key)->DisplayName, else the key.
void reparse(lua_State*L,const hades::TextFontBank&fonts,TextBody&body,int32_t owner){
    const std::string before=body.resolved;std::string beforePieces;for(const auto&p:body.pieces)beforePieces+=(p.symbol?"@"+p.text+"@":p.text);
    body.resolved=body.raw?body.rawText:[&]{const auto*d=fonts.displayName(body.textKey);return d?*d:body.textKey;}();
    for(const auto&[key,ref]:body.luaParams){lua_rawgeti(L,LUA_REGISTRYINDEX,ref);lua_setglobal(L,key.c_str());}
    body.controlLabels.clear();body.styles={runStyle(body)};uint32_t current=0;std::vector<uint32_t> stack;std::vector<hades::TextPiece> pieces;parseMarkup(L,fonts,body,body.resolved,pieces,0,current,stack);body.pieces=std::move(pieces);body.dirty=false;
    if(!body.resolved.empty()){if(body.font.empty())throw std::runtime_error("Original default text font unreconstructed");if(body.fontSize<=0)throw std::runtime_error("Original nonpositive FontSize text unreconstructed");
        hades::TextLayoutRequest request;request.font=body.font;request.size=float(body.fontSize);request.pieces=body.pieces;request.textSymbolScale=body.textSymbolScale;request.horizontal=body.horizontal;request.vertical=body.vertical;request.width=body.width;applyLayoutStyles(body,request);if(request.pieces.empty())request.text=" ";std::vector<hades::TextSymbolQuad> symbols;fonts.layout(request,&symbols);}
    std::string shown;for(const auto&p:body.pieces)shown+=(p.symbol?"@"+p.text+"@":p.text);if(shown!=beforePieces)hades::log(hades::Severity::Info,"HadesLua","TEXT_BOX_TEXT owner=%d font=%s size=%d text=%s",int(owner),body.font.c_str(),int(body.fontSize),shown.c_str());(void)before;}
const std::set<std::string> explicitCreateKeys={"Id","AutoSetDataProperties","Text","RawText","UseDescription","Scale","Color","TooltipKey","TooltipData","LuaKey","LuaValue","FadeOpacity","FadeTarget","ScaleModifier","ScaleTarget","DataProperties","Hide"};
// AutoSetDataProperties reflection over the reconstruction's admitted fields.
void applyReflected(TextBody&body,const std::map<std::string,TextFormatProperty>&p){
    auto num=[&](const char*k,float&out){if(auto i=p.find(k);i!=p.end())out=float(i->second.number);};
    if(auto i=p.find("Font");i!=p.end())body.font=i->second.string;
    if(auto i=p.find("FontSize");i!=p.end())body.fontSize=int32_t(std::trunc(i->second.number));
    if(auto i=p.find("LocationZWithOwner");i!=p.end())body.locationZWithOwner=i->second.boolean;
    if(auto i=p.find("Graft");i!=p.end())body.graft=i->second.boolean;
    if(auto i=p.find("UseEmphasizedFont");i!=p.end())body.emphasized=i->second.boolean;
    if(auto i=p.find("BlockEmphasizedFont");i!=p.end())body.blockEmphasized=i->second.boolean;
    num("TextSymbolScale",body.textSymbolScale);num("Width",body.width);num("Height",body.height);num("OffsetX",body.offsetX);num("OffsetY",body.offsetY);num("OutlineThickness",body.outlineThickness);num("ShadowBlur",body.shadowBlur);
    if(auto i=p.find("Justification");i!=p.end())body.horizontal=horizontalJustification(i->second.string);
    if(auto i=p.find("VerticalJustification");i!=p.end())body.vertical=verticalJustification(i->second.string);
    if(auto i=p.find("ShadowColor");i!=p.end())body.shadowColor=hades::textSourceColor(i->second.array);
    if(auto i=p.find("OutlineColor");i!=p.end())body.outlineColor=hades::textSourceColor(i->second.array);
    if(auto i=p.find("ShadowOffset");i!=p.end()){body.shadowX=float(i->second.array[0]);body.shadowY=float(i->second.array[1]);}
    for(const char*k:{"CharacterFadeTime","CharacterFadeInterval"})if(auto i=p.find(k);i!=p.end()&&i->second.number!=0)throw std::runtime_error("Nonzero source character fade requires per-glyph component");
    for(const char*k:{"ShadowRed","ShadowGreen","ShadowBlue","ShadowAlpha","ShadowOffsetX","ShadowOffsetY","LineSpacingBottom","FadeDuration","Graft","UseEmphasizedFont","BlockEmphasizedFont"})if(p.contains(k))body.dirty=true; // retained in the format snapshot; consumers for text are not reached.
}
int create(lua_State* state) {
    if(!lua_istable(state,1)) throw std::runtime_error("CreateTextBox requires argument table");
    auto* registry=static_cast<TextState*>(lua_touserdata(state,lua_upvalueindex(1)));
    TextFormatSnapshot format;TextBody body;
    lua_getfield(state,1,"Id");{if(!lua_isnumber(state,-1))throw std::runtime_error("CreateTextBox Id requires positive signed32-bit integer");const double id=lua_tonumber(state,-1);if(id<=0||id>INT32_MAX||std::trunc(id)!=id)throw std::runtime_error("CreateTextBox Id requires positive signed32-bit integer");format.ownerId=int32_t(id);}lua_pop(state,1);
    const auto* object=findObjectSnapshot(state,format.ownerId);
    if(!object||!object->active||!object->alive)throw std::runtime_error("CreateTextBox requires existing active owner Thing ID");
    body.ownerLifetime=object->entityHandle;format.ownerActive=true;
    body.scale=std::clamp(argFloat(state,1,"Scale",1),0.f,1.f);
    body.rawText=argText(state,1,"RawText");body.raw=!body.rawText.empty();
    if(!body.raw){body.textKey=argText(state,1,"Text");if(!body.textKey.empty()&&argBool(state,1,"UseDescription",false))throw std::runtime_error("Original CreateTextBox UseDescription unreconstructed");}
    Color c;if(argColor(state,1,"Color",c))body.color=body.colorTarget=c;
    if(!argText(state,1,"TooltipKey").empty())throw std::runtime_error("Original text tooltip unreconstructed");
    body.fadeOpacity=argFloat(state,1,"FadeOpacity",0);body.fadeTarget=argFloat(state,1,"FadeTarget",1);
    body.scaleModifier=argFloat(state,1,"ScaleModifier",1);body.scaleTarget=argFloat(state,1,"ScaleTarget",1);
    body.autoSet=argBool(state,1,"AutoSetDataProperties",false);
    if(body.autoSet){
        lua_pushnil(state);
        while(lua_next(state,1)){const std::string key=stringAt(state,-2);if(!explicitCreateKeys.contains(key))format.properties.emplace(key,propertyAt(state,key,-1));lua_pop(state,1);}
        applyReflected(body,format.properties);
        if(auto i=format.properties.find("Name");i!=format.properties.end())format.name=i->second.string;
    } else {
        body.font=argText(state,1,"Font",body.font);body.fontSize=argInt(state,1,"FontSize",body.fontSize);
        body.textSymbolScale=argFloat(state,1,"TextSymbolScale",body.textSymbolScale);body.width=argFloat(state,1,"Width",body.width);body.height=argFloat(state,1,"Height",body.height);
        body.offsetX=float(argInt(state,1,"OffsetX",0));body.offsetY=float(argInt(state,1,"OffsetY",0));
        body.horizontal=horizontalJustification(argText(state,1,"Justification","CENTER"));body.vertical=verticalJustification(argText(state,1,"VerticalJustification","CENTER"));
        body.locationZWithOwner=argBool(state,1,"LocationZWithOwner",true);
        body.outlineThickness=argFloat(state,1,"OutlineThickness",0);body.shadowBlur=argFloat(state,1,"ShadowBlur",50);
        if(const float d=argFloat(state,1,"ScaleDuration",-1);d>=0)body.scaleSpeed=1/d;
        body.variableAutoFormat=argText(state,1,"VariableAutoFormat");
        format.fontSize=body.fontSize;
    }
    if(format.properties.contains("FontSize"))format.fontSize=body.fontSize;
    for(const char*k:{"LineSpacingTop","LineSpacingBottom","LineSpacingLeft","LineSpacingRight"})if(argInt(state,1,k,0)!=0&&!body.autoSet)throw std::runtime_error(std::string("Original text line spacing unreconstructed: ")+k);
    if(argColor(state,1,"OutlineColor",c)){body.outlineColor[0]=c[0];body.outlineColor[1]=c[1];body.outlineColor[2]=c[2];} // RGB only; alpha keeps its sentinel.
    if(argColor(state,1,"ShadowColor",c))body.shadowColor=c;
    if(hasArg(state,1,"ShadowOffset")){lua_getfield(state,1,"ShadowOffset");body.shadowX=body.shadowY=3;if(lua_istable(state,-1)&&lua_rawlen(state,-1)==2){lua_rawgeti(state,-1,1);lua_rawgeti(state,-2,2);if(lua_isnumber(state,-2)&&lua_isnumber(state,-1)){body.shadowX=float(lua_tonumber(state,-2));body.shadowY=float(lua_tonumber(state,-1));}lua_pop(state,2);}lua_pop(state,1);}
    lua_getfield(state,1,"DataProperties");if(lua_istable(state,-1)){const int t=lua_absindex(state,-1);lua_pushnil(state);while(lua_next(state,t)){const std::string key=stringAt(state,-2);if(key=="OpacityWithOwner"&&lua_isboolean(state,-1))body.opacityWithOwner=lua_toboolean(state,-1);else throw std::runtime_error("Original text DataProperties field unreconstructed: "+key);lua_pop(state,1);}}lua_pop(state,1);
    body.hide=argBool(state,1,"Hide",false);
    if(!body.resolved.empty()||body.raw||!body.textKey.empty()){if(!registry->fonts)throw std::runtime_error("CreateTextBox nonempty text requires original font layout resources");}
    setLuaParam(state,body,1);
    if(registry->formats.size()>=20000)throw std::runtime_error("Actual live text component bound");
    if(registry->fonts)reparse(state,*registry->fonts,body,format.ownerId);
    registry->components[format.ownerId]=body.ownerLifetime;
    format.sequence=registry->nextSequence++;registry->bodies.emplace(format.sequence,std::move(body));
    registry->formats.push_back(std::move(format));
    return 0; // Native bridge declares CreateTextBox a void action.
}
int modify(lua_State*state){
    if(!lua_istable(state,1))throw std::runtime_error("ModifyTextBox requires argument table");auto*registry=static_cast<TextState*>(lua_touserdata(state,lua_upvalueindex(1)));
    // ScriptAction::ModifyTextBox VA18025832a..35b gives a successfully parsed
    // numeric Id precedence; otherwise traverses numeric entries in Ids
    // (25840d..550). Neither field => no targets. ScreenAnchorReference is not
    // an alias in this native action (e.g. RangedLastAmmoPresentation passes it).
    std::vector<int32_t> owners;
    lua_getfield(state,1,"Id");int numeric=0;const auto id=lua_tointegerx(state,-1,&numeric);lua_pop(state,1);
    if(numeric){if(id<INT32_MIN||id>INT32_MAX)throw std::runtime_error("TextBox target outside native signed32 domain");owners.push_back(int32_t(id));}
    else{lua_getfield(state,1,"Ids");if(lua_istable(state,-1)){for(size_t i=1,n=lua_rawlen(state,-1);i<=n;++i){if(n>100000)throw std::runtime_error("TextBox target count bound");lua_rawgeti(state,-1,int(i));if(lua_isnumber(state,-1)){const double value=lua_tonumber(state,-1);if(!std::isfinite(value)||value<INT32_MIN||value>INT32_MAX)throw std::runtime_error("TextBox Ids outside native signed32 domain");owners.push_back(int32_t(value));}lua_pop(state,1);}}lua_pop(state,1);}
    if(owners.empty())return 0;
    if(argBool(state,1,"AutoSetDataProperties",false))throw std::runtime_error("Original ModifyTextBox AutoSetDataProperties reflection unreconstructed");
    for(const char*k:{"ParamValue","ParamTarget","TooltipKey","TooltipData","BlockTooltip","ReReadImmedaitely"})if(hasArg(state,1,k))throw std::runtime_error(std::string("Original ModifyTextBox argument unreconstructed: ")+k);
    if(argBool(state,1,"UseDescription",false)&&!argText(state,1,"Text").empty())throw std::runtime_error("Original ModifyTextBox UseDescription unreconstructed");
    const std::string affect=argText(state,1,"AffectText"),exclude=argText(state,1,"ExcludeText"),text=argText(state,1,"Text"),font=argText(state,1,"Font");
    Color color{},colorTarget{};const bool hasColor=argColor(state,1,"Color",color),hasColorTarget=argColor(state,1,"ColorTarget",colorTarget);
    const float fadeTarget=argFloat(state,1,"FadeTarget",-1),fadeDuration=argFloat(state,1,"FadeDuration",-1),scaleTarget=argFloat(state,1,"ScaleTarget",-1),scaleDuration=argFloat(state,1,"ScaleDuration",-1),colorDuration=argFloat(state,1,"ColorDuration",-1);
    if(fadeTarget>1||scaleTarget>32)throw std::runtime_error("Text transition target bound");
    const bool hasAutoFormat=hasArg(state,1,"VariableAutoFormat");const std::string autoFormat=argText(state,1,"VariableAutoFormat");const bool markDirty=argBool(state,1,"MarkDirty",false);
    for(auto&format:registry->formats){auto&body=registry->bodies.at(format.sequence);
        if(std::find(owners.begin(),owners.end(),format.ownerId)==owners.end()||body.destroyed)continue;const int32_t owner=format.ownerId;
        if(!affect.empty()&&format.name!=affect&&body.textKey!=affect)continue;
        if(!exclude.empty()&&(format.name==exclude||body.textKey==exclude))continue;
        if(!text.empty()&&(body.raw||text!=body.textKey)){body.textKey=text;body.raw=false;body.rawText.clear();body.dirty=true;}
        if(hasColor)body.color=body.colorTarget=color;
        if(fadeTarget>=0)body.fadeTarget=fadeTarget;if(fadeDuration>=0)body.fadeSpeed=1/fadeDuration;
        if(scaleTarget>=0)body.scaleTarget=scaleTarget;if(scaleDuration>=0)body.scaleSpeed=1/scaleDuration;
        if(hasColorTarget)body.colorTarget=colorTarget;if(colorDuration>=0)body.colorSpeed=1/colorDuration;
        if(hasAutoFormat)body.variableAutoFormat=autoFormat;
        if(!font.empty()){body.font=font;body.dirty=true;}
        if(markDirty)body.dirty=true;
        setLuaParam(state,body,1);
        if(body.dirty){if(!registry->fonts)throw std::runtime_error("ModifyTextBox text requires original font layout resources");reparse(state,*registry->fonts,body,owner);}
    }
    return 0;
}
// GUIComponent::Update stepping: value moves toward target by speed*dt.
void step(float&value,float target,float amount){if(value==target)return;if(amount>=std::abs(target-value))value=target;else if(target>value)value+=amount;else value-=amount;}
void eraseDestroyed(lua_State*state,TextState&registry){std::erase_if(registry.formats,[&](const auto&format){auto&body=registry.bodies.at(format.sequence);if(!body.destroyed)return false;releaseBody(state,body);registry.bodies.erase(format.sequence);return true;});}
int destroy(lua_State*state){if(!lua_istable(state,1))throw std::runtime_error("DestroyTextBox requires argument table");auto*registry=static_cast<TextState*>(lua_touserdata(state,lua_upvalueindex(1)));std::vector<int32_t> ids;std::string affect;bool first=false;
    lua_pushnil(state);while(lua_next(state,1)){auto key=stringAt(state,-2);if(key=="Id"){double id=numberAt(state,-1);if(id<=0||id>INT32_MAX||id!=std::trunc(id))throw std::runtime_error("DestroyTextBox Id bound");ids.push_back(int32_t(id));}else if(key=="Ids"){if(!lua_istable(state,-1))throw std::runtime_error("DestroyTextBox Ids requires array");const int a=lua_absindex(state,-1);for(size_t i=1;i<=lua_rawlen(state,a);++i){lua_rawgeti(state,a,int(i));double id=numberAt(state,-1);lua_pop(state,1);if(id<=0||id>INT32_MAX||id!=std::trunc(id))throw std::runtime_error("DestroyTextBox Ids bound");ids.push_back(int32_t(id));}}else if(key=="Text"||key=="AffectText")affect=stringAt(state,-1);else if(key=="FirstOnly"){if(!lua_isboolean(state,-1))throw std::runtime_error("FirstOnly requires bool");first=lua_toboolean(state,-1);}else throw std::runtime_error("Unsupported source DestroyTextBox argument: "+key);lua_pop(state,1);}
    for(auto&format:registry->formats){auto&body=registry->bodies.at(format.sequence);if(std::find(ids.begin(),ids.end(),format.ownerId)==ids.end()||body.destroyed||(!affect.empty()&&format.name!=affect&&body.textKey!=affect))continue;body.destroyed=true;if(first)break;}eraseDestroyed(state,*registry);return 0;}

int guardedModify(lua_State*s){char m[1024]{};try{return modify(s);}catch(const std::exception&e){std::snprintf(m,sizeof m,"%s",e.what());}return luaL_error(s,"%s",m);}
int guardedDestroy(lua_State*s){char m[1024]{};try{return destroy(s);}catch(const std::exception&e){std::snprintf(m,sizeof m,"%s",e.what());}return luaL_error(s,"%s",m);}
int guardedCreate(lua_State* state) {
    char message[1024]={};
    try {return create(state);} catch(const std::exception& error) {std::snprintf(message,sizeof(message),"%s",error.what());}
    return luaL_error(state,"%s",message);
}
int finalize(lua_State* state) {static_cast<TextState*>(lua_touserdata(state,1))->~TextState();return 0;}
}
void registerTextRuntime(lua_State* state) {
    const int top=lua_gettop(state);
    new(lua_newuserdata(state,sizeof(TextState))) TextState();
    const int owner=lua_gettop(state);
    if(luaL_newmetatable(state,ownerMetatable)) {lua_pushcfunction(state,finalize);lua_setfield(state,-2,"__gc");}
    lua_setmetatable(state,owner);
    lua_pushvalue(state,owner);lua_setfield(state,LUA_REGISTRYINDEX,registryKey);
    lua_pushvalue(state,owner);lua_pushcclosure(state,guardedCreate,1);lua_setglobal(state,"CreateTextBox");
    lua_pushvalue(state,owner);lua_pushcclosure(state,guardedModify,1);lua_setglobal(state,"ModifyTextBox");
    lua_pushvalue(state,owner);lua_pushcclosure(state,guardedDestroy,1);lua_setglobal(state,"DestroyTextBox");
    lua_settop(state,top);
}
void configureTextControlLabels(lua_State*state,TextControlLabelProvider provider){auto*registry=lookup(state);if(!registry)throw std::runtime_error("Text input-label registry unavailable");registry->controlLabel=std::move(provider);for(auto&[id,body]:registry->bodies)body.dirty=true;}
std::vector<TextFormatSnapshot> snapshotTextFormats(lua_State* state) {
    const auto* registry=lookup(state);if(!registry)return {};
    auto result=registry->formats;
    for(auto& format:result) {
        const auto* owner=findObjectSnapshot(state,format.ownerId);
        format.ownerActive=owner && owner->alive && owner->active && !registry->bodies.at(format.sequence).destroyed;
    }
    return result;
}
std::string dumpTextRuntime(lua_State* state) {
    const auto formats=snapshotTextFormats(state);std::ostringstream out;const auto*registry=lookup(state);
    out<<"TEXT_FORMATS count="<<formats.size()<<" glyphLayout=original-source\n";
    for(const auto& format:formats) {
        out<<format.sequence<<" owner="<<format.ownerId<<" name="<<format.name<<" ownerActive="<<format.ownerActive<<" properties="<<format.properties.size();
        if(format.fontSize)out<<" fontSize="<<*format.fontSize;
        if(registry){const auto&b=registry->bodies.at(format.sequence);if(!b.resolved.empty()){out<<" font="<<b.font<<" fade="<<b.fadeOpacity<<" text=\"";for(const auto&p:b.pieces)out<<(p.symbol?"@"+p.text+"@":p.text);out<<"\"";}}
        out<<'\n';
    }
    return out.str();
}
void configureTextRuntime(lua_State*state,std::shared_ptr<const hades::TextFontBank>fonts){auto*registry=lookup(state);if(!registry||!fonts)throw std::runtime_error("Text runtime/resource ownership unavailable");registry->fonts=std::move(fonts);}
bool advanceTextRuntime(lua_State*state,float world,float screen,std::string&error){try{
    if(!std::isfinite(world)||!std::isfinite(screen)||world<0||world>1||screen<0||screen>1)throw std::runtime_error("Text clock bound");auto*registry=lookup(state);if(!registry)return true;
    for(const auto&format:registry->formats){auto&body=registry->bodies.at(format.sequence);const auto*owner=findObjectSnapshot(state,format.ownerId);if(body.destroyed)continue;if(!owner||!owner->alive||owner->entityHandle!=body.ownerLifetime){body.destroyed=true;continue;}
        const float dt=owner->screenSpace?screen:world;
        for(const auto&[control,previous]:body.controlLabels){std::string current;if(!registry->controlLabel||!registry->controlLabel(control,current,error))throw std::runtime_error(error.empty()?"Control prompt input provider unavailable":error);if(current!=previous){body.dirty=true;break;}}
        if(body.dirty&&registry->fonts)reparse(state,*registry->fonts,body,format.ownerId);
        step(body.fadeOpacity,body.fadeTarget,dt*body.fadeSpeed);
        if(body.scaleSpeed>0)step(body.scaleModifier,body.scaleTarget,dt*body.scaleSpeed);
        if(body.colorSpeed>0&&body.color!=body.colorTarget)for(size_t c=0;c<4;++c){float v=body.color[c];step(v,body.colorTarget[c],body.colorSpeed*dt);body.color[c]=float(int(std::clamp(v,0.f,1.f)*255))/255;}
    }
    eraseDestroyed(state,*registry);return true;}catch(const std::exception&e){error=e.what();return false;}}
std::vector<hades::TextVisual> snapshotTextVisuals(lua_State*state){std::vector<hades::TextVisual>out;auto*registry=lookup(state);if(!registry||!registry->fonts)return out;
    for(const auto&format:registry->formats){const auto&body=registry->bodies.at(format.sequence);const auto*owner=findObjectSnapshot(state,format.ownerId);
        if(body.destroyed||body.hide||body.resolved.empty()||!owner||!owner->alive||!owner->active||!owner->visible||owner->entityHandle!=body.ownerLifetime)continue;
        hades::TextVisual v;v.sequence=format.sequence;v.ownerId=owner->id;v.group=owner->group;v.screenSpace=owner->screenSpace;v.ownerX=owner->x;v.ownerY=owner->y;v.locationZWithOwner=body.locationZWithOwner;v.ownerZ=body.locationZWithOwner?owner->z:0;v.font=body.font;v.text=body.resolved;
        v.color=body.color;v.color[3]*=body.fadeOpacity*owner->alpha;
        const float scale=body.scale*body.scaleModifier; // TextScale(): ScaleWithOwner (+0xc9) defaults false.
        if(body.fadeOpacity*owner->alpha<=0||scale<=0)continue;
        hades::TextLayoutRequest request;request.font=v.font;request.size=float(body.fontSize);request.width=body.width;request.scale=scale;request.horizontal=body.horizontal;request.vertical=body.vertical;request.pieces=body.pieces;request.textSymbolScale=body.textSymbolScale;applyLayoutStyles(body,request);
        v.glyphs=registry->fonts->layout(request,&v.symbols);
        for(auto&q:v.glyphs){q.x+=body.offsetX;q.y+=body.offsetY;}for(auto&q:v.symbols){q.x+=body.offsetX;q.y+=body.offsetY;}
        // Draw175e00 selects each Line::mFormatter's font and FontOverride,
        // while using the owning box's fade/owner alpha. Keep shared paragraph
        // positioning; partition only the atlas/style submission.
        for(size_t index=0;index<body.styles.size();++index){const auto&style=body.styles[index];hades::TextVisual run=v;run.font=style.font;run.glyphs.clear();run.symbols.clear();for(const auto&q:v.glyphs)if(q.style==index)run.glyphs.push_back(q);for(const auto&q:v.symbols)if(q.style==index)run.symbols.push_back(q);if(run.glyphs.empty()&&run.symbols.empty())continue;
            run.color=index?style.color:body.color;run.color[3]*=body.fadeOpacity*owner->alpha;
            run.shadowColor=style.shadowColor;run.shadowX=style.shadowX;run.shadowY=style.shadowY;run.shadowBlur=style.shadowBlur;run.outlineThickness=style.outlineThickness;
            if(style.outlineThickness>0){for(size_t c=0;c<3;++c)run.outlineColor[c]=float(int(std::clamp(style.outlineColor[c],0.f,1.f)*255))/255;run.outlineColor[3]=1;}
            for(auto&c:run.shadowColor)c=float(int(std::clamp(c,0.f,1.f)*255))/255;
            run.shadowColor[3]*=run.color[3];run.outlineColor[3]*=run.color[3];out.push_back(std::move(run));}}

    return out;}
bool attachedTextDimensions(lua_State*state,int32_t ownerId,std::array<float,2>&out,std::string&error){
    out={-1,-1};try{
        auto*registry=lookup(state);if(!registry){error.clear();return true;}
        const auto component=registry->components.find(ownerId);const auto*owner=findObjectSnapshot(state,ownerId);
        if(component==registry->components.end()||!owner||owner->entityHandle!=component->second){error.clear();return true;}
        out={0,0};
        for(const auto&format:registry->formats){if(format.ownerId!=ownerId)continue;auto&body=registry->bodies.at(format.sequence);
            if(body.destroyed||body.ownerLifetime!=owner->entityHandle||body.resolved.empty())continue;
            if(!registry->fonts)throw std::runtime_error("Attached text dimensions require source fonts");
            if(body.dirty)reparse(state,*registry->fonts,body,ownerId);
            hades::TextLayoutRequest request;request.font=body.font;request.size=float(body.fontSize);request.width=body.width;request.scale=body.scale*body.scaleModifier;request.horizontal=body.horizontal;request.vertical=body.vertical;request.pieces=body.pieces;request.textSymbolScale=body.textSymbolScale;applyLayoutStyles(body,request);
            // TextWidth/Height are measured regardless of fade, hide or owner alpha.
            if(request.scale==0)continue;
            std::array<float,2>measured{};std::vector<hades::TextSymbolQuad>symbols;
            registry->fonts->layout(request,&symbols,&measured);
            out[0]=std::max(out[0],measured[0]);out[1]=std::max(out[1],measured[1]);
        }
        error.clear();return true;
    }catch(const std::exception&e){error=e.what();return false;}
}
void disposeTextOwner(lua_State*state,int32_t ownerId){auto*registry=lookup(state);if(!registry)return;registry->components.erase(ownerId);std::erase_if(registry->formats,[&](const auto&format){if(format.ownerId!=ownerId)return false;releaseBody(state,registry->bodies.at(format.sequence));registry->bodies.erase(format.sequence);return true;});}
