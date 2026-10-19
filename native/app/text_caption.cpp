#include "text_layout.hpp"
#include "speech_world.hpp"
#include <stdexcept>
namespace hades {
bool TextCaptionWorld::consume(const std::vector<SpeechPresentation>&events,const TextFontBank&fonts,bool enabled,std::string&error){try{
 for(const auto&e:events){const size_t slot=e.request.cue.find("Zag")!=std::string::npos?0:1;
  if(!e.begin){if(auto callback=completionSlots_.find(e.channelId);callback!=completionSlots_.end()){slots_[callback->second].reset();completionSlots_.erase(callback);}continue;}
  if(!enabled||!e.request.useSubtitles||e.request.subtitleText.empty())continue;
  const auto&style=fonts.subtitleStyle();TextVisual v;v.sequence=e.channelId;v.font=style.get("Font")->text();v.text=e.request.subtitleText;v.caption=true;v.sourceWidget=slot?"SubtitlesB":"SubtitlesA";v.screenSpace=true;v.ownerX=float(style.get(slot?"X":"AX")->number());v.ownerY=float(style.get(slot?"Y":"AY")->number());
  // This is a real InGameUI GUIComponentTextBox, not a synthetic Thing/group.
  // SoundCue.Name's Zag test selects A; actual opening narration selects B.
  if(const auto*array=std::get_if<SourceValue::Array>(&e.request.subtitleColor.value)){std::vector<double>c;for(const auto&x:*array)c.push_back(x.number());v.color=textSourceColor(c);}
  else if(const auto*object=std::get_if<SourceValue::Object>(&e.request.subtitleColor.value)){(void)object;std::vector<double>c;for(const char*key:{"Red","Green","Blue","Alpha"}){const auto*p=e.request.subtitleColor.get(key);c.push_back(p?p->number():1);}v.color=textSourceColor(c);}
  else if(std::holds_alternative<std::monostate>(e.request.subtitleColor.value))v.color={1,1,1,1}; // PlaySpeech2576ad: ColorByName defaultFFFFFFFF.
  else throw std::runtime_error("Actual subtitle packed color has not been resolved");
  v.shadowColor={float(style.get("ShadowRed")->number()),float(style.get("ShadowGreen")->number()),float(style.get("ShadowBlue")->number()),float(style.get("ShadowAlpha")->number())};v.shadowX=float(style.get("ShadowOffsetX")->number());v.shadowY=float(style.get("ShadowOffsetY")->number());v.shadowBlur=float(style.get("ShadowBlur")->number());v.outlineColor={float(style.get("TextOutlineRed")->number()),float(style.get("TextOutlineGreen")->number()),float(style.get("TextOutlineBlue")->number()),1};v.outlineThickness=float(style.get("OutlineThickness")->number());v.shadowColor[3]*=v.color[3];v.outlineColor[3]*=v.color[3];TextLayoutRequest r;r.font=v.font;r.text=v.text;r.size=float(style.get("FontSize")->number());r.width=float(style.get("Width")->number());v.glyphs=fonts.layout(r);if(completionSlots_.size()>=4096)throw std::runtime_error("Caption completion callback bound");slots_[slot]=Slot{e.channelId,std::move(v)};completionSlots_[e.channelId]=slot;
 }return true;
 }catch(const std::exception&e){error=e.what();return false;}}
std::vector<TextVisual>TextCaptionWorld::visuals()const{std::vector<TextVisual>out;for(const auto&s:slots_)if(s)out.push_back(s->visual);return out;}
}
