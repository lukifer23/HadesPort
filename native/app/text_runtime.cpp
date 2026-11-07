#include "text_runtime.hpp"
#include "lua_arguments.hpp"
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
    int32_t lineSpacingTop=0,lineSpacingBottom=0;
    Color color{1,1,1,1},shadowColor{0,0,0,0},outlineColor{-1,-1,-1,-1};
    float shadowX=0,shadowY=0,shadowBlur=50,outlineThickness=0;
    bool emphasized=false,blockEmphasized=false;
};
struct TextBody {
    uint64_t ownerLifetime=0;bool destroyed=false,autoSet=false,hide=false,blockTooltip=false;
    std::string textKey,rawText;bool raw=false,useDescription=false;
    std::string font;int32_t fontSize=0;float textSymbolScale=1,width=0,height=0,offsetX=0,offsetY=0;
    int32_t lineSpacingTop=0,lineSpacingBottom=0;
    // GetTextArea177ec7 uses ComponentDataDef+4dc, not runtime+b48.
    // ComponentData.Defaults0a2e71 zeroes this numeric template region.
    float dataLineSpacingBottom=0;
    // GUIComponent ctor1681ee/1681f5 initializes mArea(+20) to zero.
    // Empty text returns this stored area verbatim, not an owner/glyph AABB.
    std::array<int32_t,4> componentArea{};
    hades::TextJustification horizontal=hades::TextJustification::Center,vertical=hades::TextJustification::Center;
    float outlineThickness=0,shadowBlur=50,shadowX=0,shadowY=0;Color outlineColor{-1,-1,-1,-1},shadowColor{0,0,0,0};
    std::string variableAutoFormat;bool opacityWithOwner=false,locationZWithOwner=true,graft=false,emphasized=false,blockEmphasized=false;
    float scale=1,scaleModifier=1,scaleTarget=1,scaleSpeed=4,fadeOpacity=0,fadeTarget=1,fadeSpeed=4,colorSpeed=4;
    Color color{1,1,1,1},colorTarget{1,1,1,1};
    std::map<std::string,int> luaParams; // registry refs, GUIComponentTextBox+0xd08
    std::map<std::string,std::string> controlLabels;
    std::vector<std::string> parsedLuaKeys;
    float layoutScale=-1;std::vector<hades::TextGlyphQuad> layoutGlyphs;std::vector<hades::TextSymbolQuad> layoutSymbols;std::array<float,2> layoutDimensions{};
    uint64_t parseRevision=0;bool dirty=true;std::string resolved;std::vector<hades::TextPiece> pieces;
    std::vector<TextRunStyle> styles;
};
// MenuScreen.Load19d709 retains six independent GUI component triples.
struct TooltipSlot {
    std::string key;bool allocated=false;TextBody header,body;
    float x=0,y=0,imageOpacity=0,imageTarget=0;
    uint64_t sourceSequence=0,sourceRevision=0;
};
struct MenuTooltipState {
    bool initialized=false;std::array<TooltipSlot,6> slots;
    int32_t targetId=-1;uint64_t targetLifetime=0,textSequence=0;
    float showRemaining=0,hideRemaining=0;std::array<float,2> anchor{};
    hades::SourceValue templates;
};
struct TextState { MenuTooltipState menu;bool suppressLyrics=false; // sgg::Subtitles::SUPPRESS_LYRICS byte 0x181908619 (static zero).
    std::vector<TextFormatSnapshot> formats;std::map<uint64_t,TextBody> bodies;std::map<int32_t,uint64_t> components;std::shared_ptr<const hades::TextFontBank> fonts;TextControlLabelProvider controlLabel; uint64_t nextSequence = 1; };
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
// Complete compiled ComponentData reflection names, GetPropertyInfo0xaa0c0.
// Native SetProperty27fca2 returns false for unknown names; CreateTextBox270930
// ignores that result. Keep known-but-unrebuilt fields explicit rather than
// treating every unsupported property as an ignored Lua helper argument.
const std::set<std::string> componentReflectionFields{
    "Acceleration","ActionSound","AddColor","AddTextArea","AllowTooltipWhenInvisible",
    "AllowWrapping","Alpha","AlternateDisplayNameId","AlternateGraphic","AlternateHelpTextId",
    "AlternateHighlightFormat","AlternateTooltipFormat","AlwaysLowerCase","AlwaysUpperCase","Ambient",
    "Angle","AreaOffsetX","AreaOffsetY","AttachTo","AutoScrollDelay",
    "AutoScrollSpeed","BackAlpha","BackBlue","BackGraphic","BackGreen",
    "BackRed","BlockEmphasizedFont","Blue","BlueTarget","CauseCursorOcclusion",
    "CharacterFadeBlue","CharacterFadeColorLag","CharacterFadeGreen","CharacterFadeInterval","CharacterFadeRed",
    "CharacterFadeTime","CharacterIntervalMultiplier","ColonFadeInterval","ColorChangeSpeed","CommaFadeInterval",
    "ContainerGraphic","Deceleration","DescriptionId","DeselectOnMouseOff","DimAlpha",
    "DimBlue","DimGreen","DimRed","DisabledAnimation","DisabledFadeOutAnimation",
    "DisabledPressSound","DisabledTextAlpha","DisabledTextBlue","DisabledTextGreen","DisabledTextRed",
    "DisabledTopAnimation","DisplayNameId","DrawInFrontIndex","DynamicLineScaleWidth","ExclamationFadeInterval",
    "FadeDelay","FadeDelayMax","FadeDelayMin","FadeInSound","FadeOpacity",
    "FadeOutAnimation","FadeOutDelay","FadeOutSound","FadeSpeed","FadeTarget",
    "FillGraphic","FirstLetterFormat","FlashBlue","FlashGreen","FlashMax",
    "FlashMin","FlashRed","FlashSpeed","FlashText","FlipHorizontal",
    "FlipOffsetXAxis","FlipVertical","Font","FontSize","ForceDeselectOnClick",
    "ForceSelectOnClick","FreeFormSearchFromOffsetX","FreeFormSearchFromOffsetY","FreeFormSearchSuccessDistanceStep","FreeFormSelectable",
    "FrontGraphic","GamepadSelectionStyle","Graft","Graphic","Green",
    "GreenTarget","Height","HelpTextId","HighlightFormat","HighlightOffsetX",
    "HighlightOffsetY","HoverEndBlue","HoverEndGreen","HoverEndRed","HoverInDuration",
    "HoverOutDuration","HoverRadius","HoverShimmerBlue","HoverShimmerFrequency","HoverShimmerGreen",
    "HoverShimmerInfluence","HoverShimmerRed","Justification","LangCnScaleModifier","LangDeScaleModifier",
    "LangEsScaleModifier","LangFrScaleModifier","LangItScaleModifier","LangJaScaleModifier","LangKoScaleModifier",
    "LangPlScaleModifier","LangPtBrScaleModifier","LangRuScaleModifier","LeftMargin","LineDrawOffsetY",
    "LineSpacingBottom","LineSpacingLeft","LineSpacingRight","LineSpacingTop","MarqueeSpeed",
    "MaxCursorRadius","MaxX","MaxY","MinX","MinY",
    "MouseAlternateHelpTextId","MouseGraphicHover","MouseHelpTextId","MouseOverSound","MouseWheelScroll",
    "MouseWheelScrollSpeed","MoveEaseIn","MoveEaseOut","MoveSpeed","Name",
    "NonEnglishFont","NonEnglishScaleModifier","NumVisibleItems","OffsetX","OffsetY",
    "OpacityWithOwner","OutlineThickness","PaneCursorScroll","PeriodFadeInterval","PressAnimation",
    "PressSound","PressTopAnimation","PrintingSound","PulseMax","PulseMin",
    "PulseSpeed","QuestionFadeInterval","Red","RedTarget","RemoveFormattingOnDisable",
    "RepeatPeriodFadeInterval","RevealFxDuration","RevealFxEndBlue","RevealFxEndGreen","RevealFxEndRed",
    "RevealFxLineIndexScalar","RevealFxMidBlue","RevealFxMidGreen","RevealFxMidRed","RevealFxPerCharScalar",
    "RevealFxStartBlue","RevealFxStartGreen","RevealFxStartRed","Scale","ScaleSpeed",
    "ScaleTarget","ScaleWithOwner","ScaleWithZoom","ScaleX","ScaleY",
    "ScrollSpeed","ScrollWithSelection","SelectOnClick","SelectOnMouseOver","SelectedAlpha",
    "SelectedAnimationIsOverlay","SelectedAnimationOnlyIfAnimFinished","SelectedBlue","SelectedGraphic","SelectedGreen",
    "SelectedOffAnimation","SelectedOffTopAnimation","SelectedOnAnimation","SelectedOnTopAnimation","SelectedRed",
    "SelectedScale","SelectedTextAlpha","SelectedTextBlue","SelectedTextGreen","SelectedTextRed",
    "SelectedTextScale","SelectionFormat","SelectionSound","ShadowAlpha","ShadowBlue",
    "ShadowBlur","ShadowGreen","ShadowOffsetX","ShadowOffsetY","ShadowRed",
    "ShakeDistance","ShakeDuration","ShakeFalloffSpeed","ShakeSpeed","Spacing",
    "SpacingX","SpacingY","StopSoundOnMouseOffDuration","SwallowClicks","TextAlpha",
    "TextBlue","TextFadeTarget","TextGreen","TextJustification","TextOffsetX",
    "TextOffsetY","TextOutlineBlue","TextOutlineGreen","TextOutlineRed","TextRed",
    "TextScale","TextSymbolScale","ToggleOffSound","ToggleOnSound","TooltipAreaExtraHeight",
    "TooltipAreaOffsetY","TooltipId","TooltipOffsetX","TooltipOffsetY","TopAnimation",
    "TopFadeOutAnimation","TopMargin","UnderCursorOffAnimation","UnderCursorOnAnimation","UnselectedSound",
    "UseArea","UseEmphasizedFont","UseHover","UseInteractCursor","UseLineFading",
    "UseScissoring","UseTextArea","UseTooltipFlavor","UseTooltipHeader","UseableWhenFadedOut",
    "VariableAutoFormat","VerticalJustification","Width","WidthChangeSpeed","X",
    "Y"
};
const std::set<std::string> strings={"Name","Font","Justification","VerticalJustification","VariableAutoFormat"};
const std::set<std::string> booleans={"Graft","UseEmphasizedFont","BlockEmphasizedFont","LocationZWithOwner"};
// These are actual original UIData TextFormats fields, also present in native
// ComponentDataDef reflection / CreateTextBox explicit property reads.
const std::set<std::string> numbers={
    "FontSize","TextSymbolScale","Width","OffsetX","OffsetY","OutlineThickness",
    "ShadowAlpha","ShadowBlur","ShadowRed","ShadowGreen","ShadowBlue","ShadowOffsetX","ShadowOffsetY","LineSpacingTop","LineSpacingBottom","Height","Scale","FadeOpacity","FadeTarget","FadeDuration","CharacterFadeTime","CharacterFadeInterval",
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
bool argBool(lua_State*L,int t,const char*key,bool fallback){return hades::luaArgumentBool(L,t,key,fallback);}
// LuaTableExtensions::GetColor / ScriptArgs::ColorByName over original arrays.
// Keep potentially scripted indexed lookups inside a protected Lua frame, so
// an __index error cannot longjmp over the caller's C++ containers.
int colorChannels(lua_State*L){
 for(int i=1;i<=4;++i){lua_pushinteger(L,i);lua_gettable(L,1);if(lua_isnumber(L,-1)){const auto value=lua_tonumber(L,-1);lua_pop(L,1);lua_pushnumber(L,value);}else{lua_pop(L,1);lua_pushnumber(L,i==4?1:0);}}
 return 4;
}
bool argColor(lua_State*L,int t,const char*key,Color&out){
 lua_getfield(L,t,key);if(lua_isnil(L,-1)){lua_pop(L,1);return false;}
 // GetColor2496f0 returns white for a present non-table value. ColorByName
 // is the ScriptArgs argument-key lookup, not a global named-color resolver.
 if(!lua_istable(L,-1)){lua_pop(L,1);out={1,1,1,1};return true;}
 // GetColor2497bf..2498f6 reads RGB independently (default0), optional alpha
 // (default1). RGB selects normalized/byte conversion independently of alpha.
 lua_pushcfunction(L,colorChannels);lua_pushvalue(L,-2);
 if(lua_pcall(L,1,4,0)!=LUA_OK){const char*message=lua_tostring(L,-1);std::string error=message?message:"Indexed script color lookup failed";lua_pop(L,2);throw std::runtime_error(error);}
 std::vector<double>c;for(int i=-4;i<0;++i)c.push_back(lua_tonumber(L,i));lua_pop(L,5);out=hades::textSourceColor(c);return true;
}
hades::TextJustification horizontalJustification(const std::string&v){std::string u=v;std::transform(u.begin(),u.end(),u.begin(),[](unsigned char c){return char(std::toupper(c));});if(u=="LEFT")return hades::TextJustification::Start;if(u=="RIGHT")return hades::TextJustification::End;if(u=="UNKNOWN")throw std::runtime_error("Original UNKNOWN text justification unreconstructed");return hades::TextJustification::Center;}
hades::TextJustification verticalJustification(const std::string&v){std::string u=v;std::transform(u.begin(),u.end(),u.begin(),[](unsigned char c){return char(std::toupper(c));});if(u=="TOP")return hades::TextJustification::Start;if(u=="BOTTOM")return hades::TextJustification::End;return hades::TextJustification::Center;}
// SetLuaParam 0x175c80: key -> any (LuaValue), dirty.
void setLuaParam(lua_State*L,TextBody&body,int t){const std::string key=argText(L,t,"LuaKey");if(key.empty())return;lua_getfield(L,t,"LuaValue");const int ref=luaL_ref(L,LUA_REGISTRYINDEX);if(auto i=body.luaParams.find(key);i!=body.luaParams.end()){luaL_unref(L,LUA_REGISTRYINDEX,i->second);i->second=ref;}else{if(body.luaParams.size()>=64){luaL_unref(L,LUA_REGISTRYINDEX,ref);throw std::runtime_error("Text Lua parameter bound");}body.luaParams.emplace(key,ref);}body.dirty=true;}
void releaseBody(lua_State*L,TextBody&body){for(auto&[k,ref]:body.luaParams)luaL_unref(L,LUA_REGISTRYINDEX,ref);body.luaParams.clear();}
TextRunStyle runStyle(const TextBody&body){TextRunStyle out;out.font=body.font;out.size=float(body.fontSize);out.textSymbolScale=body.textSymbolScale;out.lineSpacingTop=body.lineSpacingTop;out.lineSpacingBottom=body.lineSpacingBottom;out.color=body.color;out.shadowColor=body.shadowColor;out.outlineColor=body.outlineColor;out.shadowX=body.shadowX;out.shadowY=body.shadowY;out.shadowBlur=body.shadowBlur;out.outlineThickness=body.outlineThickness;out.emphasized=body.emphasized;out.blockEmphasized=body.blockEmphasized;return out;}
void appendText(std::vector<hades::TextPiece>&pieces,const std::string&text,uint32_t style){if(text.empty())return;if(pieces.empty()||pieces.back().symbol||pieces.back().control!=hades::TextControl::None||pieces.back().style!=style)pieces.push_back({false,text,style});else pieces.back().text+=text;}
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
        // ParseFormatting 0x18017cd2c..0x18017cd3d creates a fresh text box
        // and calls GraftFrom(new,current,template). TextSystem::Create
        // 0x1802d79f5..0x1802d7a18 calls Clear (vtable+8 = 0x1801740d0),
        // zeroing runtime spacing. GraftFrom 0x180178818 copies ComponentData
        // only (+0xa8); neither source's runtime +0xb44/+0xb48 is copied.
        style.lineSpacingTop=style.lineSpacingBottom=0;
        if(formatter->color!=Color{1,1,1,1})style.color=formatter->color;
        stack.push_back(current);
    }
    if(body.styles.size()>=1024)throw std::runtime_error("Original text formatter stack bound");body.styles.push_back(std::move(style));current=uint32_t(body.styles.size()-1);
}
// Text lexer, English (non zh-CN/ja/ko) mode. Provenance:
//  - CharAsToken 0x180206950: single-character tokens for ! # $ % & ' ( ) * +
//    , . : ; < > ? @ [ \ ] ^ { } (the remaining ASCII is not a token char).
//  - NextToken 0x180206ba0: token char -> length 1; iswspace run -> 0x1b;
//    iswdigit run [. digit run] -> number; otherwise a word that runs until
//    iswspace or a token char (FUN_180172810 selects the CJK word rule only
//    for zh-CN/ja/ko). SkipWhitespace 0x180206b40 skips an iswspace run.
// iswspace is reconstructed for ASCII (TAB..CR, SPACE). Non-ASCII Unicode
// space classification is not recovered and fails explicitly when met here.
bool asciiSpace(unsigned char c){return c==' '||(c>=0x09&&c<=0x0d);}
bool lexTokenChar(unsigned char c){return c<0x80&&std::strchr("!#$%&'()*+,.:;<>?@[\\]^{}",c)&&c!=0;}
void rejectUnicodeSpace(const std::string&s,size_t at){
    const auto lead=uint8_t(s[at]);if(lead<0x80)return;
    const auto cps=hades::textCodepoints(s.substr(at,std::min<size_t>(4,s.size()-at)).substr(0,lead>=0xf0?4:lead>=0xe0?3:2));
    const uint32_t cp=cps.empty()?0:cps.front();
    if(cp==0x85||cp==0xa0||cp==0x1680||(cp>=0x2000&&cp<=0x200a)||cp==0x2028||cp==0x2029||cp==0x202f||cp==0x205f||cp==0x3000)throw std::runtime_error("Original text lexer iswspace classification unreconstructed for non-ASCII space");}
