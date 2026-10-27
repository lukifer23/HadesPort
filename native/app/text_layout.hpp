#pragma once
#include "asset_store.hpp"
#include "source_value.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace hades {
struct TextGlyph {std::array<int32_t,4> rect{},crop{};std::array<float,3> abc{};};
struct TextFont {
 std::string name;uint32_t width=0,height=0,nativeSize=0,fallback=0;
 uint32_t remapSize=0; // Game/FontSizes.remap record: Fonts::GetFont symbol-scale divisor.
 int32_t lineSpacing=0;float spacing=0;std::vector<uint8_t> rgba;
 std::map<uint32_t,TextGlyph> glyphs;
};
struct TextGlyphQuad {float x=0,y=0,w=0,h=0,u0=0,v0=0,u1=0,v1=0;uint32_t codepoint=0;uint32_t style=0;};
// Inline original atlas image placed by GUIComponentTextBox::ParseImage ("@path").
struct TextSymbolQuad {float x=0,y=0,w=0,h=0;std::string texture;uint32_t style=0;};
struct TextSymbol {std::string texture;uint32_t width=0,height=0,originalWidth=0,originalHeight=0,trimX=0,trimY=0;std::vector<uint8_t> rgba;};
// Parsed text run: plain text or one inline symbol (original texture path).
struct TextPiece {bool symbol=false;std::string text;uint32_t style=0;};
// GUIComponentTextBox Line::mFormatter: a format break continues the same
// physical line with a different font, size and symbol advance.
struct TextLayoutStyle {
 std::string font;float size=0,textSymbolScale=1;
 // GUIComponentTextBox runtime +0xb44/+0xb48, not font metrics: signed
 // pixel additions applied after TextScale (Draw 0x18017673c..0x1801767b0).
 int32_t lineSpacingTop=0,lineSpacingBottom=0;
 bool emphasized=false,blockEmphasized=false;
};
// ComponentData::SetupFont selects one exact EMPHASIS_MAPPING entry while
// retaining the authored font name; missing entries leave it unchanged.
std::string textFontName(const std::string& authored,bool emphasized,bool blockEmphasized);
struct TextVisual {
 uint64_t sequence=0;int32_t ownerId=0;std::string font,group,text;
 bool screenSpace=false,locationZWithOwner=true;float ownerX=0,ownerY=0,ownerZ=0;
 bool caption=false;std::string sourceWidget;
 // Shadow/outline alpha already includes source FancyText global vertex alpha.
 std::array<float,4> color{1,1,1,1},shadowColor{0,0,0,0},outlineColor{0,0,0,0};
 float shadowX=0,shadowY=0,shadowBlur=0,outlineThickness=0;
 std::vector<TextGlyphQuad> glyphs;
 std::vector<TextSymbolQuad> symbols;
};
enum class TextJustification {Start,End,Center};
struct TextLayoutRequest {
 std::string font,text;float size=0,width=0,scale=1;
 TextJustification horizontal=TextJustification::Center,vertical=TextJustification::Center;
 // When nonempty, replaces text: parsed runs including inline symbols.
 std::vector<TextPiece> pieces;float textSymbolScale=1; // data TextSymbolScale (tb+0x13c)
 std::vector<TextLayoutStyle> styles;
 int32_t lineSpacingTop=0,lineSpacingBottom=0;
 bool emphasized=false,blockEmphasized=false;
};
// Original UFN1/Alpha8 glyphs. Paragraph word wrapping is an explicit portable
// layout policy; per-glyph ABC/cropping/line spacing follows matching DrawInto.
class TextFontBank {
 std::map<std::string,TextFont> fonts_;std::map<std::string,std::string> localized_;
 // Optional catalog table "Descriptions": TextData Description per Id.
 std::map<std::string,std::string> descriptions_;bool descriptionsExported_=false;
 std::map<std::string,TextSymbol> symbols_;
 SourceValue subtitleStyle_;
public:
 void load(AssetStore&,const SourceValue&);
 const TextFont* font(const std::string&)const;
 const std::map<std::string,TextFont>& fonts()const{return fonts_;}
 const SourceValue& subtitleStyle()const{return subtitleStyle_;}
 std::string resolve(const std::string&)const;
 // GameDataManager::GetTextData(key)->DisplayName, or nullptr when no record.
 const std::string* displayName(const std::string&)const;
 // GameDataManager::GetDescription 0x1800b6a30: TextData(key) found ->
 // Description (+0x70) followed by the record's +0x88 string; nullptr when no
 // record. Callers must fail when !descriptionsExported() (catalog lacks it).
 const std::string* description(const std::string&)const;
 bool descriptionsExported()const{return descriptionsExported_;}
 const std::map<std::string,TextSymbol>& symbols()const{return symbols_;}
 std::vector<TextGlyphQuad> layout(const TextLayoutRequest&,std::vector<TextSymbolQuad>* symbols=nullptr,std::array<float,2>* measured=nullptr)const;
};
std::vector<uint32_t> textCodepoints(const std::string&);
// Exact GetColor policy: RGB all in0..1 selects float Color ctor even when
// alpha255; otherwise each channel is truncated to its packed unsigned byte.
std::array<float,4> textSourceColor(const std::vector<double>&);
struct SpeechPresentation;
class TextCaptionWorld {
 struct Slot {uint32_t channel=0;TextVisual visual;};
 std::array<std::optional<Slot>,2> slots_;
 std::map<uint32_t,size_t> completionSlots_;
public:
 bool consume(const std::vector<SpeechPresentation>&,const TextFontBank&,bool sourceSubtitlesEnabled,std::string&);
 std::vector<TextVisual> visuals()const;
};
}
