#include "text_layout.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace hades {namespace {
struct Reader {std::vector<uint8_t>b;size_t p=0;template<class T>T get(){if(sizeof(T)>b.size()-p)throw std::runtime_error("Truncated original font");T v;std::memcpy(&v,b.data()+p,sizeof v);p+=sizeof v;return v;}};
Reader read(AssetStore&s,const std::string&name,size_t bound){if(name.empty()||name.find("..")!=std::string::npos||name.front()=='/')throw std::runtime_error("Invalid font asset path");auto f=s.open(name);if(!f||f->size()>bound)throw std::runtime_error("Missing/bounded original font "+name);Reader r;r.b.resize(f->size());size_t at=0;while(at<r.b.size()){auto n=f->read(r.b.data()+at,r.b.size()-at);if(!n)throw std::runtime_error("Truncated font asset");at+=n;}return r;}
const TextGlyph& glyph(const TextFont&f,uint32_t cp){auto i=f.glyphs.find(cp);if(i==f.glyphs.end())i=f.glyphs.find(f.fallback);if(i==f.glyphs.end())throw std::runtime_error("Original font missing glyph/fallback");return i->second;}
}
// Validate/decode catalog UTF-8 within the byte bound. Original patch-note
// descriptions exceed 16K codepoints; the 16K displayed-item limit belongs
// to layout(), after markup expansion, not to stored localization records.
std::vector<uint32_t> textCodepoints(const std::string&s){if(s.size()>65536)throw std::runtime_error("Text byte bound");std::vector<uint32_t> out;for(size_t p=0;p<s.size();){const auto lead=uint8_t(s[p++]);uint32_t cp=lead;unsigned n=0;uint32_t minimum=0;if(lead>=0xc2&&lead<=0xdf){n=1;cp=lead&31;minimum=0x80;}else if(lead>=0xe0&&lead<=0xef){n=2;cp=lead&15;minimum=0x800;}else if(lead>=0xf0&&lead<=0xf4){n=3;cp=lead&7;minimum=0x10000;}else if(lead>=128||!lead)throw std::runtime_error("Invalid text UTF8");if(n>s.size()-p)throw std::runtime_error("Truncated text UTF8");while(n--){auto b=uint8_t(s[p++]);if((b&0xc0)!=0x80)throw std::runtime_error("Invalid text UTF8 continuation");cp=(cp<<6)|(b&63);}if(cp<minimum||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))throw std::runtime_error("Noncanonical text UTF8");out.push_back(cp);}return out;}
std::array<float,4> textSourceColor(const std::vector<double>&a){if(a.size()!=4)throw std::runtime_error("Text color requires four channels");for(double v:a)if(!std::isfinite(v)||std::abs(v)>65536)throw std::runtime_error("Text color bound");bool normalized=true;for(size_t c=0;c<3;++c)normalized&=a[c]>=0&&a[c]<=1;std::array<float,4> out;for(size_t c=0;c<4;++c){const auto value=normalized?int(std::clamp(float(a[c]),0.f,1.f)*255):int(a[c]);out[c]=float(uint8_t(value))/255;}return out;}
const TextFont* TextFontBank::font(const std::string&name)const{auto i=fonts_.find(name);return i==fonts_.end()?nullptr:&i->second;}
std::string textFontName(const std::string&authored,bool emphasized,bool blockEmphasized){
 // Matching DLL/PDB: dynamic initializer 0x180015970..0x180015f31
 // constructs eight adjacent key/value pairs (48 bytes each); SetupFont
 // 0x1800a4cbf..0x1800a4e07 applies one case-sensitive lookup only when
 // ComponentDataDef UseEmphasizedFont(+0x33) && !BlockEmphasizedFont(+0x34).
 // These are the complete recovered constants, not naming conventions.
 // Provenance: native/analysis/font-emphasis-contract.json.
 static const std::map<std::string,std::string> mapping{
  {"AlegreyaSansRegular","AlegreyaSansExtraBold"},
  {"AlegreyaSansExtraBold","AlegreyaSansExtraBoldItalic"},
  {"AlegreyaSansSCRegular","AlegreyaSansSCBold"},
  {"AlegreyaSansSCBold","AlegreyaSansSCExtraBold"},
  {"AlegreyaSansSCLight","AlegreyaSansSCMedium"},
  {"SpectralSCLight","SpectralSCMedium"},
  {"SpectralSCExtraLight","SpectralSCLight"},
  {"SpectralSCMedium","SpectralSCBold"}};
 if(emphasized&&!blockEmphasized)if(const auto i=mapping.find(authored);i!=mapping.end())return i->second;
 return authored;
}
const std::string* TextFontBank::displayName(const std::string&key)const{auto i=localized_.find(key);return i==localized_.end()?nullptr:&i->second;}
const std::string* TextFontBank::description(const std::string&key)const{auto i=descriptions_.find(key);return i==descriptions_.end()?nullptr:&i->second;}
std::string TextFontBank::resolve(const std::string&key)const{auto i=localized_.find(key);if(i==localized_.end())throw std::runtime_error("Original English text key unavailable: "+key);return i->second;}
void TextFontBank::load(AssetStore&store,const SourceValue&catalog){
 if(!catalog.get("Schema")||catalog.get("Schema")->text()!="HadesOriginalText1")throw std::runtime_error("Original text catalog schema");std::map<std::string,TextFont> fonts;uint64_t memory=0;
 for(const auto&[name,d]:catalog.get("Fonts")->object()){if(fonts.size()>=64)throw std::runtime_error("Original font count bound");TextFont f;f.name=name;f.nativeSize=uint32_t(d.get("NativeSize")->number());if(const auto*remap=d.get("RemapSize")){const double r=remap->number();if(!(r>0&&r<=1024))throw std::runtime_error("Original font remap size bound");f.remapSize=uint32_t(r);}if(!f.nativeSize||f.nativeSize>1024)throw std::runtime_error("Original font native size bound");auto image=read(store,d.get("Image")->text(),64*1024*1024);if(image.get<uint32_t>()!=0x31524955)throw std::runtime_error("Original font UIR1 identity");f.width=image.get<uint32_t>();f.height=image.get<uint32_t>();const uint64_t bytes=uint64_t(f.width)*f.height*4;if(!f.width||!f.height||f.width>4096||f.height>4096||bytes!=image.b.size()-image.p||(memory+=bytes)>96*1024*1024)throw std::runtime_error("Original font atlas bound");f.rgba.assign(image.b.begin()+image.p,image.b.end());auto metrics=read(store,d.get("Metrics")->text(),4*1024*1024);if(metrics.get<uint32_t>()!=0x314e4655)throw std::runtime_error("Original font UFN1 identity");auto count=metrics.get<uint32_t>();f.lineSpacing=metrics.get<int32_t>();f.spacing=metrics.get<float>();f.fallback=metrics.get<uint32_t>();if(!count||count>65536||f.lineSpacing<=0||f.lineSpacing>2048||!std::isfinite(f.spacing)||std::abs(f.spacing)>1024||metrics.b.size()!=20+uint64_t(count)*48)throw std::runtime_error("Original font metric bound");for(uint32_t i=0;i<count;++i){auto cp=metrics.get<uint32_t>();TextGlyph g;for(auto&v:g.rect)v=metrics.get<int32_t>();for(auto&v:g.crop)v=metrics.get<int32_t>();for(auto&v:g.abc){v=metrics.get<float>();if(!std::isfinite(v)||std::abs(v)>4096)throw std::runtime_error("Original glyph metric bound");}if(cp>0x10ffff||g.rect[0]<0||g.rect[1]<0||g.rect[2]<0||g.rect[3]<0||uint64_t(g.rect[0])+g.rect[2]>f.width||uint64_t(g.rect[1])+g.rect[3]>f.height||!f.glyphs.emplace(cp,g).second)throw std::runtime_error("Original glyph rectangle/identity bound");}if(!f.glyphs.contains(f.fallback))throw std::runtime_error("Original font fallback unavailable");fonts.emplace(name,std::move(f));}
 std::map<std::string,std::string> localized;for(const auto&[k,v]:catalog.get("Localization")->object()){if(localized.size()>20000||k.size()>1024)throw std::runtime_error("Localization count/key bound");textCodepoints(v.text());localized.emplace(k,v.text());}std::map<std::string,std::string> descriptions;const auto*descriptionTable=catalog.get("Descriptions");if(descriptionTable)for(const auto&[k,v]:descriptionTable->object()){if(descriptions.size()>20000||k.size()>1024)throw std::runtime_error("Description count/key bound");textCodepoints(v.text());descriptions.emplace(k,v.text());}
 std::map<std::string,TextSymbol> symbols;if(const auto*table=catalog.get("Symbols"))for(const auto&[texture,d]:table->object()){if(symbols.size()>=256)throw std::runtime_error("Original text symbol count bound");auto image=read(store,d.get("Image")->text(),4*1024*1024);if(image.get<uint32_t>()!=0x31524955)throw std::runtime_error("Original symbol UIR1 identity");TextSymbol y;y.texture=texture;y.width=image.get<uint32_t>();y.height=image.get<uint32_t>();if(!y.width||!y.height||y.width>2048||y.height>2048||uint64_t(y.width)*y.height*4!=image.b.size()-image.p)throw std::runtime_error("Original symbol image bound");
  // Atlas.Read1f3d48..1f3d80 reads trimXY then originalWH; entry+18/+1c
  // stores original dimensions at1f4199/1f41a1. Native symbol layout uses
  // that full extent; atlas geometry retains the actual cropped pixels.
  auto dimension=[&](const char*key,bool zero){const double v=d.get(key)->number();if(!std::isfinite(v)||v!=std::trunc(v)||v<(zero?0:1)||v>2048)throw std::runtime_error("Original symbol atlas metadata bound: "+texture);return uint32_t(v);};
  y.originalWidth=dimension("OriginalWidth",false);y.originalHeight=dimension("OriginalHeight",false);y.trimX=dimension("TrimX",true);y.trimY=dimension("TrimY",true);
  if(uint64_t(y.trimX)+y.width>y.originalWidth||uint64_t(y.trimY)+y.height>y.originalHeight)throw std::runtime_error("Original symbol trim outside full extent: "+texture);
  for(const char*k:{"ScaleRatioX","ScaleRatioY"})if(d.get(k)->number()!=1)throw std::runtime_error("Scaled original text symbol unadmitted: "+texture);
  y.rgba.assign(image.b.begin()+image.p,image.b.end());symbols.emplace(texture,std::move(y));}
 fonts_=std::move(fonts);localized_=std::move(localized);descriptions_=std::move(descriptions);descriptionsExported_=descriptionTable!=nullptr;symbols_=std::move(symbols);subtitleStyle_=*catalog.get("SubtitleStyle");
}
std::vector<TextGlyphQuad> TextFontBank::layout(const TextLayoutRequest&r,std::vector<TextSymbolQuad>*symbolsOut,std::array<float,2>*measured)const{
 if(!std::isfinite(r.scale)||r.scale<=0||r.scale>32||!std::isfinite(r.width)||r.width<0||r.width>100000)throw std::runtime_error("Text layout numeric bound");
 struct Style{const TextFont*font;float scale,symbolUnits;int32_t top,bottom;};std::vector<Style>styles;
 auto addStyle=[&](const std::string&name,float size,float symbolScale,int32_t top,int32_t bottom,bool emphasized,bool blockEmphasized){const auto selected=textFontName(name,emphasized,blockEmphasized);const auto*f=font(selected);if(!f)throw std::runtime_error("Original font has not been loaded: "+selected);if(!std::isfinite(size)||size<=0||size>1024||!std::isfinite(symbolScale)||symbolScale<0||symbolScale>64)throw std::runtime_error("Text style numeric bound");styles.push_back({f,size/float(f->nativeSize)*r.scale,symbolScale*float(f->nativeSize)/float(f->remapSize?f->remapSize:f->nativeSize),top,bottom});};
 if(r.styles.empty())addStyle(r.font,r.size,r.textSymbolScale,r.lineSpacingTop,r.lineSpacingBottom,r.emphasized,r.blockEmphasized);else{if(r.styles.size()>1024)throw std::runtime_error("Text formatter count bound");for(const auto&style:r.styles)addStyle(style.font,style.size,style.textSymbolScale,style.lineSpacingTop,style.lineSpacingBottom,style.emphasized,style.blockEmphasized);}
 // ParseFormatting17c690 emits Line::mFormatBreak, not a newline. Keep each
 // glyph's real formatter in the combined paragraph, including wrap/centering.
 struct Item{uint32_t cp=0,style=0;const TextSymbol*symbol=nullptr;};std::vector<Item>items;
 if(r.pieces.empty()){for(auto cp:textCodepoints(r.text))items.push_back({cp,0,nullptr});}
 else for(const auto&piece:r.pieces){if(piece.style>=styles.size())throw std::runtime_error("Text formatter index outside paragraph");if(!piece.symbol){for(auto cp:textCodepoints(piece.text))items.push_back({cp,piece.style,nullptr});continue;}auto i=symbols_.find(piece.text);if(i==symbols_.end())throw std::runtime_error("Original text symbol texture not exported: "+piece.text);if(!symbolsOut)throw std::runtime_error("Original text symbol consumer unavailable");if(!styles[piece.style].font->remapSize)throw std::runtime_error("Original font remap size unavailable");items.push_back({0,piece.style,&i->second});}
 if(items.size()>16384)throw std::runtime_error("Text glyph bound");
 auto advance=[&](const Item&it){const auto&s=styles[it.style];if(it.symbol)return std::trunc(float(it.symbol->originalWidth)*s.symbolUnits*s.scale);const auto&g=glyph(*s.font,it.cp);return(g.abc[0]+g.abc[1]+g.abc[2]+s.font->spacing)*s.scale;};
 auto extent=[&](const std::vector<Item>&line){float x=0;for(const auto&it:line)x+=advance(it);if(!line.empty()&&!line.back().symbol){const auto&s=styles[line.back().style];x-=s.font->spacing*s.scale;}return x;};
 std::vector<std::vector<Item>>lines(1);std::vector<Item>word;
 auto flush=[&](){if(word.empty())return;auto candidate=lines.back();candidate.insert(candidate.end(),word.begin(),word.end());if(r.width>0&&!lines.back().empty()&&extent(candidate)>r.width){for(const auto&it:word)if(it.symbol)throw std::runtime_error("Original symbol line wrap unreconstructed");lines.emplace_back();}lines.back().insert(lines.back().end(),word.begin(),word.end());word.clear();};
 // Portable word wrapping remains the existing policy; native ABC/crop,
 // formatter-specific size, symbol advance and line height are source-based.
 for(const auto&it:items){if(it.symbol){flush();word.push_back(it);flush();continue;}if(it.cp=='\r')continue;if(it.cp=='\n'){flush();lines.emplace_back();}else if(it.cp==' '){flush();if(!lines.back().empty())lines.back().push_back(it);}else word.push_back(it);}flush();
 if(lines.size()>1024)throw std::runtime_error("Text line bound");std::vector<float>lineHeights;float height=0;
 // Draw 0x18017672c..0x1801767b0 compares the base box's pitch with
 // each Line::mFormatter's pitch. Top and Bottom are signed runtime ints,
 // added AFTER font line spacing * TextScale; they are not glyph offsets
 // or multiplied by the font/box scale. The existing portable paragraph
 // wrapping and vertical-origin policy remains unchanged.
 const auto pitch=[](const Style&s){return s.font->lineSpacing*s.scale+float(s.top)+float(s.bottom);};
 for(const auto&line:lines){float h=pitch(styles.front());for(const auto&it:line)h=std::max(h,pitch(styles[it.style]));lineHeights.push_back(h);height+=h;}
 const float y0=r.vertical==TextJustification::Center?-height*.5f:r.vertical==TextJustification::End?-height:0;std::vector<TextGlyphQuad>out;
 if(measured){float width=0;for(const auto&line:lines)width=std::max(width,extent(line));*measured={width,height};}
 float y=0;for(size_t line=0;line<lines.size();++line){float x=0;const float width=extent(lines[line]);const float x0=r.horizontal==TextJustification::Center?-width*.5f:r.horizontal==TextJustification::End?-width:0;
  for(const auto&it:lines[line]){const auto&s=styles[it.style];const auto&f=*s.font;if(it.symbol){const auto&symbol=*it.symbol;const float unit=s.symbolUnits*s.scale,cx=x0+x+advance(it)*.5f,cy=y0+y+lineHeights[line]*.5f;symbolsOut->push_back({cx+(float(symbol.trimX)-float(symbol.originalWidth)*.5f)*unit,cy+(float(symbol.trimY)-float(symbol.originalHeight)*.5f)*unit,float(symbol.width)*unit,float(symbol.height)*unit,symbol.texture,it.style});x+=advance(it);continue;}
   const auto&g=glyph(f,it.cp);if(g.rect[2]&&g.rect[3])out.push_back({x0+x+(g.abc[0]+g.crop[0])*s.scale,y0+y+g.crop[1]*s.scale,g.rect[2]*s.scale,g.rect[3]*s.scale,float(g.rect[0])/f.width,float(g.rect[1])/f.height,float(g.rect[0]+g.rect[2])/f.width,float(g.rect[1]+g.rect[3])/f.height,it.cp,it.style});x+=advance(it);}y+=lineHeights[line];}
 return out;
}
}