size_t lexTokenEnd(const std::string&s,size_t at){
    const auto c=uint8_t(s[at]);if(lexTokenChar(c))return at+1;
    if(asciiSpace(c)){while(at<s.size()&&asciiSpace(uint8_t(s[at])))++at;return at;}
    if(c>='0'&&c<='9'){while(at<s.size()&&s[at]>='0'&&s[at]<='9')++at;if(at+1<s.size()&&s[at]=='.'&&s[at+1]>='0'&&s[at+1]<='9'){++at;while(at<s.size()&&s[at]>='0'&&s[at]<='9')++at;}return at;}
    while(at<s.size()){const auto b=uint8_t(s[at]);if(asciiSpace(b)||lexTokenChar(b))break;rejectUnicodeSpace(s,at);at+=b<0x80?1:b>=0xf0?4:b>=0xe0?3:2;}return at;}
size_t skipLexSpace(const std::string&s,size_t at){while(at<s.size()&&asciiSpace(uint8_t(s[at])))++at;if(at<s.size())rejectUnicodeSpace(s,at);return at;}
// ParseLua17cf30 consumes lexer word/number tokens as table fields. Dot and
// bracket punctuation only delimit tokens; neither means expression evaluation.
// Colon switches to a suffix: each token sets P/p decoration from its first
// codepoint. Other suffixes use the ordinary ConvertAnyToString conversion.
std::string luaMarkupValue(lua_State*L,const std::string&path,std::string* finalField=nullptr){
    if(finalField)finalField->clear();
    if(path.empty()||path.size()>4096)throw std::runtime_error("Original Lua markup path bound");
    const int top=lua_gettop(L);struct Restore{lua_State*state;int top;~Restore(){lua_settop(state,top);}}restore{L,top};
    size_t at=0,fields=0;bool first=true,suffix=false,percent=false;
    while(at<path.size()){
        const size_t end=lexTokenEnd(path,at);const auto lead=uint8_t(path[at]);
        if(suffix){percent=lead=='P'||lead=='p';at=end;continue;}
        if(lead==':'){suffix=true;at=end;continue;}
        const bool number=lead>='0'&&lead<='9';
        const bool word=!lexTokenChar(lead)&&!asciiSpace(lead)&&!number;
        if(first||word||number){
            if(++fields>64)throw std::runtime_error("Original Lua markup field bound");
            const std::string part=path.substr(at,end-at);
            if(fields>1&&finalField)*finalField=part;
            if(first){lua_rawgeti(L,LUA_REGISTRYINDEX,LUA_RIDX_GLOBALS);first=false;}
            if(!lua_istable(L,-1))throw std::runtime_error("Original Lua text value unavailable: "+path);
            const int table=lua_absindex(L,-1);
            char*numberEnd=nullptr;const float index=std::strtof(part.c_str(),&numberEnd);
            if(fields==1||numberEnd==part.c_str()||*numberEnd)lua_pushlstring(L,part.data(),part.size());
            else{
                // Native ParseLua17d8d6 strtof + cvttss2si also handles decimal
                // numeric tokens as truncated integer indices.
                if(!std::isfinite(index)||double(std::trunc(index))<INT32_MIN||double(std::trunc(index))>INT32_MAX)throw std::runtime_error("Native Lua text index outside signed32 conversion");
                lua_pushinteger(L,int32_t(index));
            }
            lua_rawget(L,table);
            // The supported source tables have direct fields. Do not execute
            // an __index callback or silently claim its value was resolved.
            if(lua_isnil(L,-1)&&lua_getmetatable(L,table))throw std::runtime_error("Original Lua text metatable lookup unreconstructed: "+path);
            lua_remove(L,table);
        }
        at=end;
    }
    if(first)throw std::runtime_error("Original Lua markup empty field: "+path);
    std::string out;int32_t signValue=0;
    if(lua_type(L,-1)==LUA_TSTRING){size_t n=0;const char*b=lua_tolstring(L,-1,&n);if(n>65536||std::memchr(b,0,n))throw std::runtime_error("Original Lua text string bound");out.assign(b,n);}
    else if(lua_type(L,-1)==LUA_TNUMBER){
        const double value=lua_tonumber(L,-1),integer=std::trunc(value);
        if(!std::isfinite(value)||integer<INT32_MIN||integer>INT32_MAX)throw std::runtime_error("Native Lua text number outside signed32 conversion");
        signValue=int32_t(integer);char buffer[64];
        if(value!=integer)std::snprintf(buffer,sizeof buffer,"%g",value);
        else std::snprintf(buffer,sizeof buffer,"%d",int(signValue));out=buffer;
    }
    else if(lua_isboolean(L,-1))out=lua_toboolean(L,-1)?"true":"false";
    else throw std::runtime_error("Original Lua text value unavailable: "+path);
    // ParseMarkup17f39c..17f3de prefixes a sign selected by the truncated
    // ConvertAnyToInt result, then appends '%'; it neither takes abs nor scales
    // by 100. Preserve the native negative/sub-unit decoration quirks.
    if(percent)out=std::string(1,signValue<0?'-':'+')+out+'%';
    return out;
}
// ParseEscapeSequence180050: numeric token0x19 -> strtof. Column flushes
// the current segment and starts a non-newline ROOT formatter segment. Pad
// pads the current UTF8 line string to trunc(parameter) bytes with spaces.
// Both consume surrounding lexer whitespace. Unknown escapes retain native
// consume/drop semantics; no layout placeholder is emitted.
size_t parseEscape(const std::string&s,size_t backslash,std::string&run,std::vector<hades::TextPiece>&pieces,uint32_t&current){
    const size_t at=backslash+1;if(at>=s.size())return backslash;
    if(s[at]=='['||s[at]==']'||s[at]=='"'){run.push_back(s[at]);return at;}
    const size_t end=lexTokenEnd(s,at);const std::string token=s.substr(at,end-at);
    if(token=="n"){
        appendText(pieces,run,current);run.clear();current=0;
        pieces.push_back({false,"",current,hades::TextControl::NewLine});return skipLexSpace(s,end)-1;
    }
    if(token=="Pad"||token=="Column"){
        const size_t number=skipLexSpace(s,end);if(number>=s.size())return s.size()-1;
        const size_t numberEnd=lexTokenEnd(s,number),resume=skipLexSpace(s,numberEnd);
        // TextTokenizer accepts its number token, not a signed/word token.
        if(s[number]<'0'||s[number]>'9')return resume-1;
        const float value=std::strtof(s.substr(number,numberEnd-number).c_str(),nullptr);
        if(!std::isfinite(value)||value>100000)throw std::runtime_error("Native text column/padding numeric bound");
        appendText(pieces,run,current);run.clear();
        if(token=="Column"){current=0;pieces.push_back({false,"",current,hades::TextControl::Column,value});}
        else{const float bytes=std::trunc(value);if(bytes>16384)throw std::runtime_error("Native text padding exceeds paragraph byte bound");pieces.push_back({false,"",current,hades::TextControl::Pad,bytes});}
        return resume-1;
    }
    return end-1;
}
// ParseMarkup17f1c0 / ParseFormatting17c690 preserve style changes through
// recursive Lua localization. Format breaks retain horizontal continuation.
void parseMarkup(lua_State*L,const hades::TextFontBank&fonts,TextBody&body,const std::string&s,std::vector<hades::TextPiece>&pieces,int depth,uint32_t&current,std::vector<uint32_t>&stack){
    if(depth>8)throw std::runtime_error("Original text markup recursion bound");std::string run;
    const auto format=[&](const std::string&name){const auto previous=current;selectFormat(L,body,name,current,stack);if(current!=previous)pieces.push_back({false,"",current,hades::TextControl::FormatBreak});};
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
            if(inner[0]=='#'){format(inner.substr(1));i=end;continue;}
            // ParseLua17cf30 retains the final field token; ParseMarkup17f2c1
            // inserts it into mParsedLuaKeys(+d38), independently of its value.
            std::string parsedKey;const std::string value=luaMarkupValue(L,inner.substr(1),&parsedKey);
            if(!parsedKey.empty()&&std::find(body.parsedLuaKeys.begin(),body.parsedLuaKeys.end(),parsedKey)==body.parsedLuaKeys.end()){if(body.parsedLuaKeys.size()>=4096)throw std::runtime_error("Native parsed Lua-key bound");body.parsedLuaKeys.push_back(parsedKey);}
            if(inner[0]=='!'){if(value.empty())throw std::runtime_error("Original empty text symbol path");pieces.push_back({true,value,current});}
            else{const bool autoFormat=!body.variableAutoFormat.empty();if(autoFormat)format(body.variableAutoFormat);const std::string*display=fonts.displayName(value);parseMarkup(L,fonts,body,display?*display:value,pieces,depth+1,current,stack);if(autoFormat)format("PreviousFormat");}
            i=end;continue;}
        if(c=='\\'){i=parseEscape(s,i,run,pieces,current);continue;}
        // Parse case 0xc '@' (ParseImage 0x180180710) and case 3 '[' remain
        // unreconstructed at paragraph level.
        if(c=='@'||c=='[')throw std::runtime_error(std::string("Original text token unreconstructed: ")+c);
        run.push_back(c);}
    appendText(pieces,run,current);}
