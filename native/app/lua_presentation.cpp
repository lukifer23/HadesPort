#include "lua_runtime.hpp"
#include "scene_renderer.hpp"
#include "scene_runtime.hpp"
#include "effect_particle_visual.hpp"
#include "input_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
float number(const hades::ActorWorld& world,int32_t id,const char* key){const auto* v=world.currentProperty(id,key);if(!v)throw std::runtime_error(std::string("Presentation source property unavailable: ")+key);const float n=float(v->number());if(!std::isfinite(n))throw std::runtime_error("Nonfinite presentation property");return n;}
int ownerPolicy(const hades::SourceValue& source,const char* name){const auto* value=source.get(name);if(!value)return 0;if(std::holds_alternative<std::string>(value->value)){const auto text=value->text();if(text=="Maintain")return 0;if(text=="Take")return 1;if(text=="Ignore")return 2;throw std::runtime_error(std::string("Unknown source owner policy ")+name);}return int(value->number());}
}
uint32_t ScriptRuntime::presentationGroup(const std::string& name)const{
    if(presentationGroupFrame_!=worldFrame_||presentationGroups_.empty()){
        presentationGroups_=snapshotSceneGroups(state_);presentationGroupIndices_.clear();
        for(uint32_t i=0;i<presentationGroups_.size();++i)presentationGroupIndices_.emplace(presentationGroups_[i].name,i);
        presentationGroupFrame_=worldFrame_;
    }
    const auto it=presentationGroupIndices_.find(name);if(it==presentationGroupIndices_.end())throw std::runtime_error("Actual draw group unavailable: "+name);return it->second;
}
uint32_t ScriptRuntime::presentationDrawGroup(const ObjectSnapshot& body)const{
    if(!body.group.empty())return presentationGroup(body.group);
    if(!body.groups.empty())return presentationGroup(body.groups.front());
    return presentationGroup("Default");
}
std::optional<ObjectSnapshot> ScriptRuntime::localPlayerBody()const{
    for(const auto& player:snapshotInputPlayers(state_))if(player.playerIndex==1){const auto* actor=actorWorld_.actor(player.activeUnitId);if(actor&&actor->registered&&actor->entityHandle==player.activeUnitLifetime&&actor->object){auto body=*actor->object;std::string error;if(!actorWorld_.location(actor->id,body.x,body.y,error)||!actorWorld_.zLocation(actor->id,body.z,error))throw std::runtime_error(error);return body;}}
    return {};
}

