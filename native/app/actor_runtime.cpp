#include <strings.h>
#include "actor_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace hades {
namespace {
const SourceValue& require(const SourceValue& v,const std::string& key){const auto* p=v.path(key);if(!p)throw std::runtime_error("Original actor property is not resolved: "+key);return *p;}
float scalar(const SourceValue& v,const std::string& key){const double n=require(v,key).number();if(std::abs(n)>std::numeric_limits<float>::max())throw std::runtime_error("Actor scalar outside float range: "+key);return float(n);}
bool flag(const SourceValue& v,const std::string& key){return require(v,key).boolean();}
std::string text(const SourceValue& v,const std::string& key){return require(v,key).text();}
int32_t id(const SourceValue& v){const double n=v.number();if(n<=0||n>INT32_MAX||std::trunc(n)!=n)throw std::runtime_error("Invalid original actor ID");return int32_t(n);}
float radians(float degrees){return degrees*0.01745329238474369f;}
float angle(float a,float delta){if(delta==0)return a;constexpr float tau=6.2831854820251465f;const float sum=a+delta;return sum-std::floor(sum/tau)*tau;}
std::vector<ActorPoint> points(const SourceValue* v){std::vector<ActorPoint> out;if(!v)return out;for(const auto& p:v->array()){if(const auto* x=p.get("X"))out.push_back({float(x->number()),scalar(p,"Y")});else{const auto& a=p.array();if(a.size()!=2)throw std::runtime_error("Actor point requires XY");out.push_back({float(a[0].number()),float(a[1].number())});}}return out;}
void geometry(ActorComponents& a,const ObjectSnapshot& o,const ActorMapSeed* seed){
    const auto& props=a.definition->properties;
    const auto currentFlag=[&](const char* key){const auto i=a.propertyOverrides.find(key);return i==a.propertyOverrides.end()?flag(props,key):i->second.boolean();};
    if(scalar(props,"Thing.ExtentScale")>0||scalar(props,"Thing.RelativeExtentScale")>0)throw std::runtime_error("Nonzero actor extent adjustment needs native geometry branch");
    auto p=points(seed?seed->map.get("Points"):nullptr);if(p.empty())p=points(props.path("Thing.Points"));
    a.geometryRequired=(!seed||flag(seed->map,"Collision"))&&(a.stopsUnits||a.stopsProjectiles);
    if(p.empty()){a.geometryAvailable=!a.geometryRequired;return;}
    if(p.size()<3)throw std::runtime_error("Actor collision polygon has fewer than three points");
    double signedArea=0;for(size_t i=0;i<p.size();++i){const auto& v=p[i];const auto& w=p[(i+1)%p.size()];signedArea+=double(v.x)*w.y-double(w.x)*v.y;}if(signedArea>0)std::reverse(p.begin(),p.end());
    for(auto& v:p){v.x+=o.x;v.y+=o.y;}
    if(a.squashScale!=1){const float co=std::cos(a.squashAngle),si=-std::sin(a.squashAngle),f=a.squashScale-1;for(auto& v:p){const float d=((v.x-o.x)*f)*co+((v.y-o.y)*f)*si;v.x+=d*co;v.y+=d*si;}}
    const float effectiveScale=o.scale*o.scaleFraction;
    if(effectiveScale!=1)for(auto& v:p){v.x=o.x+(v.x-o.x)*effectiveScale;v.y=o.y+(v.y-o.y)*effectiveScale;}
    if(o.flipHorizontal){for(auto& v:p)v.x=o.x-(v.x-o.x);std::reverse(p.begin(),p.end());}
    if((seed&&flag(seed->map,"FlipVertical"))){for(auto& v:p)v.y=o.y-(v.y-o.y);std::reverse(p.begin(),p.end());}
    if(currentFlag("Thing.RotateGeometry")&&o.angle!=0){const float co=std::cos(-o.angle),si=std::sin(-o.angle);for(auto& v:p){const float x=v.x-o.x,y=v.y-o.y;v.x=o.x+x*co-y*si;v.y=o.y+x*si+y*co;}}
    if(a.geometryFacingFlipped){for(auto& v:p)v.x=o.x-(v.x-o.x);std::reverse(p.begin(),p.end());}
    for(const auto& v:p)if(!std::isfinite(v.x)||!std::isfinite(v.y))throw std::runtime_error("Nonfinite actor geometry");
    a.geometry=std::move(p);a.geometryAvailable=true;
}
}
const ActorMapSeed* ActorWorld::seed(int32_t n)const{const auto i=seeds_.find(n);return i==seeds_.end()?nullptr:&i->second;}
const ActorDefinition* ActorWorld::definition(const std::string& n)const{const auto i=definitions_.find(n);return i==definitions_.end()?nullptr:&i->second;}
ActorComponents* ActorWorld::actor(int32_t n){const auto i=actors_.find(n);return i==actors_.end()?nullptr:i->second.get();}
const ActorComponents* ActorWorld::actor(int32_t n)const{const auto i=actors_.find(n);return i==actors_.end()?nullptr:i->second.get();}
bool ActorWorld::isTargetable(int32_t id,bool& result,std::string& error)const{
    result=false;const auto* a=actor(id);if(!a||!a->registered||!a->object){error="Native IsTargetable owner unavailable";return false;}
    // Thing.IsTargetable1802e6660: primary OwnerUntargetable first, then
    // virtual IsAlive (LifeStatus ALIVE), then LifeComponent.Targetable+0x30.
    // This transient primary veto never overwrites the persistent Life flag.
    if(a->animation.active()||a->primaryAnimationLifetime||a->primaryEffectAnimation||!a->object->animation.empty()){
        if(!primaryUntargetable_){error="Native IsTargetable primary-property provider unavailable";return false;}
        bool untargetable=false;if(!primaryUntargetable_(id,untargetable,error))return false;if(untargetable){error.clear();return true;}
    }
    if(a->lifeStatus!=0){error.clear();return true;}
    if(!a->hasLife){error="Native IsTargetable requires an actual LifeComponent";return false;}
    result=a->targetable;error.clear();return true;
}
bool ActorWorld::resetForMap(std::string& error){
 // World::Reset 0x31622d ThingSystem::Reset, then GridManager::Reset.
 // Portable handles include a map generation so a stale lifetime can never
 // alias the new map's first Thing even when its public/map ID is reused.
 if(generation_==UINT32_MAX){error="Actor map generation exhausted";return false;}
 const auto generation=generation_+1;auto bank=animationBank_;auto collision=std::move(obstacleLocationChanged_);auto auxiliary=std::move(locationAuxiliary_);auto targetability=std::move(primaryUntargetable_);
 *this=ActorWorld{};generation_=generation;animationBank_=std::move(bank);obstacleLocationChanged_=std::move(collision);locationAuxiliary_=std::move(auxiliary);primaryUntargetable_=std::move(targetability);error.clear();return true;
}
bool ActorWorld::setAnimationBank(std::shared_ptr<ActorAnimationBank> bank,std::string& error){
    if(!actors_.empty()){error="Cannot replace an actor animation bank while components exist";return false;}
    animationBank_=std::move(bank);error.clear();return true;
}
bool ActorWorld::loadCatalog(SourceValue root,std::string& error){
    try{
        if(!actors_.empty())throw std::runtime_error("Cannot replace definitions while actors exist");
        if(text(root,"schema")!="hades-source-actor-catalog-v1"||text(root,"target")!="d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254")throw std::runtime_error("Actor catalog identity mismatch");
        // Exporter mirrors SGD_DeserializeAngle205cf0 for authored UnitData
        // RotationSpeed/CosmeticRotationSpeed, retaining native ctor defaults.
        // Reject mixed-unit legacy catalogs instead of guessing at consumption.
        if(!require(root,"native_unit_angles").boolean())throw std::runtime_error("Actor catalog requires native Unit angular speed normalization");
        std::map<std::string,ActorDefinition> defs;std::map<int32_t,ActorMapSeed> seeds;
        auto& sourceDefs=std::get<SourceValue::Object>(root.value).at("definitions");
        for(auto& [key,value]:std::get<SourceValue::Object>(sourceDefs.value)){
            ActorDefinition d;d.key=key;d.name=text(value,"name");d.kind=text(value,"kind");d.properties=std::move(std::get<SourceValue::Object>(value.value).at("properties"));
            if(d.kind!="Unit"&&d.kind!="Obstacle")throw std::runtime_error("Unknown original actor kind");defs.emplace(key,std::move(d));
        }
        auto& map=std::get<SourceValue::Array>(std::get<SourceValue::Object>(root.value).at("map").value);
        for(auto& value:map){ActorMapSeed s;s.id=id(require(value,"id"));s.definition=text(value,"definition");s.map=std::move(std::get<SourceValue::Object>(value.value).at("map"));if(!defs.contains(s.definition)||!seeds.emplace(s.id,std::move(s)).second)throw std::runtime_error("Missing definition or duplicate actor seed");}
        std::map<std::string,std::string> types;
        for(const auto& [key,value]:require(root,"property_types.Unit").object())types.emplace(key,value.text());
        for(const auto& [key,value]:require(root,"property_types.Life").object())lifePropertyTypes_.emplace(key,value.text());
        for(const auto& [key,value]:require(root,"property_types.Thing").object())thingPropertyTypes_.emplace(key,value.text());
        for(const auto& [key,value]:require(root,"property_types.Obstacle").object())obstaclePropertyTypes_.emplace(key,value.text());
        unitPropertyTypes_=std::move(types);definitions_=std::move(defs);seeds_=std::move(seeds);nativeUnitAngles_=true;error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool ActorWorld::initializeMapActor(int32_t sourceId,ObjectSnapshot& output,std::shared_ptr<const WeaponCatalog> weapons,const ActorRandom& random,std::string& error){
    const auto* s=seed(sourceId);if(!s){error="Missing map actor seed";return false;}
    const auto* d=definition(s->definition);if(!d){error="Missing map actor definition";return false;}
    return initializeActor(sourceId,*d,s,scalar(s->map,"Location.X"),scalar(s->map,"Location.Y"),"",output,std::move(weapons),random,error);
}
bool ActorWorld::initializeDynamicObstacle(int32_t n,const std::string& name,float x,float y,const std::string& group,ObjectSnapshot& output,const ActorRandom& random,std::string& error){
    const auto* d=definition("Obstacles/"+name);if(!d){error="Original obstacle definition unavailable: "+name;return false;}
    return initializeActor(n,*d,nullptr,x,y,group,output,nullptr,random,error);
}
// UnitManager.CreateEnemyUnit2fa090/2fa180 selects native UnitData, passes a
// null MapThing and actual spawn XY to Unit.Init2f0f80. Common initialization
// preserves actual Life, Status, Arsenal, Physics and geometry ownership.
bool ActorWorld::initializeDynamicUnit(int32_t n,const std::string& name,float x,float y,const std::string& group,ObjectSnapshot& output,std::shared_ptr<const WeaponCatalog> weapons,const ActorRandom& random,std::string& error){
    const auto* d=definition("Units/"+name);if(!d){error="Original unit definition unavailable: "+name;return false;}
    if(!initializeActor(n,*d,nullptr,x,y,group,output,std::move(weapons),random,error))return false;
    // Unit.Init2f10ec..2f117b: random default angle, else authored degrees
    // (zero retains Thing angle). Rotation/goal starts at this same value.
    float facing=output.angle;
    if(flag(d->properties,"RandomDefaultAngle")){double r=0;if(!random||!random(r,error))return false;facing=float(r)*6.2831854820251465f;}
    else if(const float degrees=scalar(d->properties,"DefaultAngle");degrees!=0)facing=float(double(degrees)*0.017453292519943295);
    output.angle=facing;return true;
}
bool ActorWorld::initializeActor(int32_t sourceId,const ActorDefinition& definition,const ActorMapSeed* s,float x,float y,const std::string& group,ObjectSnapshot& output,std::shared_ptr<const WeaponCatalog> weapons,const ActorRandom& random,std::string& error){
    try{
        if(sourceId<=0||actors_.contains(sourceId))throw std::runtime_error("Invalid or already initialized actor ID");
        const auto* d=&definition;const auto& props=d->properties;
        auto a=std::make_unique<ActorComponents>();a->id=sourceId;a->sourceId=s?sourceId:0;a->definition=d;
        ObjectSnapshot o;o.id=sourceId;o.name=d->name;o.fromMap=s!=nullptr;o.dataType=d->kind=="Unit"?2:1;o.playerControlled=o.dataType==2&&flag(props,"PlayerControlled");
        o.x=x;o.y=y;o.z=scalar(props,"Thing.OffsetZ")+(s?scalar(s->map,"OffsetZ"):0);
        float bodyScale=scalar(props,"Thing.Scale");const float minimum=scalar(props,"Thing.MinScale"),maximum=scalar(props,"Thing.MaxScale");
        if(minimum!=0||maximum!=0){double r=0;if(!random||!random(r,error))throw std::runtime_error(error.empty()?"Actor random scale requires native RNG":error);bodyScale=float(r)*(maximum-minimum)+minimum;}
        o.scale=bodyScale*(s?scalar(s->map,"Scale"):1);o.angle=angle(scalar(props,"Thing.StartAngle"),s?radians(scalar(s->map,"Angle")):0);
        o.screenSpace=flag(props,"Thing.UseScreenLocation");o.visible=!flag(props,"Thing.Invisible");o.active=false;o.alive=false;o.doCreate=s?flag(s->map,"DoCreate"):true;o.flipHorizontal=s&&flag(s->map,"FlipHorizontal");
        o.animation=text(props,"Thing.Graphic");
        if(s)for(const auto& g:require(s->map,"GroupNames").array())o.groups.push_back(g.text());
        else o.groups.push_back(group);for(const auto& g:o.groups)if(!g.empty()){o.group=g;break;}
        a->mapParallax=s?scalar(s->map,"ParallaxAmount"):1;
        if(const auto* mode=props.path("Thing.SortMode")){
            // Catalog keeps enum defaults numerically and authored values by name.
            if(std::holds_alternative<std::string>(mode->value))a->sortMode=parseSortMode(mode->text());
            else{const double n=mode->number();if(!(n>=0&&n<=3&&n==std::trunc(n)))throw std::runtime_error("Original ThingData SortMode outside enum");a->sortMode=uint8_t(n);}
        }
        a->timeModifierFraction=scalar(props,"Thing.TimeModifierFraction"); // InitThing2dd953..959.
        a->squashAngle=s?radians(scalar(s->map,"SkewAngle")):0;a->squashScale=s?scalar(s->map,"SkewScale"):1;a->ambient=scalar(props,"Thing.Ambient");const float mapAmbient=s?scalar(s->map,"Ambient"):-1;if(mapAmbient>=0)a->ambient=mapAmbient;
        const SourceValue::Array* mapColor=s?&require(s->map,"Color").array():nullptr;if(mapColor&&mapColor->size()!=4)throw std::runtime_error("Original actor color requires RGBA");
        const char* channels[]{"Red","Green","Blue","Alpha"};for(size_t i=0;i<4;++i){
            const float channel=scalar(props,std::string("Thing.Color.")+channels[i]);
            const auto base=uint8_t(std::clamp(channel,0.f,1.f)*255.f);
            const auto mapByte=mapColor?uint8_t((*mapColor)[i].number()):uint8_t(255);
            a->baseColorBytes[i]=uint8_t(uint16_t(base)*mapByte/255);a->color[i]=float(a->baseColorBytes[i])/255.f;
        }
        const char* hsv[]{"Hue","Saturation","Value"};for(size_t i=0;i<3;++i)a->hsv[i]=scalar(props,std::string("Thing.")+hsv[i])+(s?scalar(s->map,hsv[i]):0);
        a->stopsUnits=flag(props,"Thing.StopsUnits");a->stopsProjectiles=flag(props,"Thing.StopsProjectiles");a->hasLife=o.dataType==2||flag(props,"NeedsLife");
        // Thing.InitPhysics (2dda20) only allocates a free component when Grip
        // is below99999 and the original map AttachToID is absent/nonpositive.
        if(scalar(props,"Thing.Grip")<99999.f&&(!s||scalar(s->map,"AttachToID")<=0)){
            a->physics=std::make_unique<PhysicsState>();a->physics->gravity=scalar(props,"Thing.Gravity");
            a->physics->bounce=scalar(props,"Thing.BounceMultiplier");a->physicsPending=true;
        }
        if(a->hasLife)a->invulnerable=flag(props,"Life.Invulnerable");
        if(a->hasLife&&o.dataType!=2){
            // Obstacle::Init VA1802c2ce8..1802c2dce: NeedsLife(+0x438) allocates
            // LifeComponent (Targetable=1, ArmorCoverage=2pi, multipliers1),
            // then copies LifeData ArmorCoverage/InvulnerableCoverage/
            // CollisionDamageMultiplier/Invulnerable, ActiveHitFxCooldown=1,
            // Reflection=ObstacleData Reflection(+0x43c), IsObstacle=1.
            a->life.initialized=true;a->life.isObstacle=true;a->life.activeHitFxCooldown=1;
            a->life.armorCoverage=scalar(props,"Life.ArmorCoverage");
            a->life.invulnerableCoverage=scalar(props,"Life.InvulnerableCoverage");
            a->life.collisionDamageMultiplier=scalar(props,"Life.CollisionDamageMultiplier");
            a->life.reflection=scalar(props,"Reflection");
        }
        if(o.dataType==2){
            a->canMoveData=flag(props,"CanMove");a->canAttackData=flag(props,"CanAttack");
            a->speed=scalar(props,"Speed");a->lowSpeedThreshold=scalar(props,"LowSpeedThreshold");a->lowSpeedFraction=scalar(props,"LowSpeedFraction");
            a->lifeStatus=0;a->allowAnyFire=true;a->allowInput=true;
            // Actual Unit::Init inlined Life construction and InitLife copies,
            // VA1802f1047..1802f10d0. Live and DataValue fields are distinct.
            a->life.initialized=true;
            // Unit.Init VA1802f10d7..e2 copies UnitData+69 once to Life+32.
            // Later Data setters do not silently change this live flag.
            a->life.isPlayerControlled=o.playerControlled;
            a->life.armorCoverage=scalar(props,"Life.ArmorCoverage");
            a->life.invulnerableCoverage=scalar(props,"Life.InvulnerableCoverage");
            a->life.collisionDamageMultiplier=scalar(props,"Life.CollisionDamageMultiplier");
            a->life.reflection=scalar(props,"Life.Reflection");
            a->status=std::make_unique<UnitStatus>();if(!a->status->initializeFreshUnit(error))throw std::runtime_error(error);
            a->arsenal=std::make_unique<WeaponArsenal>(o.id,o.name,weapons);
            const char* slots[]{"OnHitWeapon","OnStunWeapon","OnDeathWeapon","OnTouchdownWeapon","CollisionWeapon","InitiatedCollisionWeapon","ContinuousWeapon"};
            for(const auto* slot:slots){const auto name=text(props,slot);if(!name.empty()&&!a->arsenal->equip(name,error))throw std::runtime_error("Native unit initial weapon "+name+": "+error);}
        }
        if(animationBank_&&animationBank_->find(o.animation))a->animation.play(*animationBank_,o.animation);
        geometry(*a,o,s);
        a->entityHandle=allocateEntityLifetime(error);if(!a->entityHandle)throw std::runtime_error(error);o.entityHandle=a->entityHandle;
        actors_.emplace(o.id,std::move(a));output=std::move(o);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool ActorWorld::registerActor(int32_t n,ObjectSnapshot& object,std::string& error){auto* a=actor(n);if(!a||a->registered||object.id!=n||object.entityHandle!=a->entityHandle){error="Invalid actor manager registration";return false;}a->object=&object;a->registered=true;registrationOrder_.push_back(n);object.active=true;object.alive=true;error.clear();return true;}
bool ActorWorld::linkStructuralChild(int32_t parent,int32_t child,std::string& error){auto* p=actor(parent);auto* c=actor(child);if(!p||!c||!p->registered||!c->registered||p->definition->kind!="Obstacle"||c->definition->kind!="Obstacle"||(c->parentId&&c->parentId!=parent)){error="Invalid structural obstacle handle linkage";return false;}if(std::find(p->children.begin(),p->children.end(),child)==p->children.end())p->children.push_back(child);c->parentId=parent;
    // Obstacle::OnParentAdded VA1802c4980: unless the parent data Name is HeroStart.
    if(p->definition->name!="HeroStart")c->sortMode=1;error.clear();return true;}
bool ActorWorld::refreshGeometry(int32_t n,std::string& error){
    try{auto* a=actor(n);if(!a||!a->registered||!a->object){error="Geometry mutation needs registered actor";return false;}
        geometry(*a,*a->object,a->sourceId?seed(a->sourceId):nullptr);a->object->geometryDirty=false;error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool ActorWorld::setAngle(int32_t n,float requested,std::string& error){
    try{
        auto* a=actor(n);if(!a||!a->registered||!a->object||!a->definition)throw std::runtime_error("Thing.SetAngle requires registered actor/body/data");
        if(!std::isfinite(requested))throw std::runtime_error("Thing.SetAngle requires finite requested angle");
        auto& body=*a->object;constexpr float tau=6.2831854820251465f;
        float next=requested-std::floor(requested/tau)*tau;next-=std::floor(next/tau)*tau;
        if(!std::isfinite(next))throw std::runtime_error("Thing.SetAngle normalization overflow");
        // Native Thing.SetAngle2e0ce0 tests RAW requested angle before wrapping,
        // and prioritizes RotateGeometry over the MirrorGeometry branch.
        bool flip=false;
        const auto* rotate=currentProperty(n,"Thing.RotateGeometry");const auto* mirror=currentProperty(n,"Thing.MirrorGeometry");
        if(!rotate||!mirror)throw std::runtime_error("Thing.SetAngle geometry flags unresolved");
        if(!a->geometry.empty()&&!rotate->boolean()&&mirror->boolean()){
            constexpr float halfPi=1.5707963705062866f,threeHalfPi=4.71238899230957f;
            const auto left=[](float angle){return angle>halfPi&&angle<threeHalfPi;};
            flip=left(body.angle)!=left(requested);
        }
        body.angle=next;if(flip)a->geometryFacingFlipped=!a->geometryFacingFlipped;
        body.graphicsDirty=body.geometryDirty=true;
        if(!refreshGeometry(n,error))return false;
        // Unit virtual+70 is Thing.MarkSortScoreChanged; the shared dispatcher
        // also preserves an Obstacle override's real LocationChanged barrier.
        return propertiesDirty(n,error);
    }catch(const std::exception& e){error=e.what();return false;}
}
bool ActorWorld::location(int32_t n,float& x,float& y,std::string& error)const{
    const auto* a=actor(n);if(!a||!a->object){error="Thing location needs actor body";return false;}
    x=y=0;std::vector<int32_t> seen;
    while(a){
        if(std::find(seen.begin(),seen.end(),a->id)!=seen.end()){error="Cyclic native Thing attachment rejected";return false;}seen.push_back(a->id);
        const auto* p=actor(a->attachedParent);
        if(!p||!p->registered||!a->attachedParentLifetime||p->entityHandle!=a->attachedParentLifetime){x+=a->object->x;y+=a->object->y;break;}
        x+=a->attachOffsetX;y+=a->attachOffsetY;a=p;
    }
    error.clear();return true;
}
bool ActorWorld::zLocation(int32_t n,float& z,std::string& error)const{
    const auto* a=actor(n);if(!a||!a->object){error="Thing Z needs actual body";return false;}
    z=0;std::vector<int32_t> seen;
    while(a){
        if(std::find(seen.begin(),seen.end(),a->id)!=seen.end()){error="Cyclic native Z attachment rejected";return false;}seen.push_back(a->id);
        const auto* p=actor(a->attachedParent);if(!p||!p->registered||!a->attachedParentLifetime||p->entityHandle!=a->attachedParentLifetime){z+=a->object->z;break;}
        z+=a->attachOffsetZ;a=p;
    }
    error.clear();return true;
}
bool ActorWorld::attach(int32_t child,int32_t parent,float x,float y,float z,bool dynamicScale,std::string& error){
    auto* c=actor(child);auto* p=actor(parent);
    if(!c||!p||!c->registered||!p->registered){error.clear();return true;}
    std::vector<int32_t> seen;const ActorComponents* ancestor=p;
    while(ancestor){
        if(ancestor==c||std::find(seen.begin(),seen.end(),ancestor->id)!=seen.end()){error="Cyclic native Thing attachment rejected";return false;}seen.push_back(ancestor->id);
        const auto* next=actor(ancestor->attachedParent);if(!next||!next->registered||next->entityHandle!=ancestor->attachedParentLifetime)break;ancestor=next;
    }
    // Attach action reads Body.Scale (+0x48), not the transition fraction.
    if(dynamicScale){x*=p->object->scale;y*=p->object->scale;z*=p->object->scale;}
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)){error="Nonfinite Thing attach offset";return false;}
    if(auto* old=actor(c->attachedParent);old&&old->registered&&old->entityHandle==c->attachedParentLifetime){auto& v=old->attachedChildren;v.erase(std::remove(v.begin(),v.end(),child),v.end());}
    c->attachedParent=parent;c->attachedParentLifetime=p->entityHandle;c->attachOffsetX=x;c->attachOffsetY=y;c->attachOffsetZ=z;
    if(std::find(p->attachedChildren.begin(),p->attachedChildren.end(),child)==p->attachedChildren.end())p->attachedChildren.push_back(child);
    // SetAttachOffset calls SetLocation with the effective parent location, so
    // stored location remains the last actual location if the parent expires.
    float px=0,py=0;if(!location(parent,px,py,error))return false;
    c->object->x=px+x;c->object->y=py+y;c->object->graphicsDirty=true;c->object->geometryDirty=true;
    return refreshGeometry(child,error);
}
// Thing.Detach2e5ad0 snapshots effective world XY/Z before clearing the link;
// virtual SetLocation(check=false), SetZLocation then preserve that position.
// The caller owns Physics.Halt/audio/ECS admission after these setters.
bool ActorWorld::detach(int32_t child,bool& detached,std::string& error){
    detached=false;auto* c=actor(child);if(!c||!c->registered||!c->object){error.clear();return true;}
    auto* p=actor(c->attachedParent);if(!p||!p->registered||p->entityHandle!=c->attachedParentLifetime){error.clear();return true;}
    float x=0,y=0,z=0;if(!location(child,x,y,error)||!zLocation(child,z,error))return false;
    auto& ids=p->attachedChildren;ids.erase(std::remove(ids.begin(),ids.end(),child),ids.end());
    c->attachedParent=0;c->attachedParentLifetime=0;c->attachOffsetX=c->attachOffsetY=c->attachOffsetZ=0;
    c->object->x=x;c->object->y=y;c->object->z=z;c->object->graphicsDirty=c->object->geometryDirty=true;detached=true;
    if(c->definition->kind=="Obstacle")return obstacleLocationChanged(child,false,error);
    return refreshGeometry(child,error)&&propertiesDirty(child,error);
}
bool ActorWorld::adjustFraction(int32_t id,bool scale,float target,const BodyTransitionOptions& options,std::string& error){
    auto* a=actor(id);if(!a||!a->registered||!a->object){error="Body adjustment needs actual registered actor";return false;}
    auto& transition=a->fractions[scale?0:1];bool changed=false;
    if(!transition.adjust(target,1,options,changed,error))return false;
    if(changed){if(scale)a->object->scaleFraction=transition.current();else a->object->alpha=transition.current();a->object->graphicsDirty=true;if(scale&&!refreshGeometry(id,error))return false;}
    error.clear();return true;
}
// Thing.RunScriptAction2e2d11/2e2e1b -> Body helpers+140/+1a0.
// SetupGeometry2dcff4..2dd11a uses uniform scale only, independently of these axes.
bool ActorWorld::adjustScaleAxis(int32_t id,size_t axis,float target,const BodyTransitionOptions& options,std::string& error){
    auto* a=actor(id);if(!a||!a->registered||!a->object||axis>1){error="Body scale-vector adjustment needs actual registered actor";return false;}
    bool changed=false;if(!a->fractions[axis+2].adjust(target,1,options,changed,error))return false;
    if(changed){a->object->graphicsDirty=true;if(!refreshGeometry(id,error))return false;}
    error.clear();return true;
}
bool ActorWorld::adjustAngle(int32_t id,float target,const BodyTransitionOptions& options,std::string& error){
    auto* a=actor(id);if(!a||!a->registered||!a->object){error="Body angle adjustment needs actual registered actor";return false;}
    bool changed=false;if(!a->angleTransition.adjust(target,0,options,changed,error))return false;
    if(changed&&!setAngle(id,a->angleTransition.current(),error))return false;
    error.clear();return true;
}
bool ActorWorld::adjustColorChannel(int32_t id,size_t channel,float target,float initial,const BodyTransitionOptions& options,std::string& error){
    auto* a=actor(id);if(!a||!a->registered||!a->object||channel>3){error="Body color adjustment needs actual registered actor";return false;}
    auto& transition=channel<3?a->colorChannels[channel]:a->fractions[1];bool changed=false;
    if(!transition.adjust(target,initial,options,changed,error))return false;
    if(changed){if(channel<3)a->tint[channel]=transition.current();else a->object->alpha=transition.current();a->object->graphicsDirty=true;}
    error.clear();return true;
}
bool ActorWorld::advanceBody(int32_t id,float elapsed,float undo,std::string& error){
    auto* a=actor(id);if(!a||!a->registered||!a->object){error="Body update needs actual registered actor";return false;}
    for(size_t i=0;i<a->fractions.size();++i){auto& t=a->fractions[i];if(!t.active())continue;if(!t.advance(elapsed,undo,error))return false;
        if(i==0){a->object->scaleFraction=t.current();if(!refreshGeometry(id,error))return false;}else if(i==1)a->object->alpha=t.current();else if(!refreshGeometry(id,error))return false;a->object->graphicsDirty=true;
    }
    if(a->angleTransition.active()){if(!a->angleTransition.advance(elapsed,undo,error)||!setAngle(id,a->angleTransition.current(),error))return false;}
    for(size_t i=0;i<a->colorChannels.size();++i){auto& t=a->colorChannels[i];if(!t.active())continue;if(!t.advance(elapsed,undo,error))return false;
        a->tint[i]=t.current();a->object->graphicsDirty=true;
    }
    error.clear();return true;
}
bool ActorWorld::ensurePhysics(int32_t id,std::string& error){
    try{
        auto* a=actor(id);if(!a||!a->registered||!a->object)throw std::runtime_error("EnsurePhysics needs actual registered Thing");
        if(a->physics){error.clear();return true;}
        // EnsurePhysics allocates once, then copies the live ThingData gravity
        // and bounce values. This is separate from reset's default1024/0.
        const auto* gravity=currentProperty(id,"Thing.Gravity");const auto* bounce=currentProperty(id,"Thing.BounceMultiplier");
        if(!gravity||!bounce)throw std::runtime_error("EnsurePhysics source gravity/bounce unavailable");
        const float g=float(gravity->number()),b=float(bounce->number());
        if(!std::isfinite(g)||!std::isfinite(b))throw std::runtime_error("Nonfinite native Physics initialization");
        auto physics=std::make_unique<PhysicsState>();physics->gravity=g;physics->bounce=b;a->physics=std::move(physics);a->physicsPending=true;
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool ActorWorld::applyScriptUpwardForce(int32_t id,float speed,float hangTime,std::string& error){
    auto* a=actor(id);if(!a||!a->registered){error.clear();return true;}
    if(!std::isfinite(speed)||!std::isfinite(hangTime)){error="Nonfinite ApplyUpwardForce arguments";return false;}
    if(!ensurePhysics(id,error))return false;
    const auto* maximum=currentProperty(id,"Thing.UpwardTerminalVelocity");
    if(!maximum){error="Original Thing UpwardTerminalVelocity unavailable";return false;}
    PhysicsForceOwner owner;owner.maxUpwardVelocity=float(maximum->number());
    // ScriptAction supplies selfApplied=true, irrespective of table fields.
    // A positive HangTime replaces Speed using this actual component's gravity.
    const float force=hangTime>0?(hangTime*a->physics->gravity)*.5f:speed;
    PhysicsForceCallbacks callbacks;
    callbacks.onUpwardForce=[a](float,std::string& detail){a->physicsPending=true;detail.clear();return true;};
    callbacks.getZLocation=[this,id](float& z,std::string& detail){return zLocation(id,z,detail);};
    return applyPhysicsUpwardForce(*a->physics,owner,force,true,std::numeric_limits<float>::max(),callbacks,error);
}
bool ActorWorld::unitCanMove(int32_t id,float playerSpeedMultiplier,bool& result,std::string& error)const{
    const auto* a=actor(id);
    if(!a||!a->registered||!a->arsenal||!a->status||!a->object){error="CanMove requires actual Unit/status";return false;}
    if(a->animation.active()){
        const auto immobile=animationBank_->exactOwnerImmobile(a->animation.clipIndex());
        if(!immobile){error="Source primary OwnerImmobile property unavailable";return false;}
        if(*immobile){result=false;error.clear();return true;}
    }else if(a->primaryEffectAnimation||a->primaryAnimationLifetime){
        if(!a->primaryOwnerImmobile){error="Source primary OwnerImmobile unavailable";return false;}
        if(*a->primaryOwnerImmobile){result=false;error.clear();return true;}
    }else if(!a->object->animation.empty()){
        error="CanMove requires actual current source primary animation";return false;
    }
    float effective=(a->speed+a->speedAddition)*a->speedModifier;
    if(a->object->playerControlled)effective*=playerSpeedMultiplier;
    if(!std::isfinite(effective)){error="Nonfinite native Unit effective movement speed";return false;}
    result=effective>0&&a->status->canMove();error.clear();return true;
}
bool ActorWorld::unitAirborne(int32_t id,bool& result,std::string& error)const{
    const auto* a=actor(id);if(!a||!a->registered||!a->arsenal||!a->object){error="IsAirborne requires actual Unit";return false;}
    if(a->animation.active()){
        const auto airborne=animationBank_->exactAirborne(a->animation.clipIndex(),a->animation.slideIndex());
        if(!airborne){error="Actual current source Slide Airborne flag unavailable";return false;}
        result=*airborne;
    }else if(a->primaryAirborne)result=*a->primaryAirborne;
    else if(a->object->animation.empty())result=false; // Proven non-Slide branch.
    else{error="IsAirborne requires initialized current source primary";return false;}
    error.clear();return true;
}
bool ActorWorld::weaponOwnerFrame(int32_t id,float worldTime,float playerSpeed,
    bool editing,bool inputAllowed,WeaponOwnerFrame& output,std::string& error)const{
    const auto* a=actor(id);WeaponOwnerFrame frame;frame.ownerId=id;
    if(!a||!a->registered||!a->object||!a->arsenal){output=frame;error.clear();return true;}
    if(!std::isfinite(worldTime)||!std::isfinite(playerSpeed)){error="Nonfinite native weapon owner clock/speed configuration";return false;}
    const auto* immune=currentProperty(id,"ImmuneToStun");if(!immune||!a->status){error="Weapon owner requires actual UnitData/status";return false;}
    frame.exists=true;frame.alive=a->object->alive&&a->lifeStatus==0;frame.enabled=a->allowAnyFire;
    frame.canAttack=a->status->canAttack(editing,immune->boolean(),false);
    frame.canAttackAllowCancellation=a->status->canAttack(editing,immune->boolean(),true);
    if(!unitCanMove(id,playerSpeed,frame.canMove,error))return false;
    if(!location(id,frame.x,frame.y,error)||!zLocation(id,frame.z,error))return false;
    frame.totalZ=frame.z;bool airborne=false;if(!unitAirborne(id,airborne,error))return false;
    if(airborne){
        const auto airZ=animationBank_->exactAnimationZ(a->animation.clipIndex());
        if(!airZ){error="Source current Slide AirborneZ unavailable";return false;}frame.totalZ+=*airZ;
    }
    auto blocks=a->animation.animationBlocksAttack();
    if(!blocks&&(a->primaryEffectAnimation||a->primaryAnimationLifetime))blocks=a->primaryDisableWeapons;
    if(!blocks&&!a->object->animation.empty()){error="Weapon owner requires actual primary DisableWeapons field";return false;}
    frame.animationBlocksAttack=blocks.value_or(false);frame.angle=a->object->angle;
    frame.worldTime=worldTime;frame.inputAllowed=inputAllowed;
    output=frame;error.clear();return true;
}
bool ActorWorld::destroyActor(int32_t n,std::string& error){auto* a=actor(n);if(!a||!a->registered){error="Actor is not registered";return false;}a->registered=false;if(a->object){a->object->alive=false;a->object->active=false;a->object->entityHandle=0;}a->entityHandle=0;error.clear();return true;}
const SourceValue* ActorWorld::currentProperty(int32_t n,const std::string& key)const{
    const auto* a=actor(n);if(!a)return nullptr;const auto i=a->propertyOverrides.find(key);if(i!=a->propertyOverrides.end())return &i->second;
    if(const auto* value=a->definition->properties.path(key))return value;
    // Lua nil cannot retain dictionary entries. The source-bound field-type
    // record distinguishes a proven null native pointer from unknown data.
    if(key.starts_with("Thing.")){const auto type=thingPropertyTypes_.find(key.substr(6));if(type!=thingPropertyTypes_.end()&&type->second=="null_pointer"){static const SourceValue nullPointer;return &nullPointer;}}
    return nullptr;
}
uint64_t ActorWorld::allocateEntityLifetime(std::string& error){
    if(nextEntity_==UINT32_MAX){error="World entity handle space exhausted";return 0;}
    error.clear();return (uint64_t(generation_)<<32)|nextEntity_++;
}
bool ActorWorld::setUnitProperty(int32_t n,const std::string& key,const PropertyInput& input,PropertyChange change,std::string& error){
    try{
        auto* a=actor(n);if(!a||!a->registered||!a->arsenal){error.clear();return true;}
        const auto type=unitPropertyTypes_.find(key);
        if(type==unitPropertyTypes_.end())throw std::runtime_error("UnitData field/MoveAI fallback not reconstructed: "+key);
        const auto* v=currentProperty(n,key);if(!v)throw std::runtime_error("UnitData current value unavailable: "+key);
        PropertyPrimitive current;
        if(type->second=="bool")current=v->boolean();else if(type->second=="float32")current=float(v->number());
        else if(type->second=="int32")current=int32_t(v->number());else current=v->text();
        PropertyPrimitive next;
        if(mutatePrimitiveProperty(current,input,change,next,error)!=PropertyMutation::Applied)return false;
        a->propertyOverrides[key]=propertyPrimitiveToSource(next);
        if(key=="CanMove")a->canMoveData=std::get<bool>(next);
        else if(key=="CanAttack")a->canAttackData=std::get<bool>(next);
        else if(key=="Speed")a->speed=std::get<float>(next);
        else if(key=="LowSpeedThreshold")a->lowSpeedThreshold=std::get<float>(next);
        else if(key=="LowSpeedFraction")a->lowSpeedFraction=std::get<float>(next);
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
// ScriptAction::GetUnitDataValue VA180266220 selects the first actual Unit,
// then GetPropertyInfo<UnitData> VA1800f5910 / GetPropertyValue VA1802027f0.
// The caller owns native target-set ordering; missing units return nullptr.
bool ActorWorld::unitDataValue(int32_t id,const std::string& key,SourceValue& value,std::string& error)const{
    try{
        value={};const auto* a=actor(id);
        if(!a||!a->registered||!a->definition||a->definition->kind!="Unit"){error.clear();return true;}
        const auto type=unitPropertyTypes_.find(key);
        if(type==unitPropertyTypes_.end()){
            // UnitData reflection delegates unknown names to GameData only;
            // it does not traverse dotted paths or fall back to MoveAIData.
            if(key!="Name"&&key!="Organization"&&key!="InheritFrom"){
                for(const auto* complex:{"LifeData","ThingData","MoveAIData","PlayerUnitData"})
                    if(key==complex)throw std::runtime_error("UnitData complex reflection query not reconstructed: "+key);
                error.clear();return true;
            }
        }
        const auto* current=currentProperty(id,key);
        if(!current)throw std::runtime_error("UnitData reflected query value unavailable: "+key);
        if(std::holds_alternative<SourceValue::Object>(current->value)||std::holds_alternative<SourceValue::Array>(current->value))throw std::runtime_error("UnitData complex reflection result not reconstructed: "+key);
        if(type==unitPropertyTypes_.end()||type->second=="string")value=SourceValue{current->text()};
        else if(type->second=="bool")value=SourceValue{current->boolean()};
        else if(type->second=="float32")value=SourceValue{double(float(current->number()))};
        else if(type->second=="int32")value=SourceValue{double(int32_t(current->number()))};
        else if(type->second!="null_pointer")throw std::runtime_error("UnitData reflected result type not reconstructed: "+type->second);
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
// ScriptAction::SetObstacleProperty VA180273230 writes the embedded instance
// ObstacleData, then always dispatches virtual OnPropertiesDirty (+0x118).
// DataValue is not read. Existing consumers use currentProperty at their
// original boundaries, and propertiesDirty refreshes the real collision/grid.
bool ActorWorld::setObstacleProperty(int32_t id,const std::string& key,const PropertyInput& input,PropertyChange change,std::string& error){
    try{
        auto* a=actor(id);if(!a||!a->registered||!a->definition||a->definition->kind!="Obstacle"){error.clear();return true;}
        const auto type=obstaclePropertyTypes_.find(key);
        if(type==obstaclePropertyTypes_.end()){
            for(const auto* complex:{"ThingData","LifeData","CollisionSound","ImpactSound","Name","Organization","InheritFrom"})
                if(key==complex)throw std::runtime_error("ObstacleData reflected field not reconstructed: "+key);
            // ReflectionHelper Unknown has no write destination. The action
            // still calls OnPropertiesDirty even when reflection returns false.
            return propertiesDirty(id,error);
        }
        const auto* value=currentProperty(id,key);if(!value)throw std::runtime_error("ObstacleData current value unavailable: "+key);
        PropertyPrimitive current;
        if(type->second=="bool")current=value->boolean();else if(type->second=="float32")current=float(value->number());else if(type->second=="int32")current=int32_t(value->number());else current=value->text();
        PropertyPrimitive next;if(mutatePrimitiveProperty(current,input,change,next,error)!=PropertyMutation::Applied)return false;
        a->propertyOverrides[key]=propertyPrimitiveToSource(next);
        return propertiesDirty(id,error);
    }catch(const std::exception& e){error=e.what();return false;}
}
uint8_t parseSortMode(const std::string& name){
    const auto is=[&](const char* v){return strcasecmp(name.c_str(),v)==0;};
    if(is("FromParent"))return 1;if(is("Id"))return 2;if(is("Secondary"))return 3;return 0;
}
bool ActorWorld::setThingProperty(int32_t n,const std::string& key,const PropertyInput& input,PropertyChange change,std::string& error){
    try{
        auto* a=actor(n);if(!a||!a->registered){error.clear();return true;}
        if(key=="SortMode"){
            // ReflectionHelper sets ThingData.SortMode through ParseEnum<SortMode>;
            // Thing::SetThingProperty VA1802e66d0 then copies it to the live
            // byte+0x38 unless that byte is already FromParent(1), then
            // OnPropertiesDirty (vtable+0x118).
            const auto* text=std::get_if<std::string>(&input);
            if(!text||change!=PropertyChange::Absolute){error="ThingData SortMode requires an Absolute enum name";return false;}
            const uint8_t mode=parseSortMode(*text);
            static const char* names[]{"Isometric","FromParent","Id","Secondary"};
            a->propertyOverrides["Thing.SortMode"]=SourceValue{std::string(names[mode])};
            if(a->sortMode!=1)a->sortMode=mode;
            return propertiesDirty(n,error);
        }
        const auto type=thingPropertyTypes_.find(key);if(type==thingPropertyTypes_.end()){error="ThingData field/side effect not reconstructed: "+key;return false;}
        if(key=="CausesOcclusion"){
            // Thing::SetThingProperty 0x1802e66d0: ReflectionHelper writes
            // ThingData+0x22e, then (0x1802e6d05..0x1802e6de8) the side effect:
            // true -> if Thing+8 flags contain 0x121 (APPROXIMATION: a live
            // registered obstacle), AddToSet(GetRoot(Thing+0x644), 7); false ->
            // if OcclusionOpacity(+0x238) >= 0, BodyComponent::Adjust 0x1802b7440
            // on Body+0x320 alpha {target 1, initial 1, OcclusionInterpolateDuration
            // (+0x23c)} - the same record ObstacleManager::Update 0x1802c98be
            // builds for an unoccluded group. Then OnPropertiesDirty (vfunc+0x118).
            const auto* value=currentProperty(n,"Thing.CausesOcclusion");if(!value)throw std::runtime_error("Original ThingData value unavailable: CausesOcclusion");
            PropertyPrimitive next;if(mutatePrimitiveProperty(PropertyPrimitive{value->boolean()},input,change,next,error)!=PropertyMutation::Applied)return false;
            const bool on=std::get<bool>(next);a->propertyOverrides["Thing.CausesOcclusion"]=propertyPrimitiveToSource(next);
            if(on){if(a->definition&&a->definition->kind=="Obstacle"&&a->object&&a->object->alive&&!a->isDisposed){int32_t root=n;for(int guard=0;guard<64;++guard){const auto* r=actor(root);if(!r||!r->parentId||!actor(r->parentId))break;root=r->parentId;}occlusionRootSet.insert(root);}}
            else{const auto* opacity=currentProperty(n,"Thing.OcclusionOpacity");const auto* duration=currentProperty(n,"Thing.OcclusionInterpolateDuration");if(!opacity||!duration)throw std::runtime_error("Original occlusion ThingData fields unavailable");
                if(float(opacity->number())>=0.f){BodyTransitionOptions o;o.duration=float(duration->number());if(!adjustColorChannel(n,3,1.f,1.f,o,error))return false;}}
            return propertiesDirty(n,error);
        }
        for(const auto* special:{"ParallaxAmount","Graphic","CausesOcclusion","StopsLight","LerpCueParamDistance"})
            if(key==special){error="ThingData mutation requires native side-effect consumer: "+key;return false;}
        const std::string path="Thing."+key;const auto* value=currentProperty(n,path);if(!value)throw std::runtime_error("Original ThingData value unavailable: "+key);
        PropertyPrimitive current;
        if(type->second=="bool")current=value->boolean();else if(type->second=="float32")current=float(value->number());else if(type->second=="int32")current=int32_t(value->number());else current=value->text();
        PropertyPrimitive next;if(mutatePrimitiveProperty(current,input,change,next,error)!=PropertyMutation::Applied)return false;
        // Property mutation changes the actual copied ThingData. Components
        // read this current value at their original read boundaries.
        a->propertyOverrides[path]=propertyPrimitiveToSource(next);
        if(key=="StopsUnits")a->stopsUnits=std::get<bool>(next);
        else if(key=="StopsProjectiles")a->stopsProjectiles=std::get<bool>(next);
        else if(key=="Gravity"&&a->physics)a->physics->gravity=std::get<float>(next);
        else if(key=="BounceMultiplier"&&a->physics)a->physics->bounce=std::get<float>(next);
        else if(key=="Ambient")a->ambient=std::get<float>(next);
        return propertiesDirty(n,error);
    }catch(const std::exception& e){error=e.what();return false;}
}
bool ActorWorld::setLifeProperty(int32_t n,const std::string& key,const PropertyInput& input,PropertyChange change,bool data,std::string& error){
    try{
        auto* a=actor(n);if(!a||!a->registered||!a->hasLife){error.clear();return true;}
        if(data){
            const auto type=lifePropertyTypes_.find(key);if(type==lifePropertyTypes_.end()){error="LifeData field not reconstructed: "+key;return false;}
            const std::string path="Life."+key;const auto* value=currentProperty(n,path);if(!value)throw std::runtime_error("Original LifeData property missing: "+key);
            PropertyPrimitive old;
            if(type->second=="bool")old=value->boolean();else if(type->second=="float32")old=float(value->number());else if(type->second=="int32")old=int32_t(value->number());else old=value->text();
            PropertyPrimitive next;if(mutatePrimitiveProperty(old,input,change,next,error)!=PropertyMutation::Applied)return false;
            a->propertyOverrides[path]=propertyPrimitiveToSource(next);return true;
        }
        if(!a->life.initialized){error="Live Life fields require actual InitLife reconstruction for this actor";return false;}
        bool* b=nullptr;float* f=nullptr;int32_t* i=nullptr;auto& l=a->life;
        if(key=="IsInvulnerable")b=&a->invulnerable;else if(key=="Targetable")b=&a->targetable;else if(key=="IsObstacle")b=&l.isObstacle;
        else if(key=="DodgeChance")f=&l.dodgeChance;else if(key=="InvulnerableCoverage")f=&l.invulnerableCoverage;
        else if(key=="ArmorCoverage")f=&l.armorCoverage;else if(key=="CollisionDamageMultiplier")f=&l.collisionDamageMultiplier;
        else if(key=="DamageTakenMultiplier")f=&l.damageTakenMultiplier;else if(key=="ConsecutiveHitWindow")f=&l.consecutiveHitWindow;
        else if(key=="ActiveHitFxCooldown")f=&l.activeHitFxCooldown;else if(key=="Reflection")f=&l.reflection;
        else if(key=="ActiveHitFxCount")i=&l.activeHitFxCount;else if(key=="ConsecutiveHits")i=&l.consecutiveHits;
        else{error.clear();return true;} // Actual live reflection Unknown -> no destination.
        PropertyPrimitive old;if(b)old=*b;else if(f)old=*f;else old=*i;
        PropertyPrimitive next;if(mutatePrimitiveProperty(old,input,change,next,error)!=PropertyMutation::Applied)return false;
        if(b)*b=std::get<bool>(next);else if(f)*f=std::get<float>(next);else *i=std::get<int32_t>(next);
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
std::string ActorWorld::dump()const{std::ostringstream out;out<<"ACTOR_WORLD definitions="<<definitions_.size()<<" mapSeeds="<<seeds_.size()<<" components="<<actors_.size()<<'\n';for(const auto& [id,a]:actors_)out<<"ACTOR id="<<id<<" source="<<a->sourceId<<" handle="<<a->entityHandle<<" type="<<a->definition->kind<<" name="<<a->definition->name<<" registered="<<a->registered<<" collisionDamageMultiplier="<<a->life.collisionDamageMultiplier<<" hasLife="<<a->hasLife<<" geometry="<<a->geometry.size()<<" geometryAvailable="<<a->geometryAvailable<<" required="<<a->geometryRequired<<" attachedParent="<<a->attachedParent<<" attachedLifetime="<<a->attachedParentLifetime<<" parent="<<a->parentId<<" children="<<a->children.size()<<" weapons="<<(a->arsenal?a->arsenal->snapshot().size():0)<<'\n';return out.str();}
}