void applyLayoutStyles(const TextBody&body,hades::TextLayoutRequest&request){request.lineSpacingTop=body.lineSpacingTop;request.lineSpacingBottom=body.lineSpacingBottom;request.emphasized=body.emphasized;request.blockEmphasized=body.blockEmphasized;for(const auto&style:body.styles)request.styles.push_back({style.font,style.size,style.textSymbolScale,style.lineSpacingTop,style.lineSpacingBottom,style.emphasized,style.blockEmphasized});}
// ShowDisplayName 0x174550: GetTextData(key)->DisplayName, else the key.
// ShowDescription 0x1745e0: GameDataManager::GetDescription 0x1800b6a30. A key
// without a TextData record takes the engine's logged placeholder branch,
// which is not reproduced: explicit failure.
std::string resolveTextKey(const hades::TextFontBank&fonts,const std::string&key,bool useDescription){
    if(!useDescription){const auto*d=fonts.displayName(key);return d?*d:key;}
    if(!fonts.descriptionsExported())throw std::runtime_error("Original TextData Description records not exported in text catalog: "+key);
    const auto*d=fonts.description(key);if(!d)throw std::runtime_error("Original GetDescription missing-record placeholder unreconstructed: "+key);return *d;}
void reparse(lua_State*L,const hades::TextFontBank&fonts,TextBody&body,int32_t owner){
    const std::string before=body.resolved;std::string beforePieces;for(const auto&p:body.pieces)beforePieces+=(p.symbol?"@"+p.text+"@":p.text);
    body.resolved=body.raw?body.rawText:resolveTextKey(fonts,body.textKey,body.useDescription);
    for(const auto&[key,ref]:body.luaParams){lua_rawgeti(L,LUA_REGISTRYINDEX,ref);lua_setglobal(L,key.c_str());}
    body.controlLabels.clear();body.parsedLuaKeys.clear();body.layoutScale=-1;body.styles={runStyle(body)};uint32_t current=0;std::vector<uint32_t> stack;std::vector<hades::TextPiece> pieces;parseMarkup(L,fonts,body,body.resolved,pieces,0,current,stack);body.pieces=std::move(pieces);body.dirty=false;++body.parseRevision;
    if(!body.resolved.empty()){if(body.font.empty())throw std::runtime_error("Original default text font unreconstructed");if(body.fontSize<=0)throw std::runtime_error("Original nonpositive FontSize text unreconstructed");
        hades::TextLayoutRequest request;request.font=body.font;request.size=float(body.fontSize);request.pieces=body.pieces;request.textSymbolScale=body.textSymbolScale;request.horizontal=body.horizontal;request.vertical=body.vertical;request.width=body.width;applyLayoutStyles(body,request);if(request.pieces.empty())request.text=" ";std::vector<hades::TextSymbolQuad> symbols;fonts.layout(request,&symbols);}
    std::string shown;for(const auto&p:body.pieces)shown+=(p.symbol?"@"+p.text+"@":p.text);if(shown!=beforePieces)hades::log(hades::Severity::Info,"HadesLua","TEXT_BOX_TEXT owner=%d font=%s size=%d text=%s",int(owner),body.font.c_str(),int(body.fontSize),shown.c_str());(void)before;}
