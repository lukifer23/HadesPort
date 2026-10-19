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
std::vector<uint32_t> textCodepoints(const std::string&s){if(s.size()>65536)throw std::runtime_error("Text byte bound");std::vector<uint32_t> out;for(size_t p=0;p<s.size();){const auto lead=uint8_t(s[p++]);uint32_t cp=lead;unsigned n=0;uint32_t minimum=0;if(lead>=0xc2&&lead<=0xdf){n=1;cp=lead&31;minimum=0x80;}else if(lead>=0xe0&&lead<=0xef){n=2;cp=lead&15;minimum=0x800;}else if(lead>=0xf0&&lead<=0xf4){n=3;cp=lead&7;minimum=0x10000;}else if(lead>=128||!lead)throw std::runtime_error("Invalid text UTF8");if(n>s.size()-p)throw std::runtime_error("Truncated text UTF8");while(n--){auto b=uint8_t(s[p++]);if((b&0xc0)!=0x80)throw std::runtime_error("Invalid text UTF8 continuation");cp=(cp<<6)|(b&63);}if(cp<minimum||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))throw std::runtime_error("Noncanonical text UTF8");out.push_back(cp);if(out.size()>16384)throw std::runtime_error("Text glyph bound");}return out;}
std::array<float,4> textSourceColor(const std::vector<double>&a){if(a.size()!=4)throw std::runtime_error("Text color requires four channels");for(double v:a)if(!std::isfinite(v)||std::abs(v)>65536)throw std::runtime_error("Text color bound");bool normalized=true;for(size_t c=0;c<3;++c)normalized&=a[c]>=0&&a[c]<=1;std::array<float,4> out;for(size_t c=0;c<4;++c){const auto value=normalized?int(std::clamp(float(a[c]),0.f,1.f)*255):int(a[c]);out[c]=float(uint8_t(value))/255;}return out;}
const TextFont* TextFontBank::font(const std::string&name)const{auto i=fonts_.find(name);return i==fonts_.end()?nullptr:&i->second;}
const std::string* TextFontBank::displayName(const std::string&key)const{auto i=localized_.find(key);return i==localized_.end()?nullptr:&i->second;}
std::string TextFontBank::resolve(const std::string&key)const{auto i=localized_.find(key);if(i==localized_.end())throw std::runtime_error("Original English text key unavailable: "+key);return i->second;}
void TextFontBank::load(AssetStore&store,const SourceValue&catalog){
 if(!catalog.get("Schema")||catalog.get("Schema")->text()!="HadesOriginalText1")throw std::runtime_error("Original text catalog schema");std::map<std::string,TextFont> fonts;uint64_t memory=0;
 for(const auto&[name,d]:catalog.get("Fonts")->object()){if(fonts.size()>=64)throw std::runtime_error("Original font count bound");TextFont f;f.name=name;f.nativeSize=uint32_t(d.get("NativeSize")->number());if(const auto*remap=d.get("RemapSize")){const double r=remap->number();if(!(r>0&&r<=1024))throw std::runtime_error("Original font remap size bound");f.remapSize=uint32_t(r);}if(!f.nativeSize||f.nativeSize>1024)throw std::runtime_error("Original font native size bound");auto image=read(store,d.get("Image")->text(),64*1024*1024);if(image.get<uint32_t>()!=0x31524955)throw std::runtime_error("Original font UIR1 identity");f.width=image.get<uint32_t>();f.height=image.get<uint32_t>();const uint64_t bytes=uint64_t(f.width)*f.height*4;if(!f.width||!f.height||f.width>4096||f.height>4096||bytes!=image.b.size()-image.p||(memory+=bytes)>96*1024*1024)throw std::runtime_error("Original font atlas bound");f.rgba.assign(image.b.begin()+image.p,image.b.end());auto metrics=read(store,d.get("Metrics")->text(),4*1024*1024);if(metrics.get<uint32_t>()!=0x314e4655)throw std::runtime_error("Original font UFN1 identity");auto count=metrics.get<uint32_t>();f.lineSpacing=metrics.get<int32_t>();f.spacing=metrics.get<float>();f.fallback=metrics.get<uint32_t>();if(!count||count>65536||f.lineSpacing<=0||f.lineSpacing>2048||!std::isfinite(f.spacing)||std::abs(f.spacing)>1024||metrics.b.size()!=20+uint64_t(count)*48)throw std::runtime_error("Original font metric bound");for(uint32_t i=0;i<count;++i){auto cp=metrics.get<uint32_t>();TextGlyph g;for(auto&v:g.rect)v=metrics.get<int32_t>();for(auto&v:g.crop)v=metrics.get<int32_t>();for(auto&v:g.abc){v=metrics.get<float>();if(!std::isfinite(v)||std::abs(v)>4096)throw std::runtime_error("Original glyph metric bound");}if(cp>0x10ffff||g.rect[0]<0||g.rect[1]<0||g.rect[2]<0||g.rect[3]<0||uint64_t(g.rect[0])+g.rect[2]>f.width||uint64_t(g.rect[1])+g.rect[3]>f.height||!f.glyphs.emplace(cp,g).second)throw std::runtime_error("Original glyph rectangle/identity bound");}if(!f.glyphs.contains(f.fallback))throw std::runtime_error("Original font fallback unavailable");fonts.emplace(name,std::move(f));}
 std::map<std::string,std::string> localized;for(const auto&[k,v]:catalog.get("Localization")->object()){if(localized.size()>20000||k.size()>1024)throw std::runtime_error("Localization count/key bound");textCodepoints(v.text());localized.emplace(k,v.text());}std::map<std::string,TextSymbol> symbols;if(const auto*table=catalog.get("Symbols"))for(const auto&[texture,d]:table->object()){if(symbols.size()>=256)throw std::runtime_error("Original text symbol count bound");auto image=read(store,d.get("Image")->text(),4*1024*1024);if(image.get<uint32_t>()!=0x31524955)throw std::runtime_error("Original symbol UIR1 identity");TextSymbol y;y.texture=texture;y.width=image.get<uint32_t>();y.height=image.get<uint32_t>();if(!y.width||!y.height||y.width>2048||y.height>2048||uint64_t(y.width)*y.height*4!=image.b.size()-image.p)throw std::runtime_error("Original symbol image bound");
  // Only untrimmed, unscaled atlas entries are admitted: ParseImage's entry width
  // (TextureAtlasEntry+0x18) then equals both the packed and original width.
  for(const char*k:{"TrimX","TrimY"})if(d.get(k)->number()!=0)throw std::runtime_error("Trimmed original text symbol unadmitted: "+texture);
  for(const char*k:{"ScaleRatioX","ScaleRatioY"})if(d.get(k)->number()!=1)throw std::runtime_error("Scaled original text symbol unadmitted: "+texture);
  if(d.get("OriginalWidth")->number()!=y.width||d.get("OriginalHeight")->number()!=y.height)throw std::runtime_error("Original text symbol extent mismatch: "+texture);
  y.rgba.assign(image.b.begin()+image.p,image.b.end());symbols.emplace(texture,std::move(y));}
 fonts_=std::move(fonts);localized_=std::move(localized);symbols_=std::move(symbols);subtitleStyle_=*catalog.get("SubtitleStyle");
}
std::vector<TextGlyphQuad> TextFontBank::layout(const TextLayoutRequest&r,std::vector<TextSymbolQuad>*symbolsOut,std::array<float,2>*measured)const{
 const auto*f=font(r.font);if(!f)throw std::runtime_error("Original font has not been loaded: "+r.font);if(!std::isfinite(r.size)||r.size<=0||r.size>1024||!std::isfinite(r.scale)||r.scale<=0||r.scale>32||!std::isfinite(r.width)||r.width<0||r.width>100000||!std::isfinite(r.textSymbolScale)||r.textSymbolScale<0||r.textSymbolScale>64)throw std::runtime_error("Text layout numeric bound");const float scale=r.size/float(f->nativeSize)*r.scale;
 // Items: codepoint (symbol<0) or inline symbol. Symbol advance follows
 // ParseImage: entryWidth*TextSymbolScale(data)*TextSymbolScale(); the
 // latter uses Fonts::GetFont FontSize/remap where text uses FontSize/native.
 struct Item{uint32_t cp=0;int symbol=-1;};std::vector<const TextSymbol*> used;std::vector<Item> items;
 if(r.pieces.empty()){for(auto cp:textCodepoints(r.text))items.push_back({cp,-1});}
 else for(const auto&piece:r.pieces){if(!piece.symbol){for(auto cp:textCodepoints(piece.text))items.push_back({cp,-1});continue;}
  auto i=symbols_.find(piece.text);if(i==symbols_.end())throw std::runtime_error("Original text symbol texture not exported: "+piece.text);if(!symbolsOut)throw std::runtime_error("Original text symbol consumer unavailable");if(!f->remapSize)throw std::runtime_error("Original font remap size unavailable: "+r.font);used.push_back(&i->second);items.push_back({0,int(used.size()-1)});}
 if(items.size()>16384)throw std::runtime_error("Text glyph bound");
 const float symbolUnits=r.textSymbolScale*float(f->nativeSize)/float(f->remapSize?f->remapSize:f->nativeSize);
 if(!used.empty()&&r.horizontal!=TextJustification::Start)throw std::runtime_error("Original justified inline symbol width unreconstructed");
 // DrawLine: pen advances trunc(width*symbolScale) (two half-steps of 0.5);
 // the sprite is drawn centred, size width*symbolScale, on the line centre.
 auto advance=[&](const Item&it){if(it.symbol>=0)return std::trunc(float(used[it.symbol]->width)*symbolUnits*scale)/scale;const auto&g=glyph(*f,it.cp);return g.abc[0]+g.abc[1]+g.abc[2]+f->spacing;};
 auto extent=[&](const std::vector<Item>&line){float x=0;for(const auto&it:line)x+=advance(it);return line.empty()||line.back().symbol>=0?x:x-f->spacing;};
 std::vector<std::vector<Item>>lines(1);std::vector<Item> word;
 auto flush=[&](){if(word.empty())return;auto candidate=lines.back();candidate.insert(candidate.end(),word.begin(),word.end());if(r.width>0&&!lines.back().empty()&&extent(candidate)*scale>r.width){for(const auto&it:word)if(it.symbol>=0)throw std::runtime_error("Original symbol line wrap unreconstructed");lines.emplace_back();}lines.back().insert(lines.back().end(),word.begin(),word.end());word.clear();};
 // Portable word wrapping preserves original glyph metrics; explicit newlines
 // follow native DrawInto. Symbols are atomic words (Parse break set holds '@').
 for(const auto&it:items){if(it.symbol>=0){flush();word.push_back(it);flush();continue;}if(it.cp=='\r')continue;if(it.cp=='\n'){flush();lines.emplace_back();}else if(it.cp==' '){flush();if(!lines.back().empty())lines.back().push_back(it);}else word.push_back(it);}flush();
 if(lines.size()>1024)throw std::runtime_error("Text line bound");const float height=float(lines.size())*f->lineSpacing*scale;const float y0=r.vertical==TextJustification::Center?-height*.5f:r.vertical==TextJustification::End?-height:0;std::vector<TextGlyphQuad>out;
 if(measured){float width=0;for(const auto&line:lines)width=std::max(width,extent(line)*scale);*measured={width,height};}
 for(size_t line=0;line<lines.size();++line){float x=0;const float width=extent(lines[line])*scale;const float x0=r.horizontal==TextJustification::Center?-width*.5f:r.horizontal==TextJustification::End?-width:0;
  for(const auto&it:lines[line]){if(it.symbol>=0){const auto&y=*used[it.symbol];const float w=float(y.width)*symbolUnits*scale,h=float(y.height)*symbolUnits*scale;
    // DrawLine: centre (pen+advance/2, line centre); size width/height*symbolScale.
    const float cx=x0+(x+advance(it)*.5f)*scale,cy=y0+(float(line)+.5f)*f->lineSpacing*scale;
    symbolsOut->push_back({cx-w*.5f,cy-h*.5f,w,h,y.texture});x+=advance(it);continue;}
   const auto&g=glyph(*f,it.cp);if(g.rect[2]&&g.rect[3])out.push_back({x0+(x+g.abc[0]+g.crop[0])*scale,y0+(float(line)*f->lineSpacing+g.crop[1])*scale,g.rect[2]*scale,g.rect[3]*scale,float(g.rect[0])/f->width,float(g.rect[1])/f->height,float(g.rect[0]+g.rect[2])/f->width,float(g.rect[1]+g.rect[3])/f->height,it.cp});x+=advance(it);}}
 return out;
}
}
