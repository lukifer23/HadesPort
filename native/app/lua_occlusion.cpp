#include "lua_runtime.hpp"
#include "graphics_scene.hpp"
#include "scene_renderer.hpp"
#include <algorithm>
#include <cmath>
#include <deque>
#include <set>
#include <stdexcept>
extern "C" {
#include <lua.h>
}
// Hero/unit occlusion: sgg::ObstacleManager::Update VA0x1802c80c0, block
// guarded by ConfigOptions::UseOcclusion (byte 0x181a0ed01, initializer
// RVA0xba30 writes true at 0x18000baa0). There is no unit silhouette in this
// path: occluding obstacles fade to ThingData.OcclusionOpacity; an outline is
// queued only for a candidate with OcclusionOutlineOpacity>0 (hero data: 0).
// ThingData offsets (full data = def+8): CanBeOccluded +0x22d,
// CausesOcclusion +0x22e, OcclusionWidth +0x230, OcclusionHeight +0x234,
// OcclusionOpacity +0x238, OcclusionInterpolateDuration +0x23c,
// OcclusionOutlineOpacity +0x258, OcclusionBaseAnim +0x280.
bool ScriptRuntime::configOptionBool(const char* name,bool& value,std::string& error){
    const int top=lua_gettop(state_);lua_getglobal(state_,"GetConfigOptionValue");lua_newtable(state_);lua_pushstring(state_,name);lua_setfield(state_,-2,"Name");
    if(lua_pcall(state_,1,1,0)!=LUA_OK){error=std::string("GetConfigOptionValue ")+name+": "+lua_tostring(state_,-1);lua_settop(state_,top);return false;}
    if(!lua_isboolean(state_,-1)){error=std::string("ConfigOption is not bool: ")+name;lua_settop(state_,top);return false;}
    value=lua_toboolean(state_,-1);lua_settop(state_,top);return true;
}
namespace {
struct Area {float x=0,y=0,w=0,h=0;};
struct Occludable {int32_t id=0;float x=0,y=0;Area area;std::array<float,3> key{};};
}
bool ScriptRuntime::advanceObstacleOcclusion(std::string& error){
 try{
    bool use=true;if(!configOptionBool("UseOcclusion",use,error))return false;
    if(!use){error.clear();return true;}
    const auto prop=[&](int32_t id,const char* key)->const hades::SourceValue&{const auto* v=actorWorld_.currentProperty(id,key);if(!v)throw std::runtime_error(std::string("Original occlusion ThingData field unavailable: ")+key);return *v;};
    const auto live=[&](int32_t id){const auto* a=actorWorld_.actor(id);return a&&a->registered&&!a->isDisposed&&a->object&&a->object->visible?a:nullptr;};
    // Candidates (0x1802c8a2c..0x1802c8c40): every Unit with CanBeOccluded and a
    // graphic (+0x138), then the CanBeOccluded obstacle set (ObstacleSet 8).
    std::vector<Occludable> candidates;
    for(const auto id:actorWorld_.registrationOrder()){
        const auto* a=live(id);if(!a||!a->definition||!a->animation.active()||!prop(id,"Thing.CanBeOccluded").boolean())continue;
        hades::ActorVisual v;std::string detail;if(!samplePresentation({VisualKey::Kind::Actor,id,a->primaryAnimationLifetime},v,detail)||!v.visible)continue;
        Occludable c;c.id=id;if(!actorWorld_.location(id,c.x,c.y,error))return false;
        // Animation::GetDrawArea 0x1802991f0 as the drawn AABB of the source frame.
        c.area={v.x+v.localRect[0]*v.scale,v.y-v.z*.8660254037844386f+v.localRect[1]*v.scale,v.localRect[2]*v.scale,v.localRect[3]*v.scale};
        c.key={float(v.drawGroup),float(v.sortIndex),v.sourceSortY.value_or(v.y)};candidates.push_back(c);
    }
    if(!sceneCorners_)sceneCorners_=hades::loadSceneSpriteCorners(*assets_);
    // Obstacle draw area/sort key from its original scene primaries, moved with
    // the live body exactly as sourceSceneSprite does.
    const auto obstacleArea=[&](int32_t id,Area& out,std::array<float,3>& key)->bool{
        const auto* recipe=scenePrimaries_.recipe(id);const auto base=presentationBases_.find(id);if(!recipe||recipe->sceneSprites.empty()||base==presentationBases_.end())return false;
        const auto* a=actorWorld_.actor(id);const auto& initial=base->second.body;const auto& body=*a->object;
        const float initialScale=initial.scale*initial.scaleFraction,currentScale=body.scale*body.scaleFraction;if(initialScale<=0)return false;
        float x=0,y=0,z=0;if(!actorWorld_.location(id,x,y,error)||!actorWorld_.zLocation(id,z,error))throw std::runtime_error(error);
        const float s=currentScale/initialScale;const float ox=float(prop(id,"Thing.Offset.X").number()),oy=float(prop(id,"Thing.Offset.Y").number());
        const float oldX=initial.x+ox*initialScale,oldY=initial.y+oy*initialScale-initial.z*.8660254037844386f,newX=x+ox*currentScale,newY=y+oy*currentScale-z*.8660254037844386f;
        float l=INFINITY,t=INFINITY,r=-INFINITY,b=-INFINITY;
        for(const auto index:recipe->sceneSprites){const auto& c=sceneCorners_->at(index);for(size_t k=0;k<8;k+=2){const float px=newX+(c[k]-oldX)*s,py=newY+(c[k+1]-oldY)*s;l=std::min(l,px);r=std::max(r,px);t=std::min(t,py);b=std::max(b,py);}}
        out={l,t,r-l,b-t};float sortIndex=0;if(const auto* seed=actorWorld_.seed(a->sourceId))if(const auto* v=seed->map.get("SortIndex"))sortIndex=float(v->number());
        key={float(presentationDrawGroup(body)),sortIndex,y};return true;
    };
    // Obstacle::IsObscuring 0x1802c3d50 (world-space branch; Thing+0x220
    // screen-space obstacles fail loudly below).
    const auto obscures=[&](int32_t o,const Occludable& t)->bool{
        if(t.id==o)return false;
        const auto* a=actorWorld_.actor(o);
        // +0x22c own parallax x MapThing+0xf0 ParallaxAmount must equal 1.
        if(a->ownParallax*a->mapParallax!=1.f)return false;
        Area area;std::array<float,3> key;if(!obstacleArea(o,area,key))return false;
        if(a->object->screenSpace)throw std::runtime_error("Screen-space obstacle occlusion (IsObscuring 0x1802c3f6b) not reconstructed");
        // Draw-group index (Thing::GetFirstGroupName -> world group table) and
        // sort order. APPROXIMATION (labelled): Bounds::CalcSortOrder 0x1801e6180
        // is replaced by this port's draw-order key {group, SortIndex, y}; the
        // obstacle must draw after the unit. The CalcSortOrder "disjoint" result
        // that admits only OcclusionWidth/Height obstacles is not distinguished.
        if(key<t.key||key==t.key)return false;
        const float scale=a->object->scale*a->object->scaleFraction; // APPROXIMATION: Animation vfunc+0x98(1) as body scale.
        float ox=0,oy=0;if(!actorWorld_.location(o,ox,oy,error))throw std::runtime_error(error);
        if(const float w=float(prop(o,"Thing.OcclusionWidth").number());w>0){const float half=scale*w*.5f;if(t.x<ox-half||t.x>ox+half)return false;}
        else if(t.x<std::trunc(area.x)||t.x>std::trunc(area.x)+std::trunc(area.w))return false; // Obstacle bounds +0x98 (int rect)
        if(const float h=float(prop(o,"Thing.OcclusionHeight").number());h>0){const float half=scale*h*.5f;return oy-half<=t.y&&t.y<=oy+half;}
        // GetDrawArea int rects: unit bottom inside the obstacle's vertical extent.
        const int bottom=int(t.area.y)+int(t.area.h),top=int(area.y),end=int(area.y)+int(area.h);
        return bottom<=end&&top<=bottom;
    };
    const auto causes=[&](int32_t id){
        if(prop(id,"Thing.CausesOcclusion").boolean())return true;
        const auto* a=actorWorld_.actor(id);if(const auto* seed=actorWorld_.seed(a->sourceId))if(const auto* v=seed->map.get("CausesOcclusion"))return v->boolean(); // MapThing+0xe9
        return false;
    };
    // ObstacleSet 7 roots (ObstacleManager::GetRoot 0x1802cc0f0), each visited once.
    std::set<int32_t> roots;
    for(const auto id:actorWorld_.registrationOrder()){
        const auto* a=live(id);if(!a||!a->definition||a->definition->kind!="Obstacle"||!causes(id))continue;
        int32_t root=id;for(int guard=0;guard<64;++guard){const auto* r=actorWorld_.actor(root);if(!r||!r->parentId||!live(r->parentId))break;root=r->parentId;}
        roots.insert(root);
    }
    // ObstacleSet 7 membership persists after CausesOcclusion is cleared by
    // SetThingProperty (0x1802e6d5c only adds); such a root keeps its group in
    // the update, which then takes the unoccluded fade path.
    for(const auto root:actorWorld_.occlusionRootSet)if(live(root))roots.insert(root);
    hades::BodyTransitionOptions linear;
    hades::BodyTransitionOptions smooth;smooth.smoothStep=true; // Interpolate(..., true, -1, -1)
    for(const auto root:roots){
        std::vector<int32_t> group;std::deque<int32_t> open{root};const Occludable* hit=nullptr;
        while(!open.empty()){const int32_t o=open.front();open.pop_front();const auto* a=live(o);if(!a)continue;group.push_back(o);
            if(!hit&&causes(o))for(const auto& c:candidates)if(obscures(o,c)){hit=&c;break;}
            for(const auto child:a->children)open.push_back(child); // Obstacle +0x648 children
        }
        if(hit){
            // Outline request (0x1802c9220): ShowUIAnimations && OcclusionOutlineOpacity>0.
            bool ui=true;if(!configOptionBool("ShowUIAnimations",ui,error))return false;
            if(ui&&prop(hit->id,"Thing.OcclusionOutlineOpacity").number()>0)throw std::runtime_error("Occlusion outline draw request (ObstacleManager::Update 0x1802c9220) not reconstructed");
        }
        for(const auto o:group){
            const float opacity=float(prop(o,"Thing.OcclusionOpacity").number());
            if(opacity<0)continue; // 0x1802c98b0 (unoccluded) / 0x1802c9a92 (occluded) sign tests
            linear.duration=float(prop(o,"Thing.OcclusionInterpolateDuration").number());
            // BodyComponent::Adjust 0x1802b7440 on Body+0x320 alpha: {target, initial 1, duration}.
            if(!actorWorld_.adjustColorChannel(o,3,hit?opacity:1.f,1.f,linear,error))return false;
            const auto& baseAnim=prop(o,"Thing.OcclusionBaseAnim").text();
            auto existing=occlusionBaseAnims_.find(o);
            const float fraction=actorWorld_.actor(o)->fractions[1].fraction(); // Body+0x374
            if(hit){
                if(baseAnim.empty()||baseAnim=="null")continue;
                uint64_t handle=existing==occlusionBaseAnims_.end()?0:existing->second;
                const auto* state=handle?actionEffects_.state(handle):nullptr;
                if(!state||state->done){
                    // AnimationManager::Create 0x18029e350 + Thing::AddFrontAnim 0x1802e5ca0.
                    // APPROXIMATION (labelled): front-anim draw ordering uses the
                    // action-effect attachment order, not a Thing front list.
                    if(!createActionEffect(baseAnim,o,std::nullopt,handle,error))return false;occlusionBaseAnims_[o]=handle;
                }
                const float alpha=hades::bodyInterpolate(0,1,fraction,smooth);
                if(!actionEffects_.setBaseAlpha(handle,uint8_t(int(alpha*255.f)),error))return false;
            }else if(existing!=occlusionBaseAnims_.end()){
                const float alpha=hades::bodyInterpolate(1,0,fraction,smooth);
                if(!actionEffects_.state(existing->second)){occlusionBaseAnims_.erase(existing);continue;}
                if(alpha<=0){if(!actionEffects_.destroy(existing->second,error))return false;occlusionBaseAnims_.erase(existing);continue;} // +99 expire, Thing+0x3d0=0
                if(!actionEffects_.setBaseAlpha(existing->second,uint8_t(int(alpha*255.f)),error))return false;
            }
        }
    }
    error.clear();return true;
 }catch(const std::exception& e){error=e.what();return false;}
}