bool ScriptRuntime::sourceSceneSprite(uint32_t index,hades::SceneSprite& sprite,std::string& error)const{
 try{
    const auto entry=sceneSpriteOwners_.find(index);if(entry==sceneSpriteOwners_.end()){error="Original source sprite has no owning Thing recipe";return false;}
    const auto originalGroup=sprite.drawGroup;if(originalGroup>=originalSceneGroupNames_.size())throw std::runtime_error("Original scene group binding outside source metadata");
    sprite.drawGroup=presentationGroup(originalSceneGroupNames_[originalGroup]);sprite.blend=uint32_t(presentationGroups_[sprite.drawGroup].blendMode);
    const auto* actor=actorWorld_.actor(entry->second);const auto* primary=scenePrimaries_.stateForSprite(index);
    if(!actor||!actor->registered||!actor->object||!actor->object->visible||!primary){sprite.runtimeSuppressed=true;return true;}
    const auto* life=effects_.registry()->find(primary->handle);
    if(!life||life->canceled||primary->resource==hades::PrimaryResourceKind::Auxiliary||primary->resource==hades::PrimaryResourceKind::Pending){sprite.runtimeSuppressed=true;return true;}
    const auto base=presentationBases_.find(actor->id);if(base==presentationBases_.end())throw std::runtime_error("Original presentation baseline unavailable");
    const auto& initial=base->second.body;const auto& body=*actor->object;
    if(body.angle!=initial.angle)throw std::runtime_error("Changed scene angle requires source post-rotation affine adapter");
    const float initialScale=initial.scale*initial.scaleFraction,currentScale=body.scale*body.scaleFraction;
    if(initialScale<=0)throw std::runtime_error("Original scene body has zero initial scale");
    const float scale=currentScale/initialScale;
    float x=0,y=0,z=0;if(!actorWorld_.location(actor->id,x,y,error)||!actorWorld_.zLocation(actor->id,z,error))return false;
    constexpr float projectZ=.8660254037844386f;
    // Original packet/frame quads retain source squash, flip, atlas origin and
    // post-rotation scale. Body scale is uniform; world-axis Thing.Offset moves
    // with that body scale. Native angle mutation needs its separate adapter.
    const float ox=number(actorWorld_,actor->id,"Thing.Offset.X"),oy=number(actorWorld_,actor->id,"Thing.Offset.Y");
    const float oldX=initial.x+ox*initialScale,oldY=initial.y+oy*initialScale-initial.z*projectZ;
    const float newX=x+ox*currentScale,newY=y+oy*currentScale-z*projectZ;
    for(size_t k=0;k<8;k+=2){sprite.corners[k]=newX+(sprite.corners[k]-oldX)*scale;sprite.corners[k+1]=newY+(sprite.corners[k+1]-oldY)*scale;}
    for(size_t k=0;k<4;++k){float current=actor->color[k]*(k<3?actor->tint[k]:1),old=base->second.color[k];if(k==3){current*=body.alpha;old*=initial.alpha;}if(old==0){if(current!=0)throw std::runtime_error("Unquantized source color required after zero-channel mutation");sprite.tint[k]=0;}else sprite.tint[k]=std::clamp(sprite.tint[k]*(current/old),0.f,1.f);}
    for(size_t k=0;k<3;++k)sprite.hsv[k]+=actor->hsv[k]-base->second.hsv[k];
    const auto name=body.group!=initial.group?body.group:originalSceneGroupNames_[originalGroup];
    if(const auto*use=primary->definition.get("UseAttachedFlasher");!use||use->boolean())sprite.flashColor=flashColorForOwner(actor->id,actor->entityHandle);
    sprite.drawGroup=presentationGroup(name);const auto& group=presentationGroups_[sprite.drawGroup];sprite.runtimeSuppressed=!group.visible;sprite.blend=uint32_t(group.blendMode);
    float sortIndex=0;if(const auto* seed=actorWorld_.seed(actor->sourceId))if(const auto* source=seed->map.get("SortIndex"))sortIndex=float(source->number());
    sprite.runtimeSortKey=std::array<float,3>{float(sprite.drawGroup),sortIndex,y};
    sprite.parallaxOrigin={x,y};sprite.parallax=actor->ownParallax*actor->mapParallax;
    error.clear();return true;
 }catch(const std::exception& e){error=e.what();return false;}
}