const std::set<std::string> explicitCreateKeys={"Id","AutoSetDataProperties","Text","RawText","UseDescription","Scale","Color","TooltipKey","TooltipData","LuaKey","LuaValue","FadeOpacity","FadeTarget","ScaleModifier","ScaleTarget","DataProperties","Hide"};
// AutoSetDataProperties reflection over the reconstruction's admitted fields.
void applyReflected(TextBody&body,const std::map<std::string,TextFormatProperty>&p){
    auto num=[&](const char*k,float&out){if(auto i=p.find(k);i!=p.end())out=float(i->second.number);};
    if(auto i=p.find("Font");i!=p.end())body.font=i->second.string;
    if(auto i=p.find("VariableAutoFormat");i!=p.end())body.variableAutoFormat=i->second.string;
    if(auto i=p.find("FontSize");i!=p.end())body.fontSize=int32_t(std::trunc(i->second.number));
    if(auto i=p.find("LocationZWithOwner");i!=p.end())body.locationZWithOwner=i->second.boolean;
    if(auto i=p.find("Graft");i!=p.end())body.graft=i->second.boolean;
    if(auto i=p.find("UseEmphasizedFont");i!=p.end())body.emphasized=i->second.boolean;
    if(auto i=p.find("BlockEmphasizedFont");i!=p.end())body.blockEmphasized=i->second.boolean;
    num("TextSymbolScale",body.textSymbolScale);num("Width",body.width);num("Height",body.height);num("OffsetX",body.offsetX);num("OffsetY",body.offsetY);num("OutlineThickness",body.outlineThickness);num("ShadowBlur",body.shadowBlur);
    num("LineSpacingBottom",body.dataLineSpacingBottom);
    if(auto i=p.find("Justification");i!=p.end())body.horizontal=horizontalJustification(i->second.string);
    if(auto i=p.find("VerticalJustification");i!=p.end())body.vertical=verticalJustification(i->second.string);
    if(auto i=p.find("ShadowColor");i!=p.end())body.shadowColor=hades::textSourceColor(i->second.array);
    if(auto i=p.find("OutlineColor");i!=p.end())body.outlineColor=hades::textSourceColor(i->second.array);
    if(auto i=p.find("ShadowOffset");i!=p.end()){body.shadowX=float(i->second.array[0]);body.shadowY=float(i->second.array[1]);}
    for(const char*k:{"CharacterFadeTime","CharacterFadeInterval"})if(auto i=p.find(k);i!=p.end()&&i->second.number!=0)throw std::runtime_error("Nonzero source character fade requires per-glyph component");
    for(const char*k:{"ShadowRed","ShadowGreen","ShadowBlue","ShadowAlpha","ShadowOffsetX","ShadowOffsetY","FadeDuration","Graft","UseEmphasizedFont","BlockEmphasizedFont"})if(p.contains(k))body.dirty=true; // retained in the format snapshot; consumers for text are not reached.
}
int create(lua_State* state) {
    if(!lua_istable(state,1)) throw std::runtime_error("CreateTextBox requires argument table");
    auto* registry=static_cast<TextState*>(lua_touserdata(state,lua_upvalueindex(1)));
    TextFormatSnapshot format;TextBody body;
    // CreateTextBox 0x1802702a3..0x18027035e: getfield Id, lua_tointegerx;
    // not convertible -> return without a component. The low 32 bits key the
    // World Thing map (+0x398, signed int at node+0x20); no entry or a null
    // Thing -> return without a component (both branches exit 0x18027173d).
    lua_getfield(state,1,"Id");{int numeric=0;const lua_Integer id=lua_tointegerx(state,-1,&numeric);lua_pop(state,1);if(!numeric)return 0;format.ownerId=int32_t(uint32_t(uint64_t(id)));}
    const auto* object=findObjectSnapshot(state,format.ownerId);
    if(!object)return 0;
    if(!object->active||!object->alive)throw std::runtime_error("CreateTextBox inactive/dead owner Thing semantics unreconstructed");
    body.ownerLifetime=object->entityHandle;format.ownerActive=true;
    body.scale=std::clamp(argFloat(state,1,"Scale",1),0.f,1.f);
    body.rawText=argText(state,1,"RawText");body.raw=!body.rawText.empty();
    // 0x1802704b2..0x1802704f8: nonempty Text -> Bool(UseDescription,false)
    // selects ShowDescription 0x1745e0 over ShowDisplayName 0x174550.
    if(!body.raw){body.textKey=argText(state,1,"Text");body.useDescription=!body.textKey.empty()&&argBool(state,1,"UseDescription",false);}
    Color c;if(argColor(state,1,"Color",c))body.color=body.colorTarget=c;
    if(!argText(state,1,"TooltipKey").empty())throw std::runtime_error("Original text tooltip unreconstructed");
    body.fadeOpacity=argFloat(state,1,"FadeOpacity",0);body.fadeTarget=argFloat(state,1,"FadeTarget",1);
    body.scaleModifier=argFloat(state,1,"ScaleModifier",1);body.scaleTarget=argFloat(state,1,"ScaleTarget",1);
    body.autoSet=argBool(state,1,"AutoSetDataProperties",false);
    if(body.autoSet){
        lua_pushnil(state);
        while(lua_next(state,1)){
            const std::string key=stringAt(state,-2);
            if(!explicitCreateKeys.contains(key)&&componentReflectionFields.contains(key))format.properties.emplace(key,propertyAt(state,key,-1));
            lua_pop(state,1);
        }
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
    // CreateTextBox 0x180270e85..0x180270eae, outside the AutoSet branch:
    // Int(default 0) -> textbox runtime +0xb44/+0xb48. Clear 0x180174152
    // zeros both; Draw 0x18017673c..0x1801767b0 consumes them in row pitch.
    body.lineSpacingTop=argInt(state,1,"LineSpacingTop",0);
    body.lineSpacingBottom=argInt(state,1,"LineSpacingBottom",0);
    for(const char*k:{"LineSpacingLeft","LineSpacingRight"})if(argInt(state,1,k,0)!=0)throw std::runtime_error(std::string("Original text line spacing unreconstructed: ")+k);
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
    for(const char*k:{"ParamValue","ParamTarget","TooltipKey","TooltipData","ReReadImmedaitely"})if(hasArg(state,1,k))throw std::runtime_error(std::string("Original ModifyTextBox argument unreconstructed: ")+k);
    // ModifyTextBox 0x1802be442..0x1802be5ee: Text (default "") then
    // Bool(UseDescription,false). Description path: GetDescription(Text) even
    // for an empty key; display path: TextData DisplayName, else the key. A
    // nonempty result that differs from the current text replaces it.
    const bool useDescription=argBool(state,1,"UseDescription",false);
    if(useDescription&&argText(state,1,"Text").empty())throw std::runtime_error("Original ModifyTextBox empty-key GetDescription placeholder unreconstructed");
    const std::string affect=argText(state,1,"AffectText"),exclude=argText(state,1,"ExcludeText"),text=argText(state,1,"Text"),font=argText(state,1,"Font");
    Color color{},colorTarget{};const bool hasColor=argColor(state,1,"Color",color),hasColorTarget=argColor(state,1,"ColorTarget",colorTarget);
    const float fadeTarget=argFloat(state,1,"FadeTarget",-1),fadeDuration=argFloat(state,1,"FadeDuration",-1),scaleTarget=argFloat(state,1,"ScaleTarget",-1),scaleDuration=argFloat(state,1,"ScaleDuration",-1),colorDuration=argFloat(state,1,"ColorDuration",-1);
    if(fadeTarget>1||scaleTarget>32)throw std::runtime_error("Text transition target bound");
    const bool hasBlockTooltip=hasArg(state,1,"BlockTooltip"),blockTooltip=argBool(state,1,"BlockTooltip",false);
    const bool hasAutoFormat=hasArg(state,1,"VariableAutoFormat");const std::string autoFormat=argText(state,1,"VariableAutoFormat");const bool markDirty=argBool(state,1,"MarkDirty",false);
    for(auto&format:registry->formats){auto&body=registry->bodies.at(format.sequence);
        if(std::find(owners.begin(),owners.end(),format.ownerId)==owners.end()||body.destroyed)continue;const int32_t owner=format.ownerId;
        const auto* currentOwner=findObjectSnapshot(state,owner);if(!currentOwner||currentOwner->entityHandle!=body.ownerLifetime)continue;
        if(!affect.empty()&&format.name!=affect&&body.textKey!=affect)continue;
        if(!exclude.empty()&&(format.name==exclude||body.textKey==exclude))continue;
        if(!text.empty()){if(!registry->fonts)throw std::runtime_error("ModifyTextBox text requires original font layout resources");const std::string next=resolveTextKey(*registry->fonts,text,useDescription);
            if(!next.empty()&&next!=body.resolved){body.textKey=text;body.useDescription=useDescription;body.raw=false;body.rawText.clear();body.dirty=true;}}
        // TextComponent.ModifyTextBox2beebf..2bef0d writes only present/non-nil.
        if(hasBlockTooltip)body.blockTooltip=blockTooltip;
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
// ScriptAction::Execute 0x180251910 id128 -> 0x180252428: SUPPRESS_LYRICS =
// LuaTableExtensions::Bool(args,"SuppressLyrics",false) (0x180247fe0); no
// other argument is read and nothing is returned. Its readers are the lyric
// subtitle presenters (Subtitles::ShowLyricA/B), which the port does not
// present yet; the state is retained for them (textSuppressLyrics).
int modifySubtitles(lua_State*state){auto*registry=static_cast<TextState*>(lua_touserdata(state,lua_upvalueindex(1)));
    if(!lua_istable(state,1))throw std::runtime_error("ModifySubtitles requires argument table");
    registry->suppressLyrics=hades::luaArgumentBool(state,1,"SuppressLyrics");return 0;}
int guardedModifySubtitles(lua_State*s){char m[1024]{};try{return modifySubtitles(s);}catch(const std::exception&e){std::snprintf(m,sizeof m,"%s",e.what());}return luaL_error(s,"%s",m);}
// ExecuteBool action312, selector18 -> 0x1802552c5..0x1802552e9:
// String(args,"Text","") -> GetTextData(0x1800b6970) != nullptr. This
// deliberately tests record presence, not DisplayName's length/content.
int hasDisplayName(lua_State*state){
    if(!lua_istable(state,1))throw std::runtime_error("HasDisplayName requires argument table");
    auto*registry=static_cast<TextState*>(lua_touserdata(state,lua_upvalueindex(1)));
    if(!registry||!registry->fonts)throw std::runtime_error("HasDisplayName requires original TextData catalog");
    const bool present=registry->fonts->hasTextData(argText(state,1,"Text"));lua_pushboolean(state,present);return 1;
}
int guardedHasDisplayName(lua_State*s){char m[1024]{};try{return hasDisplayName(s);}catch(const std::exception&e){std::snprintf(m,sizeof m,"%s",e.what());}return luaL_error(s,"%s",m);}
int finalize(lua_State* state) {auto*registry=static_cast<TextState*>(lua_touserdata(state,1));for(auto&[id,body]:registry->bodies)releaseBody(state,body);for(auto&slot:registry->menu.slots){releaseBody(state,slot.header);releaseBody(state,slot.body);}registry->~TextState();return 0;}
}
bool readScriptColorArgument(lua_State*state,int table,const char*key,std::array<float,4>&out){return argColor(state,table,key,out);}
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
    lua_pushvalue(state,owner);lua_pushcclosure(state,guardedModifySubtitles,1);lua_setglobal(state,"ModifySubtitles");
    lua_pushvalue(state,owner);lua_pushcclosure(state,guardedHasDisplayName,1);lua_setglobal(state,"HasDisplayName");
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
void configureTextRuntime(lua_State*state,std::shared_ptr<const hades::TextFontBank>fonts){auto*registry=lookup(state);if(!registry||!fonts)throw std::runtime_error("Text runtime/resource ownership unavailable");registry->fonts=std::move(fonts);
    const auto&templates=registry->fonts->nativeTooltipTemplates();
    if(!std::holds_alternative<std::monostate>(templates.value)){
        if(!templates.get("Schema")||templates.get("Schema")->text()!="HadesNativeMenuTooltips1"||!templates.get("SlotCount")||templates.get("SlotCount")->number()!=6||!templates.get("ScreenDataName")||templates.get("ScreenDataName")->text()!="InGameUI")throw std::runtime_error("Native InGameUI tooltip source schema mismatch");
        for(const auto*key:{"MenuScreenDefaults","InGameUIOverrides","Backing","Header","Body"})if(!templates.get(key))throw std::runtime_error("Native tooltip complete source templates required");
        registry->menu.templates=templates;registry->menu.initialized=true;
    }
}
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
    for(auto&slot:registry->menu.slots)if(slot.allocated){step(slot.imageOpacity,slot.imageTarget,4*screen);step(slot.header.fadeOpacity,slot.header.fadeTarget,slot.header.fadeSpeed*screen);step(slot.body.fadeOpacity,slot.body.fadeTarget,slot.body.fadeSpeed*screen);}
    eraseDestroyed(state,*registry);return true;}catch(const std::exception&e){error=e.what();return false;}}
