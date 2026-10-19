#include "audio_world.hpp"
#include "audio_channels.hpp"
#include "audio_limiter.hpp"
#include "audio_sample_stream.hpp"
#include "audio_three_eq.hpp"
#include "audio_timeline.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
namespace hades {
namespace {
constexpr const char* Zero="00000000-0000-0000-0000-000000000000";
const SourceValue& field(const SourceValue& v,const char* k){auto* p=v.get(k);if(!p)throw std::runtime_error(std::string("Audio source field absent: ")+k);return *p;}
float number(const SourceValue& v){const double x=v.number();if(!std::isfinite(x)||std::fabs(x)>std::numeric_limits<float>::max())throw std::runtime_error("Audio source number outside finite range");return float(x);}
uint32_t integer(const SourceValue& v){const double x=v.number();if(x<0||x>UINT32_MAX||x!=std::trunc(x))throw std::runtime_error("Audio source unsigned integer outside bounds");return uint32_t(x);}
float dbGain(float x){return x>-80?std::pow(10.0f,x*0.05f):0;}
using Stereo=std::array<float,2>;
Stereo add(Stereo a,Stereo b){return {a[0]+b[0],a[1]+b[1]};}
Stereo gain(Stereo a,float g){return {a[0]*g,a[1]*g};}
float peak(Stereo x){return std::max(std::fabs(x[0]),std::fabs(x[1]));}
struct Curve {
 struct Point {double x;float y,shape;};std::vector<Point> points;
 void load(const SourceValue::Array& source,bool timeline){for(const auto& p:source)points.push_back({field(p,timeline?"x_raw_u32":"x_float").number(),number(field(p,"y")),number(field(p,"shape"))});if(points.empty())throw std::runtime_error("Empty source audio automation curve");}
 float at(double x)const{if(x<=points.front().x)return points.front().y;for(size_t i=1;i<points.size();++i)if(x<=points[i].x){const auto& a=points[i-1];const auto& b=points[i];if(b.x<=a.x)return b.y;double t=(x-a.x)/(b.x-a.x);if(a.shape!=0)t=std::pow(t,std::pow(4.0,double(a.shape)));return float(a.y+(b.y-a.y)*t);}return points.back().y;}
};
struct MixContext {
 std::vector<Stereo>& local;
 std::vector<Stereo>& global;
 const std::vector<float>& previousSidechains;
 std::vector<float>& nextSidechains;
 float externalGain=1;
 uint32_t tick=0; // Owning event timeline tick (timeline-owned bus automation).
};
struct Biquad {std::array<float,5> c{};Stereo z1{},z2{};
 void configure(float hz,float q,bool high){hz=std::clamp(hz,10.0f,22000.0f);q=std::max(q,0.01f);const float w=hz*6.28318530718f/48000,co=std::cos(w),alpha=std::sin(w)/(2*q),inv=1/(1+alpha);const float b1=(high?-(1+co):(1-co))*inv;c={b1*(high?-0.5f:0.5f),b1,b1*(high?-0.5f:0.5f),-2*co*inv,(1-alpha)*inv};}
 Stereo process(Stereo x){for(size_t i=0;i<2;++i){const float w=x[i]-c[3]*z1[i]-c[4]*z2[i];x[i]=c[0]*w+c[1]*z1[i]+c[2]*z2[i];z2[i]=z1[i];z1[i]=w;}return x;}
};
struct Comb {std::vector<Stereo> data;size_t at=0;Stereo low{};Stereo process(Stereo x,float feedback,float damp){auto y=data[at];for(size_t c=0;c<2;++c){low[c]+=(y[c]-low[c])*damp;data[at][c]=x[c]+low[c]*feedback;}at=(at+1)%data.size();return y;}};
struct Processor {
 std::string id,kind,returnBus,unsupported;int type=0;bool bypass=false,sidechain=false,active=false;
 std::vector<std::string> targets;std::vector<int> targetIndices;int detectorIndex=-1,returnIndex=-1;bool returnGlobal=false;std::vector<float> p;AudioLimiter limiter;AudioThreeEq eq;Biquad filter;
 float envelope=0,attack=0,release=0,makeup=1;std::vector<Stereo> delay;size_t delayAt=0;
 std::array<Comb,4> combs;float feedback=0,damping=0,wet=0,dry=0;
 void configure(const std::string& g,const SourceValue& v){id=g;kind=field(v,"kind").text();if(auto* b=v.get("legacy_bypass"))bypass=b->boolean();
  if(kind=="SEFB"){returnBus=field(v,"return_guid").text();p={dbGain(number(field(v,"level_db")))};return;}
  if(kind=="SCEF"){active=field(v,"active").boolean();for(const auto& t:field(v,"targets").array())targets.push_back(t.text());return;}
  if(kind!="BEFB")throw std::runtime_error("Unsupported original audio effect kind "+kind);
  type=int(integer(field(v,"type")));for(const auto& x:field(v,"parameters").array())p.push_back(std::holds_alternative<double>(x.value)?number(x):std::holds_alternative<bool>(x.value)?float(x.boolean()):0);
  const auto& args=field(v,"parameters").array();std::string error;
  if(type==11){if(p.size()!=4||!limiter.configure(48000,{p[0],p[1],p[2],args[3].boolean()},error))throw std::runtime_error("Limiter source parameters unsupported: "+error);}
  else if(type==28){if(p.size()!=6||!eq.configure(48000,{p[0],p[1],p[2],p[3],p[4],int(p[5])},error))throw std::runtime_error("Three-EQ source parameters unsupported: "+error);}
  else if(type==3||type==5){if(p.size()!=2)throw std::runtime_error("Source filter parameters missing");filter.configure(p[0],p[1],type==5);}
  else if(type==18){if(p.size()!=7)throw std::runtime_error("Source compressor parameters missing");sidechain=args[5].text()!="00000000";attack=std::exp(-1/(std::max(0.01f,p[2])*48));release=std::exp(-1/(std::max(0.01f,p[3])*48));makeup=dbGain(p[4]);}
  else if(type==19){if(p.size()!=13)throw std::runtime_error("Source reverb parameters missing");constexpr std::array<int,4> taps={1116,1188,1277,1356};for(size_t i=0;i<4;++i)combs[i].data.resize(size_t(taps[i]*48000/44100));feedback=std::pow(0.001f,float(combs[0].data.size())/(std::max(1.0f,p[0])*48));damping=1-std::exp(-6.2831853f*std::clamp(p[9],20.0f,20000.0f)/48000);wet=dbGain(p[11]);dry=dbGain(p[12]);delay.resize(size_t(std::clamp(p[1],0.0f,1000.0f)*48)+1);}
  else if(type==6){if(p.size()!=4)throw std::runtime_error("Source echo parameters missing");delay.resize(size_t(std::clamp(p[0],1.0f,5000.0f)*48));}
  else if(type==27){if(p.size()<14)throw std::runtime_error("Source panner parameters missing");}
  // Kept loud but per route: a global bus may carry a DSP the Android mixer
  // does not reproduce; createCue refuses any cue routed through it.
  else unsupported="Unsupported source DSP type "+std::to_string(type);
 }
 void compile(const std::map<std::string,int>& local,const std::map<std::string,int>& global,const std::map<std::string,int>& effectIndices){
  if(auto it=effectIndices.find(id);it!=effectIndices.end())detectorIndex=it->second;
  for(const auto& t:targets)if(auto it=effectIndices.find(t);it!=effectIndices.end())targetIndices.push_back(it->second);
  if(kind=="SEFB"){if(auto it=local.find(returnBus);it!=local.end())returnIndex=it->second;else if(auto it=global.find(returnBus);it!=global.end()){returnIndex=it->second;returnGlobal=true;}else throw std::runtime_error("Source audio return bus not compiled");}
 }
 Stereo process(Stereo x,const AudioSource& source,const AudioSource& listener,MixContext& context,std::string& error){
  const float detector=detectorIndex<0?0:context.previousSidechains[size_t(detectorIndex)];
  if(bypass)return x;
  if(kind=="SEFB"){auto& signal=(returnGlobal?context.global:context.local)[size_t(returnIndex)];signal=add(signal,gain(x,p[0]*(returnGlobal?context.externalGain:1)));return x;}
  if(kind=="SCEF"){if(active)for(int t:targetIndices)context.nextSidechains[size_t(t)]=std::max(context.nextSidechains[size_t(t)],peak(x));return x;}
  if(type==11){if(!limiter.process(x.data(),1,2,error))throw std::runtime_error(error);return x;}
  if(type==28){if(!eq.process(x.data(),1,2,error))throw std::runtime_error(error);return x;}
  if(type==3||type==5)return filter.process(x);
  if(type==18){const float input=sidechain?detector:peak(x);const float coeff=input>envelope?attack:release;envelope=input+(envelope-input)*coeff;const float inputDb=20*std::log10(std::max(envelope,1e-20f));const float reduction=inputDb>p[0]?(p[0]+(inputDb-p[0])/std::max(p[1],1.0f))-inputDb:0;return gain(x,makeup*dbGain(reduction));}
  if(type==6){const auto y=delay[delayAt];delay[delayAt]=add(x,gain(y,std::clamp(p[1]*.01f,0.0f,.999f)));delayAt=(delayAt+1)%delay.size();return add(gain(x,dbGain(p[2])),gain(y,dbGain(p[3])));}
  if(type==19){const auto pre=delay[delayAt];delay[delayAt]=x;delayAt=(delayAt+1)%delay.size();Stereo reverbed{};for(auto& comb:combs)reverbed=add(reverbed,comb.process(pre,feedback,damping));return add(gain(x,dry),gain(reverbed,wet*.25f));}
  if(type==27){float pan=std::clamp(p[1]/100,-1.0f,1.0f),volume=1;
   if(int(p[0])==2&&source.spatial){const float dx=source.x-listener.x,dy=source.y-listener.y,dz=source.z-listener.z;const float distance=std::sqrt(dx*dx+dy*dy+dz*dz);const float lo=std::max(1.0f,p[12]),hi=std::max(lo+1,p[13]);volume=distance<=lo?1:distance>=hi?0:lo/distance*(hi-distance)/(hi-lo);pan=std::clamp(dx/std::max(lo,distance),-1.0f,1.0f);}
   // Preserve centered stereo exactly; portable balance panner attenuates only
   // the opposite channel. Spatial attenuation is a documented Android choice.
   x[0]*=volume*(pan>0?std::sqrt(1-pan):1);x[1]*=volume*(pan<0?std::sqrt(1+pan):1);return x;
  }
  // Silent input stays silent for these unreproduced DSPs (no admitted route).
  if(!unsupported.empty()){if(x[0]==0&&x[1]==0)return x;throw std::runtime_error(unsupported+" received signal");}
  throw std::runtime_error("Unexecuted source audio processor");
 }
};
struct Bus {
 std::string id,destination;int index=-1,destinationIndex=-1;bool destinationGlobal=false;float gainDb=0,pitch=0,constantGain=1,userGain=1;std::vector<Processor> pre,post;SourceValue automation;
 // Automated mixer-strip property (0 volume dB, 1 pitch semitones). Each input
 // is a timeline- or parameter-owned controller curve; the value is resolved as
 // fmodstudio.dll PropertyInstance::refreshValue (0x180014940) with
 // ConflictResolver::resolveValue/processValue (0x180014fc0/0x1800146a0),
 // method 2 (additive): volume inputs are mapped to dB and summed, any input
 // <=-80 dB silences; pitch inputs are summed less inverse-map(0), then mapped.
 // An automated property replaces the static strip value. Mapping = the PROP
 // MappingModel (forward 0x1800d1fb0, inverse 0x1800d1f20). Snapshot and
 // modulator terms are not reproduced (none admitted on these properties).
 struct Automation {bool timeline=false;const float* parameter=nullptr;Curve curve;};
 struct Property {std::vector<Automation> inputs;Curve mapping;float inverseZero=0;
  float value(uint32_t tick)const{float acc=0;bool silent=false;
   for(const auto& a:inputs){const float x=a.curve.at(a.timeline?double(tick):double(*a.parameter));
    if(volume){const float v=mapping.at(x);if(silent||v<=-80)silent=true;else acc+=v;}else acc+=x-inverseZero;}
   if(volume)return silent?-std::numeric_limits<float>::infinity():acc;
   return mapping.at(acc+inverseZero);}
  bool volume=false;
 };
 Property volumeAutomation,pitchAutomation;
 bool automatedVolume()const{return !volumeAutomation.inputs.empty();}
 bool automatedPitch()const{return !pitchAutomation.inputs.empty();}
 // FMOD::propertyToPitch (0x180014800): clamp(-48,48) semitones -> 2^(x/12);
 // returned here in semitones so a route can sum its owners.
 float pitchSemitones(uint32_t tick)const{return automatedPitch()?std::clamp(pitchAutomation.value(tick),-48.0f,48.0f):pitch;}
 void configure(const std::string& name,const SourceValue& v,const SourceValue& effects){id=name;destination=field(v,"destination").text();const auto& strip=field(v,"mixer_strip");gainDb=number(field(strip,"volume"));constantGain=dbGain(gainDb);pitch=number(field(strip,"pitch"));if(auto* a=v.get("automation"))automation=*a;
  for(auto pair:{std::pair{"pre_fader_effects",&pre},std::pair{"post_fader_effects",&post}})for(const auto& g:field(v,pair.first).array()){Processor p;p.configure(g.text(),field(effects,g.text().c_str()));pair.second->push_back(std::move(p));}
 }
 void compile(const std::map<std::string,int>& local,const std::map<std::string,int>& global,const std::map<std::string,int>& effectIndices,const std::map<std::string,float>& parameters,bool hasTimeline){
  if(auto it=local.find(id);it!=local.end())index=it->second;else index=global.at(id);
  if(destination!=Zero){if(auto it=local.find(destination);it!=local.end())destinationIndex=it->second;else{destinationIndex=global.at(destination);destinationGlobal=true;}}
  for(auto* list:{&pre,&post})for(auto& fx:*list)fx.compile(local,global,effectIndices);
  if(std::holds_alternative<SourceValue::Array>(automation.value))for(const auto& a:automation.array()){
   const uint32_t property=integer(field(a,"property"));if(property>1)throw std::runtime_error("Unsupported source bus automation property "+std::to_string(property));
   auto& target=property==0?volumeAutomation:pitchAutomation;
   if(integer(field(a,"method"))!=2||integer(field(a,"type"))!=(property==0?1u:0u))throw std::runtime_error("Unsupported source bus automation combination");
   Automation item;const auto owner=field(a,"owner").text();
   if(owner=="timeline"){if(!hasTimeline)throw std::runtime_error("Timeline bus automation outside an event");item.timeline=true;item.curve.load(field(a,"points").array(),true);}
   else if(owner=="parameter"){const auto* key=a.get("parameter");if(!key)throw std::runtime_error("Unmapped bus automation owner");auto it=parameters.find(key->text());if(it==parameters.end())throw std::runtime_error("Missing source bus automation parameter");item.parameter=&it->second;item.curve.load(field(a,"points").array(),false);}
   else throw std::runtime_error("Unmapped bus automation owner "+owner);
   Curve mapping;for(const auto& point:field(a,"mapping").array()){const auto& xy=point.array();if(xy.size()!=2)throw std::runtime_error("Invalid source property mapping point");mapping.points.push_back({number(xy[0]),number(xy[1]),0});}
   if(mapping.points.empty())throw std::runtime_error("Empty source property mapping");
   if(target.inputs.empty()){target.mapping=mapping;target.volume=property==0;target.inverseZero=inverseMap(mapping,0);}
   else if(target.mapping.points.size()!=mapping.points.size())throw std::runtime_error("Conflicting source property mappings");
   target.inputs.push_back(std::move(item));}
 }
 // fmodstudio.dll 0x1800d1f20: inverse of the clamped piecewise-linear mapping.
 static float inverseMap(const Curve& m,float y){const auto& p=m.points;if(y<p.front().y)return float(p.front().x);size_t prev=0,next=0;while(next<p.size()&&y>=p[next].y){prev=next;++next;}
  if(next>=p.size())return float(p[prev].x);const auto& a=p[prev];const auto& b=p[next];return float((y-a.y)/(b.y-a.y)*(b.x-a.x)+a.x);}
 Stereo process(Stereo x,const AudioSource& source,const AudioSource& listener,MixContext& context,std::string& error){
  for(auto& p:pre)x=p.process(x,source,listener,context,error);
  // FMOD::propertyToVolume (0x180014830): <-80 dB silent, clamped at +10 dB.
  float volume=constantGain;if(automatedVolume()){const float db=volumeAutomation.value(context.tick);volume=db<=-80?0:std::pow(10.0f,std::min(db,10.0f)*0.05f);}
  volume*=userGain; // AudioCategory Bus::setVolume from ConfigOptions (setCategoryVolumes)
  x=gain(x,volume);for(auto& p:post)x=p.process(x,source,listener,context,error);return x;
 }

};
std::vector<std::string> ordered(const std::map<std::string,Bus>& buses){std::vector<std::string> post;std::map<std::string,int> state;
 std::function<void(const std::string&)> visit=[&](const std::string& id){if(state[id]==2)return;if(state[id]==1)return; // Source feedback sends are carried one output sample.
 state[id]=1;const auto& b=buses.at(id);if(buses.contains(b.destination))visit(b.destination);for(const auto* p:{&b.pre,&b.post})for(const auto& e:*p)if(e.kind=="SEFB"&&!e.bypass&&buses.contains(e.returnBus))visit(e.returnBus);state[id]=2;post.push_back(id);};
 for(const auto& [id,b]:buses){(void)b;visit(id);}std::reverse(post.begin(),post.end());return post;
}
}
struct AudioWorld::Impl {
 struct Instrument {std::unique_ptr<AudioSampleStream> stream;std::string bus;int busIndex=-1;uint32_t start=0,end=0,trim=0;float gain=1,pitch=1,semitones=0;std::vector<Curve> fades;
  // Route pitch: instrument + event-local bus strips (static) plus automated
  // bus pitch (dynamic, evaluated per timeline tick). A dynamic route plays by
  // integrating the per-tick rate (Android adaptation: FMOD's seek-under-pitch
  // after a timeline jump is not recovered; the position is re-integrated from
  // the region start, which equals the static formula for constant pitch).
  std::vector<const Bus*> automated;bool dynamic=false;double cursor=0;uint32_t lastTick=UINT32_MAX;
  double rate(uint32_t tick)const{float x=semitones;for(const auto* b:automated)x+=b->pitchSemitones(tick);return std::pow(2.0,double(x)/12);}
  double integrate(uint32_t tick)const{double c=double(trim)*rate(start);for(uint32_t t=start;t<tick;++t)c+=rate(t);return c;}
 };
 struct Event:AudioEventInstance {
  Impl* owner;uint32_t id=0;std::string name;AudioSource source;AudioTimeline timeline;AudioTimelineDefinition timelineDefinition;std::vector<SourceValue> playlists;size_t baseInstrumentCount=0;std::vector<std::string> selectedWaveforms;std::vector<Instrument> instruments;std::map<std::string,Bus> buses;std::vector<Bus*> order;std::vector<Stereo> mix;bool started=false,paused=false,stopped=false,voice=false;float volume=1;
  explicit Event(Impl* o):owner(o){}
  void route(Instrument& item,float sourceSemitones){float pitch=sourceSemitones;item.automated.clear();std::string cursor=item.bus;std::set<std::string> visited;
   while(buses.contains(cursor)&&visited.insert(cursor).second){const auto& b=buses.at(cursor);if(b.automatedPitch())item.automated.push_back(&b);else pitch+=b.pitch;cursor=b.destination;}
   item.semitones=pitch;item.dynamic=!item.automated.empty();item.pitch=std::pow(2.0f,pitch/12);item.lastTick=UINT32_MAX;
   if(!std::isfinite(item.pitch)||item.pitch<=0||item.pitch>1024)throw std::runtime_error("Source audio pitch outside finite supported range");}
  bool start(std::string& e)override{if(started&&!stopped){e="Repeated start on a playing source event requires explicit native restart behavior";return false;}
   if(!playlists.empty()){
    if(!timelineDefinition.parameters.empty()){e="Parameterized source playlist requires stable bus-automation rebinding";return false;}
    instruments.resize(baseInstrumentCount);selectedWaveforms.clear();auto definition=timelineDefinition;
    for(auto& parameter:definition.parameters){const auto i=timeline.parameters().find(parameter.name);if(i!=timeline.parameters().end())parameter.initial=i->second;}
    for(const auto& playlist:playlists){
     const auto& choices=field(playlist,"choices").array();const auto selected=owner->choosePlaylist(name+":"+field(playlist,"guid").text(),choices);
     const auto& choice=choices.at(selected);Instrument item;item.bus=field(playlist,"bus").text();item.busIndex=buses.at(item.bus).index;
     item.start=integer(field(playlist,"start"));item.trim=integer(field(choice,"left_trim_tick"));item.gain=dbGain(number(field(choice,"gain_db")));
     route(item,number(field(choice,"pitch_semitones")));
     const auto& sample=field(field(owner->catalog,"samples"),field(choice,"sample").text().c_str());
     AudioBankSample descriptor{field(sample,"asset").text(),int(integer(field(sample,"subsong"))),int(integer(field(sample,"channels"))),int(integer(field(sample,"sample_rate"))),int64_t(integer(field(sample,"frames"))),uint64_t(integer(field(sample,"encoded_file_bytes")))};
     double duration=std::ceil(double(descriptor.frames)*48000/descriptor.sampleRate/item.pitch)-item.trim;
     if(item.dynamic){
      // Triggered sound ends with its source: integrate the timeline-owned
      // route pitch from the trigger. Requires a deterministic linear timeline.
      if(!definition.transitions.empty())throw std::runtime_error("Playlist through automated pitch on a jumping timeline");
      for(const auto* b:item.automated)for(const auto& a:b->pitchAutomation.inputs)if(!a.timeline)throw std::runtime_error("Playlist through parameter-automated pitch");
      const double length=double(descriptor.frames)*48000/descriptor.sampleRate;double c=double(item.trim)*item.rate(item.start);uint64_t t=item.start;
      while(c<length){c+=item.rate(uint32_t(t));if(++t-item.start>(uint64_t(1)<<26))throw std::runtime_error("Playlist automated-pitch duration outside bounds");}
      duration=double(t-item.start);}
     if(duration<1||duration>UINT32_MAX-item.start){e="Playlist source duration outside bounded range";return false;}
     item.end=item.start+uint32_t(duration);definition.end=std::max(definition.end,item.end);
     item.stream=std::make_unique<AudioSampleStream>(owner->assets);if(!item.stream->open(descriptor,e))return false;
     selectedWaveforms.push_back(field(choice,"waveform_guid").text());instruments.push_back(std::move(item));
    }
    if(!timeline.load(definition,e))return false;
   }
   std::vector<AudioTimelineEvent> events;if(!timeline.start(events,e))return false;started=true;stopped=false;owner->markersFor(id,name,events);return true;}
  bool setPaused(bool p,std::string& e)override{paused=p;e.clear();return true;}
  bool stop(bool,std::string& e)override{stopped=true;e.clear();return true;}
  AudioParameterAccess parameter(const std::string& key,float& out,std::string& e)override{auto it=timeline.parameters().find(key);if(it==timeline.parameters().end())return AudioParameterAccess::Absent;out=it->second;e.clear();return AudioParameterAccess::Present;}
  AudioParameterAccess setParameter(const std::string& key,float v,std::string& e)override{auto result=timeline.setParameter(key,v,e);return result==AudioTimeline::ParameterResult::Changed?AudioParameterAccess::Present:result==AudioTimeline::ParameterResult::Absent?AudioParameterAccess::Absent:AudioParameterAccess::Failed;}
  bool setVolume(float v,std::string& e)override{volume=v;e.clear();return true;}
  bool frame(std::vector<Stereo>& global,std::vector<float>& sidechains,std::string& error){
   if(!started||paused||stopped||timeline.finished())return true;std::fill(mix.begin(),mix.end(),Stereo{});const uint32_t tick=timeline.tick();
   for(auto& i:instruments){if(tick<i.start||tick>=i.end){i.lastTick=UINT32_MAX;continue;}uint64_t sourceTick;
    if(!i.dynamic)sourceTick=uint64_t(std::llround((double(tick-i.start)+i.trim)*i.pitch));
    else{if(i.lastTick!=UINT32_MAX&&tick==i.lastTick+1)i.cursor+=i.rate(i.lastTick);else i.cursor=i.integrate(tick);i.lastTick=tick;sourceTick=uint64_t(std::llround(i.cursor));}Stereo x;if(!i.stream->stereoAtTick(sourceTick,x[0],x[1],error))return false;float fade=1;for(const auto& curve:i.fades)fade*=curve.at(tick);auto& input=mix[size_t(i.busIndex)];input=add(input,gain(x,i.gain*fade));}
   MixContext context{mix,global,owner->sidechains,sidechains,volume,tick};
   for(auto* b:order){const Stereo input=mix[size_t(b->index)];mix[size_t(b->index)]={};const Stereo x=b->process(input,source,owner->listener,context,error);if(b->destinationIndex<0)throw std::runtime_error("Local source audio bus has no output route");auto& output=(b->destinationGlobal?global:mix)[size_t(b->destinationIndex)];output=add(output,gain(x,b->destinationGlobal?volume:1));}
   std::vector<AudioTimelineEvent> events;if(!timeline.advance(1,events,error))return false;owner->markersFor(id,name,events);return true;
  }

 };
 AssetStore* assets;mutable std::mutex mutex;SourceValue catalog;struct CueReplacement{std::string name;bool valid=false;};std::map<std::string,CueReplacement> cueSwaps;bool loaded=false,faulted=false;std::string fault;AudioChannels channels;std::map<uint32_t,Event*> events;std::map<std::string,Bus> globalBuses;std::vector<Bus*> globalOrder;std::map<std::string,int> globalIndices,effectIndices;std::vector<float> sidechains,nextSidechains;std::vector<Stereo> globalMix,feedback;AudioSource listener;std::vector<AudioMusicMarker> markers;std::vector<AudioCompletion> completions;uint64_t rendered=0;
 struct VolumeTarget{float value=0,speed=0;bool stop=false,pause=false;};std::map<uint32_t,VolumeTarget> volumeTargets;
 struct PlaylistBag {std::vector<bool> used;size_t last=SIZE_MAX;};
 std::map<std::string,PlaylistBag> playlistBags;uint64_t audioRandom=0x4841444553415544ULL;
 uint32_t mixingControl=0;
 struct SnapshotBinding {std::string guid,kind;uint32_t index=0;Curve curve,mapping;Bus* bus=nullptr;Processor* effect=nullptr;float value=0;};
 std::vector<SnapshotBinding> reverbBindings;float lastReverb=std::numeric_limits<float>::quiet_NaN();
 void applyReverb(float value){
  if(value==lastReverb)return;
  for(auto& b:reverbBindings){const float mapped=b.mapping.at(b.curve.at(value));if(!std::isfinite(mapped))throw std::runtime_error("Nonfinite Primary snapshot target");
   if(b.bus)b.bus->constantGain=dbGain(mapped);
   else if(b.kind=="SEFB")b.effect->p[0]=dbGain(mapped);
   else{auto& p=*b.effect;p.p[0]=mapped;p.feedback=std::pow(0.001f,float(p.combs[0].data.size())/(std::max(1.0f,mapped)*48));}
   b.value=mapped;
  }lastReverb=value;
 }
 explicit Impl(AssetStore* a):assets(a){}
 size_t choosePlaylist(const std::string& key,const SourceValue::Array& choices){
  if(choices.empty()||choices.size()>256)throw std::runtime_error("Playlist choice count outside supported range");
  auto& bag=playlistBags[key];if(bag.used.size()!=choices.size())bag.used.assign(choices.size(),false);
  if(std::all_of(bag.used.begin(),bag.used.end(),[](bool x){return x;}))std::fill(bag.used.begin(),bag.used.end(),false);
  double total=0;for(size_t i=0;i<choices.size();++i)if(!bag.used[i]){const float weight=number(field(choices[i],"weight"));if(weight<=0)throw std::runtime_error("Nonpositive source playlist weight unsupported");total+=weight;}
  // Portable audio-only generator; never consumes the game's stream0. Source
  // SmartRandom weights remain exact; proprietary history/RNG is an adaptation.
  audioRandom^=audioRandom>>12;audioRandom^=audioRandom<<25;audioRandom^=audioRandom>>27;
  const uint64_t bits=audioRandom*2685821657736338717ULL;double remaining=double(bits>>11)*0x1.0p-53*total;
  size_t selected=SIZE_MAX;for(size_t i=0;i<choices.size();++i)if(!bag.used[i]){selected=i;remaining-=number(field(choices[i],"weight"));if(remaining<0)break;}
  if(selected==SIZE_MAX)throw std::runtime_error("No selectable source playlist waveform");bag.used[selected]=true;bag.last=selected;return selected;
 }
 bool fail(const std::string& e,std::string& out){faulted=true;fault=e;out=e;return false;}
 void markersFor(uint32_t id,const std::string& name,const std::vector<AudioTimelineEvent>& incoming){if(!name.starts_with("/Music/"))return;for(const auto& m:incoming)if(m.kind==AudioTimelineEvent::Kind::Marker){if(markers.size()>=65536)throw std::runtime_error("Undrained source music marker queue exceeds bound");markers.push_back({id,m.name,int32_t(m.tick/48)});}}
};
AudioWorld::AudioWorld(AssetStore* a):impl_(std::make_unique<Impl>(a)){}
AudioWorld::~AudioWorld()=default;
bool AudioWorld::resolveCue(const std::string& name,bool& valid,std::string& error,bool applySwaps)const{
 auto& w=*impl_;std::lock_guard lock(w.mutex);
 if(w.faulted){error=w.fault;return false;}if(!w.loaded){error="Original audio namespace is not loaded";return false;}
 std::string normalized=name;if(!normalized.empty()&&normalized.front()!='/'&&normalized.front()!='{')normalized='/'+normalized;
 if(applySwaps){auto it=w.cueSwaps.find(normalized);if(it!=w.cueSwaps.end()){if(!it->second.valid){valid=false;error.clear();return true;}normalized=it->second.name;}}
 if(const auto* table=w.catalog.get("event_namespace")){
  if(!normalized.empty()&&normalized.front()=='{'){error="Audio GUID lookup requires its native lookup provider";return false;}
  if(normalized.rfind("/VO/",0)==0){
   if(const auto* voices=w.catalog.get("voice_namespace")){
    std::string key=normalized.substr(normalized.find_last_of('/')+1);for(char& c:key)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
    if(voices->get(key)){valid=true;error.clear();return true;}
   }
   // Voice names absent from VO.h may still name a real Studio event.
  }
  valid=table->get(normalized)!=nullptr;error.clear();return true;
 }
 if(field(w.catalog,"events").get(normalized)){valid=true;error.clear();return true;}
 error="Original complete audio namespace is unavailable for "+normalized;return false;
}
size_t AudioWorld::allocatedChannelCount()const{auto& w=*impl_;std::lock_guard lock(w.mutex);return w.events.size();}
bool AudioWorld::voiceCue(const std::string& name,bool& voice,std::string& error)const{
 auto& w=*impl_;std::lock_guard lock(w.mutex);voice=false;if(w.faulted){error=w.fault;return false;}if(!w.loaded){error="Original audio catalog unavailable";return false;}
 if(name.rfind("/VO/",0)==0){const auto* table=w.catalog.get("voice_namespace");if(!table){error="Original VO.h namespace unavailable";return false;}std::string key=name.substr(name.find_last_of('/')+1);for(char& c:key)if(c>='A'&&c<='Z')c=char(c-'A'+'a');voice=table->get(key)!=nullptr;}
 error.clear();return true;
}
bool AudioWorld::subtitleText(const std::string& name,std::string& text,std::string& error)const{
 auto& w=*impl_;std::lock_guard lock(w.mutex);text.clear();if(w.faulted){error=w.fault;return false;}if(!w.loaded){error="Original audio catalog not loaded";return false;}
 if(const auto* lines=w.catalog.get("voice_subtitles"))if(const auto* line=lines->get(name)){text=field(*line,"text").text();error.clear();return true;}
 error="Original subtitle not exported: "+name;return false;
}
bool AudioWorld::loadCatalog(const SourceValue& root,std::string& error){auto& w=*impl_;std::lock_guard lock(w.mutex);try{
 if(w.loaded||!w.assets||field(root,"schema").text()!="hades-native-audio-events-v1"||integer(field(root,"timeline_rate"))!=48000)throw std::runtime_error("Invalid/unavailable original audio event catalog");
 w.catalog=root;const auto masterSha=field(field(field(root,"source_banks"),"MasterBank.bank"),"sha256").text();
 // MasterBank also defines events (actor voice templates). Their track,
 // master and input buses are per-event instances, not global mixer buses:
 // every bus on an instrument's route up to and including the event input bus.
 std::set<std::string> eventOwned;const auto& allBuses=field(root,"buses");
 for(const auto& [name,e]:field(root,"events").object()){(void)name;const auto input=field(e,"input_bus").text();eventOwned.insert(field(e,"master_bus").text());eventOwned.insert(input);
  std::vector<std::string> starts;for(const auto& i:field(e,"instruments").array())starts.push_back(field(i,"bus").text());if(const auto* pl=e.get("playlists"))for(const auto& p:pl->array())starts.push_back(field(p,"bus").text());
  for(auto cursor:starts){std::vector<std::string> route;std::set<std::string> seen;while(cursor!=Zero&&allBuses.get(cursor)&&seen.insert(cursor).second){route.push_back(cursor);if(cursor==input){eventOwned.insert(route.begin(),route.end());break;}cursor=field(field(allBuses,cursor.c_str()),"destination").text();}}}
 for(const auto& [id,v]:allBuses.object())if(!eventOwned.contains(id)&&field(field(v,"source"),"bank_sha256").text()==masterSha){Bus b;b.configure(id,v,field(root,"effects"));w.globalBuses.emplace(id,std::move(b));}
 int effectIndex=0;for(const auto& [id,v]:field(root,"effects").object()){(void)v;w.effectIndices[id]=effectIndex++;}w.sidechains.resize(size_t(effectIndex));w.nextSidechains.resize(size_t(effectIndex));int busIndex=0;for(const auto& [id,b]:w.globalBuses){(void)b;w.globalIndices[id]=busIndex++;}w.globalMix.resize(size_t(busIndex));w.feedback.resize(size_t(busIndex));for(auto& [id,b]:w.globalBuses){(void)id;b.compile({},w.globalIndices,w.effectIndices,{},false);}for(const auto& id:ordered(w.globalBuses))w.globalOrder.push_back(&w.globalBuses.at(id));
 if(const auto* control=root.get("mixing_control")){
  const auto& snapshot=field(*control,"reverb");if(field(snapshot,"schema").text()!="hades-primary-reverb-snapshot-v1"||!field(snapshot,"all_target_guids_verified").boolean())throw std::runtime_error("Invalid source Primary snapshot");
  for(const auto& raw:field(snapshot,"bindings").array()){
   Impl::SnapshotBinding b;b.guid=field(raw,"target_guid").text();b.kind=field(raw,"target_kind").text();b.index=integer(field(raw,"target_index"));b.value=number(field(raw,"base_value"));b.curve.load(field(raw,"curve").array(),false);
   for(const auto& point:field(raw,"mapping").array()){const auto& a=point.array();if(a.size()!=2)throw std::runtime_error("Invalid source snapshot mapping point");b.mapping.points.push_back({number(a[0]),number(a[1]),0});}
   if(b.mapping.points.empty())throw std::runtime_error("Empty source snapshot mapping");
   if(b.kind=="RBSB"){auto it=w.globalBuses.find(b.guid);if(it==w.globalBuses.end()||b.index!=0)throw std::runtime_error("Source snapshot return target not admitted");b.bus=&it->second;}
   else{
    for(auto& [id,bus]:w.globalBuses){(void)id;for(auto* list:{&bus.pre,&bus.post})for(auto& effect:*list)if(effect.id==b.guid){if(b.effect)throw std::runtime_error("Duplicate source snapshot effect target");b.effect=&effect;}}
    if(!b.effect||b.effect->kind!=b.kind||(b.kind=="SEFB"?b.index!=0:b.kind!="BEFB"||b.effect->type!=19||b.index!=1000))throw std::runtime_error("Unsupported source snapshot effect target");
   }
   w.reverbBindings.push_back(std::move(b));
  }
  if(w.reverbBindings.size()!=5)throw std::runtime_error("Source Reverb snapshot closure must contain five controls");
 }
 w.loaded=true;error.clear();return true;
 }catch(const std::exception& e){return w.fail(e.what(),error);}}
uint32_t AudioWorld::mixingControlId()const{auto& w=*impl_;std::lock_guard lock(w.mutex);return w.mixingControl;}
std::vector<AudioMixingTargetState> AudioWorld::mixingTargets()const{auto& w=*impl_;std::lock_guard lock(w.mutex);std::vector<AudioMixingTargetState> out;for(const auto& b:w.reverbBindings)out.push_back({b.guid,b.index,b.value});return out;}
bool AudioWorld::initializeMixingControl(std::string& error){
 std::string name;float initial=0;
 {auto& w=*impl_;std::lock_guard lock(w.mutex);if(w.faulted){error=w.fault;return false;}if(w.mixingControl)return true;const auto* definition=w.catalog.get("mixing_control");if(!definition){error.clear();return true;}name=field(*definition,"name").text();initial=number(field(field(*definition,"reverb"),"initial"));}
 uint32_t id=0;if(!createCue(name,{},id,error)||!id||!play(id,error))return false;
 {auto& w=*impl_;std::lock_guard lock(w.mutex);w.mixingControl=id;
  // Native LoadMixingParam calls47d57/47e44/48026/48110 preserve
  // these actual control values even while their other snapshot consumers
  // remain outside the bounded Reverb port (future writes are explicit errors).
  for(const auto& [name,value]:std::array<std::pair<const char*,float>,4>{{{"Health",1},{"Pause",0},{"Glitch",1},{"ControllerVoice",0}}})if(!w.channels.setParameter(id,name,value,0,error))return w.fail(error,error);
 }
 return setMixingParameter("Reverb",initial,error);
}
bool AudioWorld::setMixingParameter(const std::string& name,float value,std::string& error){
 auto& w=*impl_;std::lock_guard lock(w.mutex);try{
  if(w.faulted){error=w.fault;return false;}if(!std::isfinite(value))throw std::runtime_error("Nonfinite source mixing parameter");
  auto it=w.events.find(w.mixingControl);if(!w.mixingControl||it==w.events.end())throw std::runtime_error("Original persistent MixingControl event not admitted");
  if(!it->second->timeline.parameters().contains(name)){error.clear();return true;} // Native ignores missing-name status.
  if(name!="Reverb")throw std::runtime_error("Original Primary snapshot controls not yet ported for "+name);
  if(!w.channels.setParameter(w.mixingControl,name,value,0,error))return w.fail(error,error);
  w.applyReverb(it->second->timeline.parameters().at(name));error.clear();return true;
 }catch(const std::exception& e){return w.fail(e.what(),error);}
}
bool AudioWorld::swapCue(const std::string& from,const std::string& destination,std::string& error){
 // Native stores a destination descriptor snapshot, not another swap reference.
 bool valid=false;if(!resolveCue(destination,valid,error))return false;
 std::string name=destination;if(!name.empty()&&name.front()!='/'&&name.front()!='{')name='/'+name;
 auto& w=*impl_;std::lock_guard lock(w.mutex);w.cueSwaps[from]={name,valid};error.clear();return true;
}
std::vector<AudioChannelState> AudioWorld::ownedChannels(int32_t owner,uint64_t lifetime)const{
 auto& w=*impl_;std::lock_guard lock(w.mutex);std::vector<AudioChannelState> out;
 for(const auto& [id,e]:w.events)if(e->source.spatial&&e->source.ownerId==owner&&e->source.ownerLifetime==lifetime){AudioChannelState s;s.id=id;s.name=e->name;s.source=e->source;out.push_back(std::move(s));}return out;
}
std::vector<AudioChannelState> AudioWorld::attachedChannels()const{
 auto& w=*impl_;std::lock_guard lock(w.mutex);std::vector<AudioChannelState> out;
 for(const auto& [id,e]:w.events)if(e->source.spatial&&e->source.ownerId){AudioChannelState s;s.id=id;s.name=e->name;s.source=e->source;out.push_back(std::move(s));}return out;
}
bool AudioWorld::detachOwner(int32_t owner,uint64_t lifetime,float x,float y,std::string& error){
 auto& w=*impl_;std::lock_guard lock(w.mutex);if(w.faulted){error=w.fault;return false;}if(!std::isfinite(x)||!std::isfinite(y)){error="Nonfinite owner-death source location";return false;}
 for(auto& [id,e]:w.events){(void)id;if(e->source.spatial&&e->source.ownerId==owner&&e->source.ownerLifetime==lifetime){e->source.x=x;e->source.y=y;e->source.z=0;e->source.ownerId=0;e->source.ownerLifetime=0;}}error.clear();return true;
}
bool AudioWorld::createCue(const std::string& requested,const AudioSource& source,uint32_t& id,std::string& error,bool applySwaps){auto& w=*impl_;std::lock_guard lock(w.mutex);id=0;try{
 std::string name=requested;if(!name.empty()&&name.front()!='/'&&name.front()!='{')name='/'+name;
 if(applySwaps){const auto swap=w.cueSwaps.find(name);if(swap!=w.cueSwaps.end()){if(!swap->second.valid){error.clear();return true;}name=swap->second.name;}}
 if(w.faulted){error=w.fault;return false;}if(!w.loaded)throw std::runtime_error("Original compressed audio event catalog not loaded");if(name.empty()){error.clear();return true;}if(!std::isfinite(source.x)||!std::isfinite(source.y)||!std::isfinite(source.z))throw std::runtime_error("Nonfinite audio source");const auto* v=field(w.catalog,"events").get(name);if(!v){
  // The catalog is derived from static original-script/data coverage
  // (native/tools/audio_coverage.py); a cue outside it, or one whose source
  // graph the event player does not reproduce, fails loudly with the reason.
  const auto* why=w.catalog.get("unsupported_events");const auto* reason=why?why->get(name):nullptr;
  error=reason?"Original audio event graph unsupported: "+name+" ("+reason->text()+")":"Original audio event outside derived coverage catalog: "+name;return false;}if(w.events.size()>=256)throw std::runtime_error("Audio event lifetime count exceeds bounded initial implementation");
 auto event=std::make_unique<Impl::Event>(&w);event->name=name;event->source=source;
 if(name.rfind("/VO/",0)==0)if(const auto* voices=w.catalog.get("voice_namespace")){std::string key=name.substr(name.find_last_of('/')+1);for(char& c:key)if(c>='A'&&c<='Z')c=char(c-'A'+'a');event->voice=voices->get(key)!=nullptr;}
AudioTimelineDefinition tl;const auto& t=field(*v,"timeline");tl.end=integer(field(t,"end"));for(const auto& p:field(t,"parameters").array())tl.parameters.push_back({field(p,"name").text(),number(field(p,"minimum")),number(field(p,"maximum")),number(field(p,"initial"))});for(const auto& p:field(t,"markers").array())tl.markers.push_back({integer(field(p,"tick")),integer(field(p,"length")),field(p,"name").text()});for(const auto& p:field(t,"transitions").array()){AudioTransition a;a.tick=integer(field(p,"tick"));a.destination=integer(field(p,"destination"));a.conditional=field(p,"conditional").boolean();if(a.conditional){a.parameter=field(p,"parameter").text();a.minimum=number(field(p,"minimum"));a.maximum=number(field(p,"maximum"));}tl.transitions.push_back(a);}
 event->timelineDefinition=tl;if(!event->timeline.load(tl,error))return false;
 std::set<std::string> local;
 auto refuse=[&](const Bus& bus){for(const auto* list:{&bus.pre,&bus.post})for(const auto& p:*list)if(!p.unsupported.empty())throw std::runtime_error("Original audio event "+name+" routes through bus "+bus.id+" with "+p.unsupported);};
 std::function<void(const std::string&)> globalRoute=[&](const std::string& key){std::set<std::string> seen;std::vector<std::string> todo{key};while(!todo.empty()){auto k=todo.back();todo.pop_back();auto it=w.globalBuses.find(k);if(k==Zero||it==w.globalBuses.end()||!seen.insert(k).second)continue;refuse(it->second);todo.push_back(it->second.destination);for(const auto* list:{&it->second.pre,&it->second.post})for(const auto& p:*list)if(p.kind=="SEFB")todo.push_back(p.returnBus);}};
 std::function<void(const std::string&)> admitBus=[&](const std::string& key){if(key==Zero||local.contains(key))return;if(w.globalBuses.contains(key)){globalRoute(key);return;}local.insert(key);const auto& raw=field(field(w.catalog,"buses"),key.c_str());Bus bus;bus.configure(key,raw,field(w.catalog,"effects"));refuse(bus);const auto dst=bus.destination;for(const auto* list:{&bus.pre,&bus.post})for(const auto& p:*list)if(p.kind=="SEFB")admitBus(p.returnBus);event->buses.emplace(key,std::move(bus));admitBus(dst);};
 for(const auto& i:field(*v,"instruments").array()){Impl::Instrument item;item.bus=field(i,"bus").text();admitBus(item.bus);item.start=integer(field(i,"start"));item.end=integer(field(i,"end"));item.trim=integer(field(i,"left_trim_tick"));item.gain=dbGain(number(field(i,"gain_db")));for(const auto& fade:field(i,"fades").array()){if(integer(field(fade,"property"))!=4)throw std::runtime_error("Unsupported source instrument automation property");Curve curve;curve.load(field(fade,"points").array(),true);item.fades.push_back(std::move(curve));}item.semitones=number(field(i,"pitch_semitones"));
  const auto& sample=field(field(w.catalog,"samples"),field(i,"sample").text().c_str());AudioBankSample descriptor{field(sample,"asset").text(),int(integer(field(sample,"subsong"))),int(integer(field(sample,"channels"))),int(integer(field(sample,"sample_rate"))),int64_t(integer(field(sample,"frames"))),uint64_t(integer(field(sample,"encoded_file_bytes")))};item.stream=std::make_unique<AudioSampleStream>(w.assets);if(!item.stream->open(descriptor,error))return false;event->instruments.push_back(std::move(item));}
 event->baseInstrumentCount=event->instruments.size();
 if(const auto* playlists=v->get("playlists"))for(const auto& playlist:playlists->array()){
  if(integer(field(playlist,"mode"))!=2||integer(field(playlist,"selection"))!=1)throw std::runtime_error("Unsupported original playlist mode");
  admitBus(field(playlist,"bus").text());event->playlists.push_back(playlist);
 }
 std::map<std::string,int> indices;int index=0;for(const auto& [id,b]:event->buses){(void)b;indices[id]=index++;}event->mix.resize(size_t(index));for(auto& [id,b]:event->buses){(void)id;b.compile(indices,w.globalIndices,w.effectIndices,event->timeline.parameters(),true);}for(auto& i:event->instruments){i.busIndex=indices.at(i.bus);event->route(i,i.semitones);}for(const auto& id:ordered(event->buses))event->order.push_back(&event->buses.at(id));auto* pointer=event.get();if(!w.channels.admit(name,std::move(event),id,error))return false;pointer->id=id;w.events[id]=pointer;error.clear();return true;
 }catch(const std::exception& e){return w.fail(e.what(),error);}}
#define AUDIO_CHANNEL_OPERATION(name,call) bool AudioWorld::name(uint32_t id,std::string& error){auto& w=*impl_;std::lock_guard lock(w.mutex);if(w.faulted){error=w.fault;return false;}try{return w.channels.call(id,error);}catch(const std::exception& e){return w.fail(e.what(),error);}}
AUDIO_CHANNEL_OPERATION(play,play)
AUDIO_CHANNEL_OPERATION(pause,pause)
AUDIO_CHANNEL_OPERATION(resume,resume)
#undef AUDIO_CHANNEL_OPERATION
bool AudioWorld::stopAllGameSounds(std::string& error){
 // World::Reset 0x3160b5..11f stops the game sound channel group. Keep the
 // silent persistent Studio MixingControl snapshot controller; it is not a
 // waveform in that group. No completion/marker callback survives map reset.
 auto& w=*impl_;std::lock_guard lock(w.mutex);if(w.faulted){error=w.fault;return false;}
 for(const auto& channel:w.channels.snapshot())if(channel.id!=w.mixingControl){if(!w.channels.stop(channel.id,true,error))return false;w.events.erase(channel.id);w.channels.retireCompleted(channel.id);}
 w.markers.clear();w.completions.clear();error.clear();return true;
}
bool AudioWorld::stop(uint32_t id,bool immediate,std::string& error){auto& w=*impl_;std::lock_guard lock(w.mutex);if(w.faulted){error=w.fault;return false;}return w.channels.stop(id,immediate,error);}
bool AudioWorld::setParameter(uint32_t id,const std::string& key,float value,float duration,std::string& error){auto& w=*impl_;std::lock_guard lock(w.mutex);if(w.faulted){error=w.fault;return false;}
 try{if(id&&id==w.mixingControl){auto event=w.events.find(id);if(event==w.events.end()){error="Actual MixingControl channel disappeared";return w.fail(error,error);}if(!event->second->timeline.parameters().contains(key)){error.clear();return true;}if(key!="Reverb")return w.fail("Original Primary snapshot controls not yet ported for "+key,error);if(!w.channels.setParameter(id,key,value,duration,error))return w.fail(error,error);if(duration<=0)w.applyReverb(event->second->timeline.parameters().at(key));return true;}return w.channels.setParameter(id,key,value,duration,error);}catch(const std::exception& e){return w.fail(e.what(),error);}}
bool AudioWorld::setVolume(uint32_t id,float value,float duration,bool stopOnEnd,bool pauseOnEnd,std::string& error){auto& w=*impl_;std::lock_guard lock(w.mutex);if(w.faulted){error=w.fault;return false;}if(!std::isfinite(value)||!std::isfinite(duration)){error="Nonfinite audio volume target";return false;}if(duration>0){const float speed=1/duration;if(!std::isfinite(speed)){error="Audio volume target speed outside finite range";return false;}w.volumeTargets[id]={value,speed,stopOnEnd,pauseOnEnd};error.clear();return true;}return w.channels.setVolumeImmediate(id,value,error);}
bool AudioWorld::advanceControls(float elapsed,std::string& error){auto& w=*impl_;std::lock_guard lock(w.mutex);if(w.faulted){error=w.fault;return false;}if(!w.channels.advanceParameterTargets(elapsed,error))return w.fail(error,error);if(w.mixingControl){auto control=w.events.find(w.mixingControl);if(control==w.events.end())return w.fail("Actual MixingControl channel disappeared",error);try{w.applyReverb(control->second->timeline.parameters().at("Reverb"));}catch(const std::exception& e){return w.fail(e.what(),error);}}for(auto i=w.volumeTargets.begin();i!=w.volumeTargets.end();){auto it=w.events.find(i->first);if(it==w.events.end()){i=w.volumeTargets.erase(i);continue;}auto& event=*it->second;const auto target=i->second;const float step=elapsed*target.speed,difference=target.value-event.volume;if(!std::isfinite(step)||!std::isfinite(difference))return w.fail("Audio volume target arithmetic overflow",error);event.volume=std::fabs(difference)<=step?target.value:event.volume+(difference>0?step:-step);if(std::fabs(event.volume-target.value)<=1e-6f){event.volume=target.value;if(target.stop){if(!w.channels.stop(i->first,target.value==0,error))return w.fail(error,error);}else if(target.pause){if(!w.channels.pause(i->first,error))return w.fail(error,error);}i=w.volumeTargets.erase(i);}else ++i;}for(auto it=w.events.begin();it!=w.events.end();){if(it->second->stopped||it->second->timeline.finished()){const uint32_t id=it->first;if(w.completions.size()>=65536)return w.fail("Undrained audio completion queue exceeds bound",error);w.completions.push_back({id,it->second->name,it->second->voice});it=w.events.erase(it);w.volumeTargets.erase(id);w.channels.retireCompleted(id);}else ++it;}error.clear();return true;}
bool AudioWorld::setListener(float x,float y,float z,std::string& error){auto& w=*impl_;std::lock_guard lock(w.mutex);if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)){error="Nonfinite source audio listener";return false;}w.listener={false,x,y,z};error.clear();return true;}
bool AudioWorld::setSource(uint32_t id,const AudioSource& source,std::string& error){auto& w=*impl_;std::lock_guard lock(w.mutex);if(!std::isfinite(source.x)||!std::isfinite(source.y)||!std::isfinite(source.z)){error="Nonfinite source audio location";return false;}auto it=w.events.find(id);if(it!=w.events.end())it->second->source=source;error.clear();return true;}
bool AudioWorld::setCategoryVolumes(float master,float music,float fx,float dialogue,std::string& error){
    // AudioManager::LoadCategories 0x180046db0: bus GUID -> VolumeType
    // (4 Master, 3 Fx, 1 Music, 2 Dialogue). ConfigOptions default 1.0 each.
    // Evidence: .local/reports/native-audio-load-categories-static.json
    auto& w=*impl_;std::lock_guard lock(w.mutex);
    const std::pair<const char*,float> categories[]{{"a531b196-bb6e-4cd0-b5b3-420e81b5af19",master},{"9b320436-fb96-4de1-8229-63da4b1d52af",fx},
        {"764b5aef-b84a-486a-8484-b80b26a6cfb7",music},{"30b6f7ba-1ed3-4994-9f31-fe8554991a2e",fx},{"029a988e-8e1c-4f5c-a239-30ad2201d324",dialogue}};
    for(const auto& [guid,value]:categories){if(!std::isfinite(value)||value<0||value>1){error="Audio category volume outside [0,1]";return false;}
        auto it=w.globalBuses.find(guid);if(it==w.globalBuses.end()){error=std::string("Original audio category bus missing: ")+guid;return false;}it->second.userGain=value;}
    error.clear();return true;
}
bool AudioWorld::render(float* pcm,uint32_t frames,std::string& error){auto& w=*impl_;std::lock_guard lock(w.mutex);if(!pcm||frames>65536){error="Audio output buffer outside bounds";return false;}std::fill(pcm,pcm+size_t(frames)*2,0);if(w.faulted){error=w.fault;return false;}if(!w.loaded){error="Source audio graph not loaded";return false;}try{for(uint32_t i=0;i<frames;++i){std::copy(w.feedback.begin(),w.feedback.end(),w.globalMix.begin());std::fill(w.nextSidechains.begin(),w.nextSidechains.end(),0);for(auto& [id,event]:w.events){(void)id;if(!event->frame(w.globalMix,w.nextSidechains,error))return w.fail(error,error);}Stereo out{};MixContext context{w.globalMix,w.globalMix,w.sidechains,w.nextSidechains};for(auto* b:w.globalOrder){const Stereo input=w.globalMix[size_t(b->index)];w.globalMix[size_t(b->index)]={};const Stereo x=b->process(input,AudioSource{},w.listener,context,error);if(b->destinationIndex<0)out=add(out,x);else{auto& output=w.globalMix[size_t(b->destinationIndex)];output=add(output,x);}}std::copy(w.globalMix.begin(),w.globalMix.end(),w.feedback.begin());w.sidechains.swap(w.nextSidechains);if(!std::isfinite(out[0])||!std::isfinite(out[1]))throw std::runtime_error("Nonfinite mixed source audio");pcm[size_t(i)*2]=out[0];pcm[size_t(i)*2+1]=out[1];++w.rendered;}error.clear();return true;}catch(const std::exception& e){return w.fail(e.what(),error);}}
std::vector<AudioCompletion> AudioWorld::drainCompletions(){auto& w=*impl_;std::lock_guard lock(w.mutex);std::vector<AudioCompletion> out;out.swap(w.completions);return out;}
std::vector<AudioMusicMarker> AudioWorld::drainMarkers(){auto& w=*impl_;std::lock_guard lock(w.mutex);auto out=std::move(w.markers);w.markers.clear();return out;}
std::vector<uint32_t> AudioWorld::channelIds(const std::string& name)const{const auto& w=*impl_;std::lock_guard lock(w.mutex);std::vector<uint32_t> ids;for(const auto& [id,event]:w.events)if(event->name==name)ids.push_back(id);return ids;}
bool AudioWorld::channelState(uint32_t id,AudioChannelState& result)const{const auto& w=*impl_;std::lock_guard lock(w.mutex);auto it=w.events.find(id);if(it==w.events.end())return false;const auto& e=*it->second;result={id,e.name,e.source,e.started,e.paused,e.stopped,e.timeline.finished(),e.volume,e.selectedWaveforms};return true;}
size_t AudioWorld::liveEventCount(const std::string& name)const{const auto& w=*impl_;std::lock_guard lock(w.mutex);size_t count=0;for(const auto& [id,e]:w.events){(void)id;if(e->name==name&&!e->stopped&&!e->timeline.finished())++count;}return count;}
std::string AudioWorld::dump()const{const auto& w=*impl_;std::lock_guard lock(w.mutex);std::ostringstream s;s<<"AUDIO_WORLD loaded="<<w.loaded<<" events="<<w.events.size()<<" frames="<<w.rendered<<" globalBuses="<<w.globalBuses.size()<<" queuedMarkers="<<w.markers.size()<<" fault="<<w.faulted<<" dsp=Android-source-graph-adaptation resampler=32tap-windowed-sinc";if(w.faulted)s<<" reason="<<w.fault;return s.str();}
}