std::vector<ScriptRuntime::VisualKey> ScriptRuntime::presentationVisuals()const{
    std::vector<VisualKey> out;
    for(auto id:actorWorld_.registrationOrder()){const auto* actor=actorWorld_.actor(id);if(actor&&actor->registered&&actor->object&&actor->object->visible&&actor->animation.active())out.push_back({VisualKey::Kind::Actor,id,actor->primaryAnimationLifetime});}
    for(auto handle:scenePrimaries_.auxiliaryHandles()){const auto* s=scenePrimaries_.state(handle);const auto* actor=actorWorld_.actor(s->ownerId);if(actor&&actor->registered&&actor->object&&actor->object->visible)out.push_back({VisualKey::Kind::Auxiliary,s->ownerId,handle});}
    for(const auto& [handle,p]:particles_.particles())if(particles_.alive(handle))out.push_back({VisualKey::Kind::Particle,p.ownerId,handle});
    for(auto handle:actionEffects_.handles()){const auto* effect=actionEffects_.state(handle);
        if(effect->guiLine){const auto line=aimLines_.find(effect->ownerId);if(line==aimLines_.end()||line->second.handle!=handle||line->second.frame!=worldFrame_)continue;}
        out.push_back({VisualKey::Kind::Action,effect->ownerId,handle});}
    // One key per drawn screen animation: primary then its ChildAnimation chain
    // (each child draws after its parent, Animation.SubmitDraw 0x2b2707).
    for(const auto& [id,screen]:screenAnimations_.things())if(!screen.dead&&screen.primary&&screen.body&&screen.body->active&&screen.body->visible)for(const auto& layer:screen.layers)out.push_back({VisualKey::Kind::Screen,id,layer.handle});
    // Thing.Invisible hides the editor marker/primary, not its separately
    // allocated Light. Original light-only obstacles intentionally use it.
    for(auto id:actorLightOwners()){const auto* actor=actorWorld_.actor(id);if(actor&&actor->object)out.push_back({VisualKey::Kind::Light,id,actor->entityHandle});}
    return out;
}
std::shared_ptr<const hades::LightShadowGeometry> ScriptRuntime::presentationShadowGeometry(){
    if(presentationShadowFrame_==worldFrame_&&presentationShadows_)return presentationShadows_;
    const auto snapshot=actorWorld_.occluderSnapshot();uint64_t revision=1469598103934665603ull;
    for(const auto& source:snapshot){revision=(revision^source.ownerLifetime)*1099511628211ull;revision=(revision^source.revision)*1099511628211ull;}
    if(!presentationShadows_||presentationShadows_->revision!=revision){
        auto geometry=std::make_shared<hades::LightShadowGeometry>();geometry->revision=revision;
        for(const auto& source:snapshot){hades::LightShadowPolygon polygon;polygon.lifetime=source.ownerLifetime;polygon.revision=source.revision;for(const auto& point:source.polygon)polygon.points.push_back({point.x,point.y});geometry->polygons.push_back(std::move(polygon));}presentationShadows_=std::move(geometry);
    }
    presentationShadowFrame_=worldFrame_;return presentationShadows_;
}