namespace {
void ensureBodyLayout(TextBody& body,const hades::TextFontBank& fonts){
    const float scale=body.scale*body.scaleModifier;
    if(body.layoutScale==scale)return;
    body.layoutGlyphs.clear();body.layoutSymbols.clear();body.layoutDimensions={};
    if(scale>0&&!body.resolved.empty()){
        hades::TextLayoutRequest request;request.font=body.font;request.size=float(body.fontSize);request.width=body.width;request.scale=scale;request.horizontal=body.horizontal;request.vertical=body.vertical;request.pieces=body.pieces;request.textSymbolScale=body.textSymbolScale;applyLayoutStyles(body,request);
        body.layoutGlyphs=fonts.layout(request,&body.layoutSymbols,&body.layoutDimensions);
        for(auto&q:body.layoutGlyphs){q.x+=body.offsetX;q.y+=body.offsetY;}
        for(auto&q:body.layoutSymbols){q.x+=body.offsetX;q.y+=body.offsetY;}
    }
    body.layoutScale=scale;
}
// One geometry cache belongs to the parsed component. Fade/owner placement
// changes do not require paragraph layout or copies of all glyphs per style.
void appendBodyVisuals(TextBody&body,const hades::TextFontBank&fonts,const hades::TextVisual&base,float alpha,std::vector<hades::TextVisual>&out){
    if(body.hide||body.resolved.empty()||body.fadeOpacity*alpha<=0||body.scale*body.scaleModifier<=0)return;
    ensureBodyLayout(body,fonts);
    for(size_t index=0;index<body.styles.size();++index){
        const auto&style=body.styles[index];hades::TextVisual run=base;
        run.font=hades::textFontName(style.font,style.emphasized,style.blockEmphasized);run.text=body.resolved;
        for(const auto&q:body.layoutGlyphs)if(q.style==index)run.glyphs.push_back(q);
        for(const auto&q:body.layoutSymbols)if(q.style==index)run.symbols.push_back(q);
        if(run.glyphs.empty()&&run.symbols.empty())continue;
        run.color=index?style.color:body.color;run.color[3]*=body.fadeOpacity*alpha;
        run.shadowColor=style.shadowColor;run.shadowX=style.shadowX;run.shadowY=style.shadowY;run.shadowBlur=style.shadowBlur;run.outlineThickness=style.outlineThickness;
        if(style.outlineThickness>0){for(size_t c=0;c<3;++c)run.outlineColor[c]=float(int(std::clamp(style.outlineColor[c],0.f,1.f)*255))/255;run.outlineColor[3]=1;}
        for(auto&c:run.shadowColor)c=float(int(std::clamp(c,0.f,1.f)*255))/255;
        run.shadowColor[3]*=run.color[3];run.outlineColor[3]*=run.color[3];out.push_back(std::move(run));
    }
}
}
namespace {
const hades::SourceValue& tooltipField(const hades::SourceValue&value,const char*key){const auto*p=value.get(key);if(!p)throw std::runtime_error(std::string("Native tooltip source template missing: ")+key);return *p;}
float guiNumber(const hades::SourceValue&d,const char*key,float fallback){const auto*p=d.get(key);if(!p)return fallback;const double n=p->number();if(!std::isfinite(n)||std::abs(n)>100000)throw std::runtime_error(std::string("Native GUI numeric bound: ")+key);return float(n);}
std::string guiString(const hades::SourceValue&d,const char*key,const std::string&fallback={}){const auto*p=d.get(key);return p?p->text():fallback;}
TextBody tooltipTextTemplate(const hades::SourceValue&d){
    TextBody b;b.font=guiString(d,"Font");b.fontSize=int32_t(guiNumber(d,"FontSize",0));
    b.width=guiNumber(d,"Width",0);b.height=guiNumber(d,"Height",0);b.textSymbolScale=guiNumber(d,"TextSymbolScale",1);
    b.horizontal=horizontalJustification(guiString(d,"TextJustification",guiString(d,"Justification","Center")));
    b.vertical=verticalJustification(guiString(d,"VerticalJustification","Center"));
    b.lineSpacingTop=int32_t(guiNumber(d,"LineSpacingTop",0));b.lineSpacingBottom=int32_t(guiNumber(d,"LineSpacingBottom",0));
    b.dataLineSpacingBottom=guiNumber(d,"LineSpacingBottom",0);b.scale=guiNumber(d,"Scale",1);
    b.color=b.colorTarget=hades::textSourceColor({guiNumber(d,"TextRed",1),guiNumber(d,"TextGreen",1),guiNumber(d,"TextBlue",1),guiNumber(d,"TextAlpha",1)});
    b.shadowColor={guiNumber(d,"ShadowRed",0),guiNumber(d,"ShadowGreen",0),guiNumber(d,"ShadowBlue",0),guiNumber(d,"ShadowAlpha",0)};
    b.shadowX=guiNumber(d,"ShadowOffsetX",0);b.shadowY=guiNumber(d,"ShadowOffsetY",0);b.shadowBlur=guiNumber(d,"ShadowBlur",50);
    b.outlineThickness=guiNumber(d,"OutlineThickness",0);b.outlineColor={guiNumber(d,"TextOutlineRed",0),guiNumber(d,"TextOutlineGreen",0),guiNumber(d,"TextOutlineBlue",0),1};
    b.fadeOpacity=guiNumber(d,"FadeOpacity",1);b.fadeTarget=guiNumber(d,"FadeTarget",1);b.fadeSpeed=guiNumber(d,"FadeSpeed",4);
    return b;
}
void hideTooltipState(MenuTooltipState&m,float showDelay){
    // MenuScreen.HideTooltip19e0b0; FadeOut169340 changes target, not opacity.
    for(auto&slot:m.slots){slot.key.clear();if(slot.allocated){slot.imageTarget=0;slot.header.fadeTarget=slot.body.fadeTarget=0;}}
    m.anchor={};m.targetId=-1;m.targetLifetime=0;m.textSequence=0;
    if(m.showRemaining>0)m.showRemaining=showDelay;
}
void copyTooltipParams(lua_State*L,TextBody&destination,const TextBody&source){
    releaseBody(L,destination);
    for(const auto&[key,ref]:source.luaParams){lua_rawgeti(L,LUA_REGISTRYINDEX,ref);destination.luaParams.emplace(key,luaL_ref(L,LUA_REGISTRYINDEX));}
}
void clearTooltipText(lua_State*L,TextBody&body){
    // Clear1740d0 -> base Clear168e30 restores runtime opacity/target1,
    // clears lines, params and source-parent; compiled template data remains.
    releaseBody(L,body);body.lineSpacingTop=body.lineSpacingBottom=0;body.raw=false;body.textKey.clear();body.rawText.clear();body.resolved.clear();body.pieces.clear();body.styles.clear();body.parsedLuaKeys.clear();body.controlLabels.clear();body.fadeOpacity=body.fadeTarget=1;body.scaleModifier=body.scaleTarget=1;body.layoutScale=-1;body.dirty=true;
}
void showTooltipText(lua_State*L,const hades::TextFontBank&fonts,TextBody&body,const std::string&key,bool description){
    // ShowDisplayName174550 / ShowDescription1745e0 -> ShowText174690.
    // Its nonempty path sets FadeTarget1 and clears Hide; empty fades out.
    const auto text=resolveTextKey(fonts,key,description);
    const bool changed=body.textKey!=key||body.useDescription!=description||body.resolved!=text;
    body.textKey=key;body.useDescription=description;body.raw=false;body.hide=false;
    body.fadeTarget=text.empty()?0:1;
    if(changed||body.dirty)reparse(L,fonts,body,0);
}
void appendMenuTooltipVisuals(TextState&registry,std::vector<hades::TextVisual>&out){
    auto&m=registry.menu;if(!m.initialized||!registry.fonts)return;
    const auto&backing=tooltipField(m.templates,"Backing");const auto graphic=guiString(backing,"Graphic");
    for(size_t i=0;i<m.slots.size();++i){auto&slot=m.slots[i];if(!slot.allocated)continue;
        hades::TextVisual v;v.ownerId=0;v.screenSpace=true;v.locationZWithOwner=false;v.ownerX=slot.x;v.ownerY=slot.y;v.sourceWidget="TooltipBacking";
        if(slot.imageOpacity>0){
            const auto at=registry.fonts->symbols().find(graphic);if(at==registry.fonts->symbols().end())throw std::runtime_error("Original native tooltip backing not loaded");
            const auto&image=at->second;const float scale=guiNumber(backing,"Scale",1);
            v.color[3]=slot.imageOpacity;
            v.symbols.push_back({(float(image.trimX)-float(image.originalWidth)*.5f)*scale,(float(image.trimY)-float(image.originalHeight)*.5f)*scale,float(image.width)*scale,float(image.height)*scale,graphic});
            out.push_back(std::move(v));
        }
        v={};v.ownerId=0;v.screenSpace=true;v.locationZWithOwner=false;v.ownerX=slot.x+guiNumber(tooltipField(m.templates,"Header"),"OffsetX",0);v.ownerY=slot.y+guiNumber(tooltipField(m.templates,"Header"),"OffsetY",0);v.sourceWidget="TooltipHeader";
        appendBodyVisuals(slot.header,*registry.fonts,v,1,out);
        v.ownerX=slot.x+guiNumber(tooltipField(m.templates,"Body"),"OffsetX",0);v.ownerY=slot.y+guiNumber(tooltipField(m.templates,"Body"),"OffsetY",0);v.sourceWidget="TooltipBody";
        appendBodyVisuals(slot.body,*registry.fonts,v,1,out);
    }
}
}
bool hideMenuTooltips(lua_State*L,float showDelay,std::string&error){try{
    auto*r=lookup(L);if(!r||!r->menu.initialized)throw std::runtime_error("HideTopMenuScreenTooltips requires actual native MenuScreen resources/lifetime");
    hideTooltipState(r->menu,showDelay);error.clear();return true;
}catch(const std::exception&e){error=e.what();return false;}}
bool advanceMenuTooltips(lua_State*L,float elapsed,bool eligible,int32_t ownerId,uint64_t lifetime,const std::array<float,2>&anchor,float showDelay,float hideDelay,std::string&error){try{
    auto*r=lookup(L);if(!r||!r->fonts)throw std::runtime_error("Native tooltip text resources unavailable");auto&m=r->menu;
    if(!m.initialized)throw std::runtime_error("Native InGameUI/MenuScreen source resources unavailable");
    if(!std::isfinite(elapsed)||elapsed<0||elapsed>1||!std::isfinite(showDelay)||!std::isfinite(hideDelay))throw std::runtime_error("Native tooltip clock/config bound");
    if(!eligible){hideTooltipState(m,showDelay);error.clear();return true;}
    m.showRemaining-=elapsed;m.hideRemaining-=elapsed;
    bool queued=false;const auto*owner=findObjectSnapshot(L,ownerId);
    if(owner&&owner->entityHandle==lifetime&&owner->alive&&owner->active&&owner->visible){
        // InGameUI.UpdateTooltip1d9ca0 visits the actual ordered TextComponent
        // boxes, retaining final parsed field tokens. Keywords is a direct Lua
        // table; unmapped keys fall back to their own original HelpText id.
        for(const auto&format:r->formats){if(format.ownerId!=ownerId)continue;auto&source=r->bodies.at(format.sequence);
            if(source.destroyed||source.ownerLifetime!=lifetime||source.blockTooltip)continue;
            for(const auto&parsed:source.parsedLuaKeys){
                std::string key=parsed;const int top=lua_gettop(L);lua_getglobal(L,"Keywords");
                if(lua_istable(L,-1)){lua_pushlstring(L,parsed.data(),parsed.size());lua_rawget(L,-2);if(lua_type(L,-1)==LUA_TSTRING)key=stringAt(L,-1);}
                lua_settop(L,top);
                const auto*description=r->fonts->description(key);if(!description||description->empty())continue;
                // QueueTooltip19f7c0 hides a different Thing target, remembers
                // the actual source textbox and refreshes the hide countdown.
                if(m.targetId!=ownerId||m.targetLifetime!=lifetime)hideTooltipState(m,showDelay);
                m.targetId=ownerId;m.targetLifetime=lifetime;m.textSequence=format.sequence;m.anchor=anchor;m.hideRemaining=hideDelay;
                auto slot=std::find_if(m.slots.begin(),m.slots.end(),[&](const auto&s){return s.key.empty()||s.key==key;});
                if(slot!=m.slots.end()){slot->key=key;queued=true;}
            }
        }
    }
    if(!queued)m.showRemaining=showDelay;
    if(m.showRemaining<=0){float y=m.anchor[1];
        for(auto&slot:m.slots)if(!slot.key.empty()){
            if(!slot.allocated){
                slot.header=tooltipTextTemplate(tooltipField(m.templates,"Header"));slot.body=tooltipTextTemplate(tooltipField(m.templates,"Body"));
                // GUIComponent.Clear168e67 starts image opacity/target at1.
                slot.imageOpacity=slot.imageTarget=1;slot.allocated=true;
            }
            slot.imageTarget=1;
            if(slot.header.fadeOpacity==0)clearTooltipText(L,slot.header);
            const auto source=r->bodies.find(m.textSequence);
            const bool sourceChanged=slot.sourceSequence!=m.textSequence;
            if(slot.body.fadeOpacity==0||sourceChanged)clearTooltipText(L,slot.body);
            if(source!=r->bodies.end()&&(sourceChanged||slot.sourceRevision!=source->second.parseRevision||slot.body.dirty)){copyTooltipParams(L,slot.body,source->second);slot.body.dirty=true;slot.sourceRevision=source->second.parseRevision;}
            slot.sourceSequence=m.textSequence;
            showTooltipText(L,*r->fonts,slot.header,slot.key,false);showTooltipText(L,*r->fonts,slot.body,slot.key,true);
            slot.x=m.anchor[0];slot.y=y;y+=guiNumber(tooltipField(m.templates,"Backing"),"SpacingY",0);
        }
        if(y>1080)for(auto&slot:m.slots)if(!slot.key.empty())slot.y+=1080-y;
    }
    if(m.hideRemaining<=0)hideTooltipState(m,showDelay);
    error.clear();return true;
}catch(const std::exception&e){error=e.what();return false;}}
MenuTooltipSnapshot snapshotMenuTooltips(lua_State*L){MenuTooltipSnapshot out;auto*r=lookup(L);if(!r)return out;const auto&m=r->menu;out.initialized=m.initialized;out.targetId=m.targetId;out.ownerLifetime=m.targetLifetime;out.textSequence=m.textSequence;out.showRemaining=m.showRemaining;out.hideRemaining=m.hideRemaining;out.anchor=m.anchor;for(const auto&s:m.slots){if(!s.key.empty())out.activeKeys.push_back(s.key);if(s.allocated){out.retainedComponents+=3;out.visibleComponents+=(s.imageOpacity>0)+(s.header.fadeOpacity>0)+(s.body.fadeOpacity>0);}}return out;}
std::vector<hades::TextVisual> snapshotTextVisuals(lua_State*state){std::vector<hades::TextVisual>out;auto*registry=lookup(state);if(!registry||!registry->fonts)return out;
    for(const auto&format:registry->formats){auto&body=registry->bodies.at(format.sequence);const auto*owner=findObjectSnapshot(state,format.ownerId);
        if(body.destroyed||!owner||!owner->alive||!owner->active||!owner->visible||owner->entityHandle!=body.ownerLifetime)continue;
        hades::TextVisual v;v.sequence=format.sequence;v.ownerId=owner->id;v.group=owner->group;v.screenSpace=owner->screenSpace;v.ownerX=owner->x;v.ownerY=owner->y;v.locationZWithOwner=body.locationZWithOwner;v.ownerZ=body.locationZWithOwner?owner->z:0;
        appendBodyVisuals(body,*registry->fonts,v,owner->alpha,out);
    }
    appendMenuTooltipVisuals(*registry,out);return out;
}
namespace {
std::array<float,2> measureAttachedText(lua_State*state,TextState&registry,TextBody&body,int32_t ownerId){
    if(body.dirty&&registry.fonts)reparse(state,*registry.fonts,body,ownerId);
    if(body.resolved.empty())return {0,0};
    if(!registry.fonts)throw std::runtime_error("Attached text dimensions require source fonts");
    ensureBodyLayout(body,*registry.fonts);return body.layoutDimensions;
}
int32_t textAreaInteger(float value){
    const double truncated=std::trunc(double(value));
    if(!std::isfinite(value)||truncated<INT32_MIN||truncated>INT32_MAX)throw std::runtime_error("Text interaction area outside native signed32 conversion");
    return int32_t(truncated);
}
}
bool attachedTextDimensions(lua_State*state,int32_t ownerId,std::array<float,2>&out,std::string&error){
    out={-1,-1};try{
        auto*registry=lookup(state);if(!registry){error.clear();return true;}
        const auto component=registry->components.find(ownerId);const auto*owner=findObjectSnapshot(state,ownerId);
        if(component==registry->components.end()||!owner||owner->entityHandle!=component->second){error.clear();return true;}
        out={0,0};
        for(const auto&format:registry->formats){if(format.ownerId!=ownerId)continue;auto&body=registry->bodies.at(format.sequence);
            if(body.destroyed||body.ownerLifetime!=owner->entityHandle)continue;
            const auto measured=measureAttachedText(state,*registry,body,ownerId);
            out[0]=std::max(out[0],measured[0]);out[1]=std::max(out[1],measured[1]);
        }
        error.clear();return true;
    }catch(const std::exception&e){error=e.what();return false;}
}
bool attachedTextAreas(lua_State*state,int32_t ownerId,std::vector<std::array<int32_t,4>>&out,std::string&error){
#ifdef __clang__
#pragma clang fp contract(off)
#endif
    out.clear();try{
        auto*registry=lookup(state);if(!registry){error.clear();return true;}
        const auto component=registry->components.find(ownerId);const auto*owner=findObjectSnapshot(state,ownerId);
        if(component==registry->components.end()||!owner||!owner->entityHandle||owner->entityHandle!=component->second){error.clear();return true;}
        std::vector<std::array<int32_t,4>> areas;
        for(const auto&format:registry->formats){if(format.ownerId!=ownerId)continue;auto&body=registry->bodies.at(format.sequence);
            if(body.destroyed||body.ownerLifetime!=owner->entityHandle)continue;
            const auto measured=measureAttachedText(state,*registry,body,ownerId);
            if(body.resolved.empty()){areas.push_back(body.componentArea);continue;}
            if(std::none_of(body.pieces.begin(),body.pieces.end(),[](const auto&piece){return piece.symbol||!piece.text.empty();}))throw std::runtime_error("Native interaction text-area line presence for control-only text not reconstructed");
            // GetTextArea177df2..177e26: convert actual dimensions separately,
            // then adjust float origin using those integer dimensions. Source
            // CreateTextBox writes OffsetX/Y to component+110/+114; default
            // text offsets+188/+18c are zero. GUIComponent.GetLocation169a70
            // does not subtract owner Z or filter opacity/Hide.
            const int32_t width=textAreaInteger(measured[0]),height=textAreaInteger(measured[1]);
            float x=owner->x+body.offsetX,y=owner->y+body.offsetY;
            if(body.horizontal==hades::TextJustification::Center)x-=float(width)*.5f;
            else if(body.horizontal==hades::TextJustification::End)x-=float(width);
            const int32_t left=textAreaInteger(x);int32_t top=textAreaInteger(y);
            // Native Bottom1 and Center2 both subtract HALF the integer height
            // (177e7f..177e99); GetTextArea differs from paragraph draw origin.
            if(body.vertical!=hades::TextJustification::Start)top=textAreaInteger(y-float(height)*.5f);
            else{
                const int32_t correction=textAreaInteger(-.5f*body.dataLineSpacingBottom);
                const int64_t adjusted=int64_t(top)+correction;
                if(adjusted<INT32_MIN||adjusted>INT32_MAX)throw std::runtime_error("Text interaction top offset outside signed32 range");
                top=int32_t(adjusted);
            }
            areas.push_back({left,top,width,height});
        }
        out=std::move(areas);error.clear();return true;
    }catch(const std::exception&e){error=e.what();return false;}
}

void disposeTextOwner(lua_State*state,int32_t ownerId){auto*registry=lookup(state);if(!registry)return;registry->components.erase(ownerId);std::erase_if(registry->formats,[&](const auto&format){if(format.ownerId!=ownerId)return false;releaseBody(state,registry->bodies.at(format.sequence));registry->bodies.erase(format.sequence);return true;});}
bool textSuppressLyrics(lua_State*state){const auto*registry=lookup(state);return registry&&registry->suppressLyrics;}
