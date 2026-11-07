// Host-only real source diagnostic. Never replace providers, definitions, RNG
// or movie pixels; run separately from room activation/progression.
#include "../app/lua_runtime.hpp"
#include "../app/platform_log.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#ifndef __ANDROID__
namespace {
void requireScreen(bool condition,const std::string& detail){if(!condition)throw std::runtime_error(detail);}
float scalar(const hades::SourceValue& d,const char* key,float fallback=0){const auto* v=d.get(key);return v?float(v->number()):fallback;}
bool flag(const hades::SourceValue& d,const char* key){const auto* v=d.get(key);return v&&v->boolean();}
std::string text(const hades::SourceValue& d,const char* key){const auto* v=d.get(key);return v?v->text():std::string{};}
bool near(float a,float b){return std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=1e-4f*std::max({1.f,std::abs(a),std::abs(b)});}
struct ScreenProbeCleanup {std::function<void()> run;~ScreenProbeCleanup(){run();}};
}

bool ScriptRuntime::probeScreenAnimations(){
    if(!state_||!mainLoaded_||!gameLoaded_||!screenAnimationBank_||roomBooted_||roomActivated_||!screenAnimations_.things().empty()){
        error_="Screen probe requires initialized original scripts and a fresh unbooted screen world";return false;
    }
    lua_State* const L=state_;const int top=lua_gettop(L);const uint64_t savedFrame=worldFrame_;const double savedTime=screenTime_;
    std::vector<int32_t> owners;uint32_t steps=0,movieCases=0,movieFrames=0,chainCases=0,fallbackCases=0,holdCases=0,symbolCases=0,helperLayers=0,createdFx=0,randomCases=0,randomLoops=0,colorCases=0;
    uint64_t decodedBytes=0;std::string detail;
    ScreenProbeCleanup cleanup{[&]{
        // Use the real disposal route, including foreign child/owner teardown.
        // The diagnostic consumes the genuine shared RNG/audio streams and is
        // deliberately process-isolated; it does not promise a reusable VM.
        for(auto id:owners){std::string ignored;(void)screenAnimations_.markDead(id,ignored);}
        std::string ignored;(void)screenAnimations_.disposePending(screenAnimationSound(),ignored);
        worldFrame_=savedFrame;screenTime_=savedTime;lua_settop(L,top);
    }};
    try{
        auto catalog=[&](const char* name,size_t limit){
            auto file=assets_->open(name);requireScreen(file&&file->size()<=limit,std::string("Missing/oversized actual catalog: ")+name);
            std::vector<char> bytes(file->size());size_t at=0;while(at<bytes.size()){const auto n=file->read(bytes.data()+at,bytes.size()-at);requireScreen(n>0,"Truncated actual screen diagnostic catalog");at+=n;}
            if(luaL_loadbufferx(L,bytes.data(),bytes.size(),name,"t")!=LUA_OK||lua_pcall(L,0,1,0)!=LUA_OK)throw std::runtime_error(lua_tostring(L,-1));
            hades::SourceValue out;requireScreen(hades::readSourceValue(L,-1,out,detail),detail);lua_settop(L,top);return out;
        };
        const auto screen=catalog("screen-animations.lua",2*1024*1024),actors=catalog("actors.lua",32*1024*1024);
        const auto* clips=screen.get("clips");const auto* definitions=actors.get("definitions");requireScreen(clips&&definitions,"Actual screen/actor catalog definitions unavailable");
        std::string obstacle,sourceGraphic;
        // Select a genuine source-defined static anchor, not a fabricated
        // Thing or a mutable animation name standing in for ThingData.Graphic.
        for(const auto& [key,row]:definitions->object()){
            const auto* d=actorWorld_.definition(key);if(!d||d->kind!="Obstacle")continue;
            const auto& p=d->properties;const auto* use=p.path("Thing.UseScreenLocation"),*life=p.path("NeedsLife"),*graphic=p.path("Thing.Graphic"),*alpha=p.path("Thing.Color.Alpha"),*grip=p.path("Thing.Grip");
            if(!use||!use->boolean()||!life||life->boolean()||!graphic||graphic->text().empty()||!alpha||alpha->number()<=0||!grip||grip->number()<99999)continue;
            const auto* animation=screenAnimations_.definition(graphic->text());if(!animation||text(*animation,"Type")!="ConstantNoUpdate"||!text(*animation,"ChildAnimation").empty()||!text(*animation,"Sound").empty()||animation->get("CreateAnimation")||animation->get("CreateAnimations"))continue;
            if(obstacle.empty()||d->name.size()<obstacle.size()){obstacle=d->name;sourceGraphic=graphic->text();}
            (void)row;
        }
        requireScreen(!obstacle.empty(),"No actual static sourceGraphic screen anchor admitted");
        auto create=[&](){
            lua_getglobal(L,"CreateScreenObstacle");requireScreen(lua_isfunction(L,-1),"Original CreateScreenObstacle not loaded");lua_newtable(L);lua_pushlstring(L,obstacle.data(),obstacle.size());lua_setfield(L,-2,"Name");
            // Explicit valid coordinates avoid depending on pre-room camera/UI
            // constants. Original helper still selects its actual Events group.
            lua_pushnumber(L,0);lua_setfield(L,-2,"X");lua_pushnumber(L,0);lua_setfield(L,-2,"Y");
            if(lua_pcall(L,1,1,0)!=LUA_OK)throw std::runtime_error(lua_tostring(L,-1));requireScreen(lua_isnumber(L,-1),"Original CreateScreenObstacle returned no Thing id");const auto id=int32_t(lua_tointeger(L,-1));lua_settop(L,top);owners.push_back(id);
            const auto* s=screenAnimations_.thing(id);requireScreen(s&&s->body&&s->body==findObjectSnapshot(L,id)&&s->sourceGraphic==sourceGraphic&&s->body->entityHandle&&!s->body->queryProxy,"Registered source screen Thing lifetime/Graphic mismatch");return id;
        };
        auto set=[&](int32_t id,const std::string& name){
            lua_getglobal(L,"SetAnimation");requireScreen(lua_isfunction(L,-1),"Registered SetAnimation unavailable");lua_newtable(L);lua_pushinteger(L,id);lua_setfield(L,-2,"DestinationId");lua_pushlstring(L,name.data(),name.size());lua_setfield(L,-2,"Name");
            if(lua_pcall(L,1,0,0)!=LUA_OK)throw std::runtime_error(lua_tostring(L,-1));lua_settop(L,top);const auto* s=screenAnimations_.thing(id);requireScreen(s&&s->primary&&s->body->animation==name,"Registered SetAnimation did not select actual primary: "+name);
        };
        auto dispose=[&](int32_t id){requireScreen(screenAnimations_.markDead(id,detail),detail);requireScreen(screenAnimations_.disposePending(screenAnimationSound(),detail),detail);const auto* s=screenAnimations_.thing(id);requireScreen(s&&s->dead&&!s->primary&&!s->body->alive&&!s->body->entityHandle,"Actual screen disposal retained live owner/allocation");};
        auto step=[&](float elapsed){
            requireScreen(std::isfinite(elapsed)&&elapsed>=0&&elapsed<=1,"Screen probe component elapsed bound");requireScreen(++steps<=2048,"Screen diagnostic component step bound");++worldFrame_;screenTime_+=elapsed;
            requireScreen(advanceScreenAnimations(elapsed,detail),detail);
            // Render real mixer PCM instead of completing cues with a synthetic
            // timer. No hardware output or game callback/progression is run.
            requireScreen(audio_.advanceControls(elapsed,detail),detail);const auto frames=uint32_t(std::lround(double(elapsed)*48000));std::vector<float> pcm(size_t(frames)*2);
            if(frames)requireScreen(audio_.render(pcm.data(),frames,detail),detail);
        };
        auto visual=[&](int32_t id,uint64_t handle){hades::ActorVisual v;requireScreen(screenAnimations_.visual(id,handle,v,detail),detail);requireScreen(v.sourceBank==screenAnimationBank_.get()&&v.frame.decoder==screenAnimationBank_.get()&&std::isfinite(v.angle)&&std::isfinite(v.scale)&&v.scale>0,"Screen visual bank/transform invalid");for(float n:v.localRect)requireScreen(std::isfinite(n),"Screen visual rectangle nonfinite");for(float n:v.color)requireScreen(std::isfinite(n)&&n>=0&&n<=1,"Screen visual color bound");return v;};
        // Reach the genuine Animation SoundAllowedAfterFrame gate with actual
        // component clock ticks; retain the runtime's existing sound providers.
        step(1.f/60);step(1.f/60);
        for(const auto& [name,row]:clips->object()){
            const auto* binding=row.get("movie_binding");if(!binding)continue;++movieCases;requireScreen(movieCases<=16,"Screen movie diagnostic source class bound");
            const auto* d=screenAnimations_.definition(name);const auto index=screenAnimationBank_->find(name);requireScreen(d&&index&&text(*d,"Type")=="Book"&&text(*d,"VideoTexture")==text(*binding,"video_texture"),"Actual source MovieBook binding mismatch");
            const auto& clip=screenAnimationBank_->clips().at(*index);const auto count=uint32_t(scalar(*binding,"source_frame_count"));requireScreen(count>0&&count<=1000&&clip.slides.size()==count&&clip.originalAngleIndices==std::vector<uint32_t>{0},"Actual MovieBook source schedule/angles mismatch");
            const auto& canvas=binding->get("canvas")->array();requireScreen(canvas.size()==2,"Actual movie source canvas missing");
            for(size_t slide=0;slide<clip.slides.size();++slide){
                const auto image=screenAnimationBank_->sample(*index,slide,0,false);requireScreen(image.canvasWidth==canvas[0].number()&&image.canvasHeight==canvas[1].number()&&image.originalMovieFrame==slide&&clip.slides[slide].originalFrame==slide+1,"Actual decoded movie source frame ordering/geometry mismatch");
                const auto pixels=image.pixels();requireScreen(pixels.size()==image.byteCount(),"Actual movie decoder payload size mismatch");decodedBytes+=pixels.size();++movieFrames;
            }
            const int32_t id=create();set(id,name);const auto* state=screenAnimations_.thing(id);const auto initial=state->primary;const auto lifetime=state->body->entityHandle;auto first=visual(id,initial);requireScreen(first.frame.originalMovieFrame==0,"New MovieBook did not start at first original frame");
            const float speed=scalar(*d,"PlaySpeed",60);requireScreen(speed>0&&speed<=1000,"Actual movie source playback speed invalid");
            // Natural fractional playback only: no frame target, instant jump,
            // synthetic completion or replacement of the real close movie.
            const auto bound=size_t(std::ceil(double(count+2)/speed*60))+4;
            for(size_t tick=0;tick<bound;++tick){step(1.f/60);state=screenAnimations_.thing(id);requireScreen(state&&state->body->entityHandle==lifetime,"Movie completion replaced actual Thing lifetime");if(state->body->animation!=name)break;}
            state=screenAnimations_.thing(id);const auto chain=text(*d,"ChainTo");
            if(flag(*d,"HoldLastFrame")){requireScreen(state->primary==initial&&state->body->animation==name&&state->body->frame==count,"Source HoldLastFrame did not preserve last frame/lifetime");auto last=visual(id,initial);requireScreen(last.frame.originalMovieFrame==count-1,"Source held movie visual is not its last original frame");++holdCases;}
            else if(!chain.empty()&&chain!="null"){requireScreen(state->body->animation==chain&&state->primary!=initial,"Natural movie completion did not select original ChainTo");++chainCases;}
            else{requireScreen(state->body->animation==sourceGraphic&&state->primary!=initial&&state->sourceGraphic==sourceGraphic,"Natural movie completion did not restore immutable ThingData.Graphic");++fallbackCases;}
            requireScreen(!effects_.registry()->find(initial)||flag(*d,"HoldLastFrame"),"Completed movie retained its old actual primary allocation");dispose(id);
            hades::log(hades::Severity::Info,"HadesScreenProbe","SCREEN_PROBE_MOVIE name=%s source_frames=%u hold=%d chain=%s",name.c_str(),count,int(flag(*d,"HoldLastFrame")),chain.c_str());
        }
        requireScreen(movieCases&&chainCases&&fallbackCases&&holdCases,"Actual movie catalog does not cover chain/fallback/hold classes");
        for(const auto& [name,row]:clips->object()){
            const auto* d=screenAnimations_.definition(name);if(!d||text(*d,"Type")!="Constant"||!flag(*d,"Loop"))continue;
            const bool symbol=d->get("StartScaleX")&&d->get("EndScaleX")&&d->get("StartScaleY")&&d->get("EndScaleY")&&d->get("StartAngle")&&d->get("EndAngle")&&flag(*d,"PingPongScale")&&flag(*d,"PingPongAngle")&&!text(*d,"ChildAnimation").empty()&&d->get("CreateAnimations");
            const bool random=scalar(*d,"RandomDurationMin")>0&&scalar(*d,"RandomDurationMax")>=scalar(*d,"RandomDurationMin")&&flag(*d,"PingPongColor")&&d->get("StartAlpha")&&d->get("EndAlpha");
            if(!symbol&&!random)continue;requireScreen(symbolCases+randomCases<32,"Screen helper diagnostic derived class bound");const int32_t id=create();set(id,name);auto* state=screenAnimations_.thing(id);const auto primary=state->primary;
            if(symbol){
                ++symbolCases;const float duration=scalar(*d,"Duration");requireScreen(duration>0&&duration<=2,"Actual symbol duration diagnostic bound");
                auto before=visual(id,primary);std::set<std::string> visited;uint64_t parent=0;std::string expected=name;
                for(const auto& layer:state->layers){requireScreen(visited.insert(layer.name).second&&layer.name==expected,"Actual source symbol child order differs");const auto* definition=screenAnimations_.definition(layer.name);const auto* life=effects_.registry()->find(layer.handle);requireScreen(definition&&life&&life->initialized&&life->parent==parent&&life->ownerId==id&&life->ownerLifetime==state->body->entityHandle,"Actual symbol child allocation/owner mismatch");if(scalar(*definition,"Duration")>0)requireScreen(near(layer.duration,scalar(*definition,"Duration"))&&layer.progress==0,"Source symbol independent helper duration was not initialized");
                    const auto layerVisual=visual(id,layer.handle);(void)layerVisual;++helperLayers;size_t created=0;
                    if(const auto* list=definition->get("CreateAnimations"))for(const auto& entry:list->array()){requireScreen(created<layer.created.size(),"Source symbol created FX missing");const auto* fx=actionEffects_.state(layer.created[created++]);requireScreen(fx&&fx->ownerId==id&&fx->ownerLifetime==state->body->entityHandle&&fx->name==text(entry,"Name"),"Source symbol created FX resource/owner mismatch");++createdFx;}
                    requireScreen(created==layer.created.size(),"Symbol created FX escaped original source list");parent=layer.handle;expected=text(*definition,"ChildAnimation");
                }
                requireScreen(expected.empty()||expected=="null","Actual source symbol screen child closure incomplete");
                step(duration/2);state=screenAnimations_.thing(id);requireScreen(state->primary==primary&&near(state->layers.front().progress,.5f),"Source symbol half-duration fraction differs");auto middle=visual(id,primary);
                requireScreen(near(middle.localRect[2]/before.localRect[2],scalar(*d,"EndScaleX")/scalar(*d,"StartScaleX"))&&near(middle.localRect[3]/before.localRect[3],scalar(*d,"EndScaleY")/scalar(*d,"StartScaleY")),"Source symbol X/Y helper endpoints did not independently reach their authored values");
                step(duration/2);state=screenAnimations_.thing(id);requireScreen(state->primary==primary&&near(state->layers.front().remaining,0),"Source Constant completed/looped at exactly zero");step(1.f/60);state=screenAnimations_.thing(id);requireScreen(state->primary==primary&&near(state->layers.front().remaining,duration)&&near(state->layers.front().progress,1),"Source symbol loop did not retain lifetime/cached helper fraction");
            }
            if(random){
                ++randomCases;const float lo=scalar(*d,"RandomDurationMin"),hi=scalar(*d,"RandomDurationMax");requireScreen(hi<=2,"Actual random lighting duration diagnostic bound");
                for(int loop=0;loop<3;++loop){
                    state=screenAnimations_.thing(id);const float duration=state->layers.front().duration;requireScreen(duration>=lo&&duration<=hi&&near(state->layers.front().remaining,duration),"Actual native RNG lighting duration outside source bounds");const auto start=visual(id,primary);step(duration/2);auto middle=visual(id,primary);requireScreen(std::abs(middle.color[3]-start.color[3])>.001f,"Source lighting alpha ping-pong did not reach distinct endpoint");step(duration/2);step(1.f/60);state=screenAnimations_.thing(id);requireScreen(state->primary==primary&&near(state->layers.front().progress,1)&&state->layers.front().remaining>0,"Actual random lighting loop lost cached fraction/owner");++randomLoops;
                    hades::log(hades::Severity::Info,"HadesScreenProbe","SCREEN_PROBE_RANDOM name=%s loop=%d sampled_duration=%.9g next_duration=%.9g",name.c_str(),loop,duration,state->layers.front().duration);
                }
            }
            dispose(id);(void)row;
        }
        requireScreen(symbolCases&&randomCases,"Actual catalog lacks derived symbol/random lighting classes");
        // GetColor RVA0x2497bf..0x2498f6 / float ctor0xa1b60: RGB alone
        // selects conversion. Byte RGB's absent alpha is the raw default1
        // (one byte), while normalized RGB clamps explicit alpha255 to1.0.
        struct ColorCase {const char* label;std::vector<double> source;std::array<uint8_t,4> expected;bool base;};
        const std::array<ColorCase,4> colors{{
            {"normalized_fractional",{.5,.25,1,.75},{127,63,255,191},true},
            {"byte_fractional_wrap",{256.75,-1.75,2.75,255.75},{0,255,2,255},false},
            {"byte_rgb_default_alpha",{255,127,63},{255,127,63,1},true},
            {"normalized_rgb_byte_alpha",{.5,.25,1,255},{127,63,255,255},false}
        }};
        std::string colorMovie;
        for(const auto& [name,row]:clips->object())if(row.get("movie_binding")){const auto* d=screenAnimations_.definition(name);if(d&&!d->get("Color")&&!d->get("Alpha")&&!d->get("Red")){colorMovie=name;break;}}
        requireScreen(!colorMovie.empty(),"Color diagnostic requires an actual uncolored MovieBook");
        auto pushColor=[&](const std::vector<double>& channels){lua_newtable(L);for(size_t k=0;k<channels.size();++k){lua_pushnumber(L,channels[k]);lua_rawseti(L,-2,int(k+1));}lua_setfield(L,-2,"Color");};
        for(const auto& c:colors)for(bool animation:{false,true}){
            const int32_t id=create();lua_getglobal(L,"SetColor");lua_newtable(L);lua_pushinteger(L,id);lua_setfield(L,-2,"Id");lua_pushboolean(L,1);lua_setfield(L,-2,"SetBase");pushColor({1,1,1,1});
            if(lua_pcall(L,1,0,0)!=LUA_OK)throw std::runtime_error(lua_tostring(L,-1));lua_settop(L,top);
            lua_getglobal(L,animation?"SetAnimation":"SetColor");lua_newtable(L);lua_pushinteger(L,id);lua_setfield(L,-2,animation?"DestinationId":"Id");
            if(animation){lua_pushlstring(L,colorMovie.data(),colorMovie.size());lua_setfield(L,-2,"Name");}else{lua_pushboolean(L,c.base);lua_setfield(L,-2,"SetBase");}pushColor(c.source);
            if(lua_pcall(L,1,0,0)!=LUA_OK)throw std::runtime_error(lua_tostring(L,-1));lua_settop(L,top);const auto* state=screenAnimations_.thing(id);const auto shown=visual(id,state->primary);const bool base=c.base&&!animation;
            for(size_t k=0;k<4;++k){const float value=float(c.expected[k])/255.f;requireScreen(near(state->baseColor[k],base?value:1.f)&&near(state->fractionColor[k],base?1.f:value),std::string("Registered color channel storage mismatch: ")+c.label);requireScreen(std::lround(shown.color[k]*255.f)==c.expected[k],std::string("Actual screen visual color byte mismatch: ")+c.label);}
            requireScreen(near(state->body->alpha,base?1.f:float(c.expected[3])/255.f),"Registered color Body alpha mismatch");dispose(id);++colorCases;
        }
        hades::log(hades::Severity::Info,"HadesScreenProbe","SCREEN_PROBE_OK scope=host_registered_actions movie_cases=%u decoded_frames=%u decoded_bytes=%llu chain=%u fallback=%u hold=%u symbol_roots=%u helper_layers=%u created_fx=%u random_cases=%u random_loops=%u color_cases=%u component_steps=%u progression=0 gpuClaim=0 deviceClaim=0 anchor=%s sourceGraphic=%s",movieCases,movieFrames,(unsigned long long)decodedBytes,chainCases,fallbackCases,holdCases,symbolCases,helperLayers,createdFx,randomCases,randomLoops,colorCases,steps,obstacle.c_str(),sourceGraphic.c_str());
        error_.clear();return true;
    }catch(const std::exception& e){error_=std::string("Screen action probe: ")+e.what();return false;}
}
#endif