bool ScriptRuntime::samplePresentationImpl(const VisualKey& key,hades::ActorVisual& out,std::string& error){
 try{
    out={};
    if(key.kind==VisualKey::Kind::Light){
        const auto* actor=actorWorld_.actor(key.owner);if(!actor||!actor->registered||!actor->object||actor->entityHandle!=key.handle)throw std::runtime_error("Actual source light owner lifetime expired");
        hades::ActorLightVisual light;if(!sampleActorLight(key.owner,light,error))return false;out=std::move(light.image);out.light=true;out.parallax=light.parallax;
        out.drawGroup=presentationDrawGroup(*actor->object);out.blend=uint32_t(presentationGroups_[out.drawGroup].blendMode);out.visible=presentationGroups_[out.drawGroup].visible;
        // Native DrawLayer submits the gathered lights after ordinary objects
        // in their source group. Full native depth-sort interleaving is pending.
        out.sortIndex=INT32_MAX;out.sourceSortY=out.y;if(light.createsShadow)out.lightShadows=presentationShadowGeometry();return true;
    }
    if(key.kind==VisualKey::Kind::Screen){
        const auto* screen=screenAnimations_.thing(key.owner);if(!screen||screen->dead||!screen->body)throw std::runtime_error("Actual screen visual lifetime unavailable");
        if(!screenAnimations_.visual(key.owner,key.handle,out,error))return false;
        out.drawGroup=presentationDrawGroup(*screen->body);out.blend=uint32_t(presentationGroups_[out.drawGroup].blendMode);out.visible=presentationGroups_[out.drawGroup].visible;out.sourceSortY=screen->body->sortById?float(screen->body->id):screen->body->y;return true;
    }
    if(key.kind==VisualKey::Kind::Action){
        const auto* source=actionEffects_.state(key.handle);const auto* actor=actorWorld_.actor(key.owner);if(!source)throw std::runtime_error("Actual action FX lifetime unavailable");
        hades::EffectOwnerState owner=source->ownerSnapshot;std::array<uint8_t,4> fraction{255,255,255,255};
        if(!source->detached&&key.owner<0){
            // Projectile-owned FX (Detonate graphic). Owner fraction = projectile body alpha;
            // APPROXIMATION: projectile tint/ambient fractions are not applied to owned FX.
            const auto po=projectileFxOwners_.find(key.owner);const auto* q=po==projectileFxOwners_.end()?nullptr:projectileWorld_.actor(po->second);
            if(!q||!effectOwner(key.owner,owner,error))return false;fraction[3]=uint8_t(std::clamp(q->body.alpha,0.f,1.f)*255.f);
        }
        else if(!source->detached){
            if(!actor||!actor->registered||!actor->object)throw std::runtime_error("Actual attached action FX draw owner unavailable");
            if(!effectOwner(key.owner,owner,error))return false;
            fraction={uint8_t(actor->tint[0]*255.f),uint8_t(actor->tint[1]*255.f),uint8_t(actor->tint[2]*255.f),uint8_t(std::clamp(actor->object->alpha,0.f,1.f)*255.f)};
        }
        if(!actionEffects_.visual(key.handle,owner,fraction,out,error))return false;
        if(source->guiLine){
            const auto i=aimLines_.find(key.owner);if(i==aimLines_.end()||i->second.handle!=key.handle||i->second.ownerLifetime!=owner.lifetime)throw std::runtime_error("Actual GUI aim line lifetime unavailable");const auto& line=i->second;
            // GUIComponentLine::Draw VA180170b52..b9f: scale X = projected
            // endpoint distance / AnimationData.ScaleLength; scale Y = integer
            // projectile width / ScaleWidth. World-space renderer projection
            // cancels the native camera-zoom multiply on both values.
            const float dx=line.endX-line.x,dy=line.endY-line.y,length=std::hypot(dx,dy);
            const auto* sourceLength=source->definition.get("ScaleLength"),*sourceWidth=source->definition.get("ScaleWidth");if(!sourceLength||!sourceWidth||sourceLength->number()<=0||sourceWidth->number()<=0)throw std::runtime_error("Actual GUI line scale metadata unavailable");
            const float sx=length/float(sourceLength->number()),sy=line.width!=0?line.width/float(sourceWidth->number()):1.f;
            out.localRect[0]*=sx;out.localRect[2]*=sx;out.localRect[1]*=sy;out.localRect[3]*=sy;
            out.x=line.x;out.y=line.y;out.z=0;out.scale=1;out.angle=std::atan2(dy,dx);out.visible=line.frame==worldFrame_&&length>0;
            // Portable draw-group adaptation: GUIComponentLine's UI-system
            // submission enters the original world combat UI group.
            out.drawGroup=presentationGroup("Combat_UI_World");out.blend=uint32_t(presentationGroups_[out.drawGroup].blendMode);out.visible=out.visible&&presentationGroups_[out.drawGroup].visible;out.sourceSortY=out.y;return true;
        }
        const auto* groupName=source->definition.get("GroupName");std::string group=source->runtimeGroup?*source->runtimeGroup:(groupName?groupName->text():"");
        const auto* attached=source->definition.get("UseAttachedGroup");
        // preDisposeOwner retains the real owner snapshot. Looking up the
        // retired Thing here drops surviving detonation FX after its disposal.
        if(group.empty()&&(!attached||attached->boolean())){if(!owner.group)throw std::runtime_error("Action FX owner draw group unavailable");group=*owner.group;}
        if(group.empty())group="Default";
        out.drawGroup=presentationGroup(group);out.blend=uint32_t(presentationGroups_[out.drawGroup].blendMode);out.visible=presentationGroups_[out.drawGroup].visible;
        out.sourceSortY=out.y;return true;
    }
    if(key.kind==VisualKey::Kind::Particle){
        const auto it=particles_.particles().find(key.handle);if(it==particles_.particles().end()||!particles_.alive(key.handle))throw std::runtime_error("Actual visible particle lifetime expired");const auto& p=it->second;
        if(!p.sourceColorResolved||!p.imageBank)throw std::runtime_error("Actual particle image/color unavailable");
        hades::EffectParticleDrawContext context;context.sourceResolved=true;context.color={1,1,1,1};context.sortY=p.y;
        const auto* source=p.sourceDefinition.get("GroupName");std::string group=source?source->text():"";
        const auto* attached=p.sourceDefinition.get("UseAttachedGroup");if(group.empty()&&(!attached||attached->boolean())){const auto* actual=actorWorld_.actor(p.ownerId);if(!actual||!actual->object)throw std::runtime_error("Actual attached particle group unavailable");group=actual->object->group;}
        if(group.empty())group="Default";context.group=presentationGroup(group);context.blend=uint32_t(presentationGroups_[context.group].blendMode);
        if(const auto* value=p.sourceDefinition.get("BlendMode"))context.blend=uint32_t(value->number());if(const auto* value=p.sourceDefinition.get("AddColor"))context.addColor=value->boolean();
        const auto* owner=actorWorld_.actor(p.ownerId);if(owner&&owner->registered&&owner->object){context.ownerBaseColor=owner->baseColorBytes;context.ownerHsv=owner->hsv;context.ownerFractionColor=std::array<uint8_t,4>{uint8_t(owner->tint[0]*255.f),uint8_t(owner->tint[1]*255.f),uint8_t(owner->tint[2]*255.f),uint8_t(std::clamp(owner->object->alpha,0.f,1.f)*255.f)};}
        if(!hades::effectParticleVisual(particles_,key.handle,context,out,error))return false;out.visible=presentationGroups_[context.group].visible;return true;
    }
    auto* actor=actorWorld_.actor(key.owner);if(!actor||!actor->registered||!actor->object)throw std::runtime_error("Actual visible Thing unavailable");const auto& body=*actor->object;
    out.id=uint32_t(actor->id);out.lifetime=key.handle;out.scale=body.scale*body.scaleFraction;out.color=actor->color;for(size_t k=0;k<3;++k)out.color[k]*=actor->tint[k];out.color[3]*=body.alpha;out.hsv=actor->hsv;
    if(!actorWorld_.location(actor->id,out.x,out.y,error)||!actorWorld_.zLocation(actor->id,out.z,error))return false;
    out.drawGroup=presentationDrawGroup(body);out.blend=uint32_t(presentationGroups_[out.drawGroup].blendMode);out.visible=presentationGroups_[out.drawGroup].visible;out.sourceSortY=out.y;
    out.x+=number(actorWorld_,actor->id,"Thing.Offset.X")*out.scale;out.y+=number(actorWorld_,actor->id,"Thing.Offset.Y")*out.scale;
    if(key.kind==VisualKey::Kind::Actor){
        if(!actor->animation.active()||actor->primaryAnimationLifetime!=key.handle)throw std::runtime_error("HAA presentation lifetime mismatch");
        out.sourceBank=animationBank_.get();out.frame=animationBank_->sample(actor->animation.clipIndex(),actor->animation.slideIndex(),animationBank_->selectedAngleForFacing(actor->animation.clipIndex(),actorAnimationFacing(*actor)));out.localRect=out.frame.worldRect(*actor->animation.clip());out.scale*=actor->animationOwnScale;
        return true;
    }
    const auto* source=scenePrimaries_.state(key.handle);if(!source||source->ownerLifetime!=actor->entityHandle)throw std::runtime_error("Auxiliary image source owner lifetime mismatch");
    // Exact own image scale/offset are distinct. Draw offset inherits owner
    // scale even when source ScaleFromOwner ignores it (GetDrawLocation).
    out.x+=source->offsetX*body.scale*body.scaleFraction;out.y+=source->offsetY*body.scale*body.scaleFraction;out.z+=source->offsetZ;
    if(ownerPolicy(source->definition,"ScaleFromOwner")==2)out.scale=source->ownScale;else out.scale*=source->ownScale;
    if(ownerPolicy(source->definition,"ColorFromOwner")==2){out.color={1,1,1,1};out.hsv={0,0,0};}
    for(size_t k=0;k<3;++k)out.hsv[k]+=source->hsv[k];
    if(ownerPolicy(source->definition,"AngleFromOwner")==0)out.angle=-body.angle;
    return scenePrimaries_.sampleAuxiliary(key.handle,out,error);
 }catch(const std::exception& e){error=e.what();return false;}
}
