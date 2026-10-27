#include "interaction_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace hades {
namespace {
const SourceValue& field(const SourceValue& v,const char* key){const auto* p=v.get(key);if(!p)throw std::runtime_error(std::string("Missing recovered Interact field: ")+key);return *p;}
float number(const SourceValue& v,const char* k){return float(field(v,k).number());}
bool boolean(const SourceValue& v,const char* k){return field(v,k).boolean();}
const std::string& text(const SourceValue& v,const char* k){return field(v,k).text();}
bool fail(std::string& error,const std::string& message){error=message;return false;}
bool finite(float n){return std::isfinite(n);}
bool validClock(InteractionClock c){return finite(c.time)&&c.frame>=0;}
bool validOwner(const InteractionOwnerFrame& f,InteractionKey key,std::string& error,bool requireBody=true){
    if(f.key!=key||!f.present)return fail(error,"Interact owner is not the requested retained actual Thing");
    if(requireBody&&!f.hasBody)return fail(error,"Interact requires actual Body component");
    if(!finite(f.x)||!finite(f.y)||!finite(f.z)||!finite(f.angle)||!finite(f.upwardVelocity))return fail(error,"Nonfinite actual Interact owner transform");
    return true;
}
int32_t truncateInt(float n){
    if(!finite(n)||double(n)<std::numeric_limits<int32_t>::min()||double(n)>std::numeric_limits<int32_t>::max())throw std::runtime_error("Interact grid coordinate outside native int32 conversion bounds");
    return int32_t(n);
}
int32_t cell(int32_t coordinate,int32_t origin,int32_t count){
    const int64_t relative=int64_t(coordinate)-origin;
    return int32_t(std::clamp(relative/256,int64_t(0),int64_t(count-1)));
}
int64_t sparseCell(int32_t coordinate){const int64_t n=coordinate;return n>=0?n/256:(n-255)/256;}
InteractionRect unite(InteractionRect a,InteractionRect b){
    if(!a.width||!a.height)return b;
    if(!b.width||!b.height)return a;
    const int64_t x=std::min(a.x,b.x),y=std::min(a.y,b.y);
    const int64_t right=std::max(int64_t(a.x)+a.width,int64_t(b.x)+b.width);
    const int64_t bottom=std::max(int64_t(a.y)+a.height,int64_t(b.y)+b.height);
    if(right-x>INT32_MAX||bottom-y>INT32_MAX)throw std::runtime_error("Interact bounds union outside int32");
    return {int32_t(x),int32_t(y),int32_t(right-x),int32_t(bottom-y)};
}
void location(const SourceValue& def,const InteractionOwnerFrame& owner,float& x,float& y){
    float ox=number(def,"OffsetX"),oy=number(def,"OffsetY");
    if(owner.bodyFlips&1)ox=-ox;
    if(owner.primaryFlipped)ox=-ox;
    if(owner.bodyFlips&2)oy=-oy;
    x=owner.x+ox;y=(boolean(def,"CursorOnly")?owner.z*0.8660254f:owner.y)+oy;
}
float distanceSquared(float ax,float ay,float bx,float by){const float dx=ax-bx,dy=ay-by;return dx*dx+dy*dy;}
float angle(float x,float y){const float a=std::atan2(-y,x);return a-std::floor(a/6.2831855f)*6.2831855f;}
}
SourceValue InteractionWorld::compiledDefaults(){
#include "interaction_defaults.inc"
}
bool InteractionWorld::resolveDefinition(const SourceValue& original,SourceValue& out,std::string& error){
    try{
        SourceValue merged=compiledDefaults();auto& dst=std::get<SourceValue::Object>(merged.value);
        for(const auto& [name,value]:original.object()){
            auto at=dst.find(name);if(at==dst.end())return fail(error,"Unrecovered original Interact property: "+name);
            const auto& base=at->second;
            if(std::holds_alternative<double>(base.value)){
                const double n=value.number();if(!std::isfinite(n))return fail(error,"Nonfinite Interact property: "+name);
                if(name=="NumUses"||name=="PlayerIndex"||name=="SpawnType"){
                    if(n!=std::trunc(n)||n<INT32_MIN||n>INT32_MAX||(name=="SpawnType"&&(n<0||n>255)))return fail(error,"Interact integer out of native bounds: "+name);
                    at->second.value=n;
                }else{const float f=float(n);if(!finite(f))return fail(error,"Interact float32 overflow: "+name);at->second.value=double(f);}
            }else if(std::holds_alternative<bool>(base.value))at->second.value=value.boolean();
            else if(std::holds_alternative<std::string>(base.value))at->second.value=value.text();
            else if(name=="UseSound"||name=="DisabledUseSound"){
                // Original source cue string is intentionally retained for the
                // real audio namespace normalizer. No validity is invented.
                if(!std::holds_alternative<std::string>(value.value)&&!std::holds_alternative<SourceValue::Object>(value.value))return fail(error,"Invalid original Interact SoundCue value");
                at->second=value;
            }else return fail(error,"Unrecovered Interact complex override: "+name);
        }
        out=std::move(merged);error.clear();return true;
    }catch(const std::exception& e){return fail(error,e.what());}
}
bool InteractionWorld::configureGrid(InteractionGrid grid,std::string& error){
    if(grid.columns<=0||grid.rows<=0||int64_t(grid.columns)*grid.rows>1000000)return fail(error,"Useable grid needs actual bounded world origin and positive dimensions");
    grid_=grid;gridReady_=true;sparseGrid_=false;cells_.clear();for(auto& [key,r]:records_){(void)key;r->state.dirty=true;}error.clear();return true;
}
void InteractionWorld::configureSparseGrid(){grid_={};gridReady_=true;sparseGrid_=true;cells_.clear();for(auto& [key,r]:records_){(void)key;r->state.dirty=true;}}
bool InteractionWorld::add(InteractionKey key,const SourceValue& original,InteractionClock clock,std::string& error){
    if(key.id<0||!key.lifetime||!validClock(clock))return fail(error,"Interact registration needs actual Thing identity and world clock");
    if(records_.contains(key))return fail(error,"Interact already registered for actual Thing lifetime");
    for(const auto& [existing,r]:records_){(void)r;if(existing.id==key.id)return fail(error,"Interact public ID reused before old lifetime removal");}
    SourceValue definition;if(!resolveDefinition(original,definition,error))return false;
    auto r=std::make_shared<Record>();auto& s=r->state;s.key=key;s.definition=std::move(definition);
    s.remainingUses=int32_t(field(s.definition,"NumUses").number());s.enabled=boolean(s.definition,"StartsUseable");
    s.global=boolean(s.definition,"UseableOffScreen");s.dirty=!s.global;
    if(boolean(s.definition,"StartsCoolingDown")){
        if(number(s.definition,"Cooldown")>0){s.lastUseTime=clock.time;s.lastUseFrame=clock.frame;}
        const float delay=number(s.definition,"RepeatDelay"),interval=number(s.definition,"RepeatInterval");
        if(delay>0||interval>0)s.repeatTimer=std::max(delay,interval);
    }
    records_.emplace(key,std::move(r));order_.push_back(key);error.clear();return true;
}
bool InteractionWorld::remove(InteractionKey key,std::string& error){
    if(!records_.erase(key))return fail(error,"Removing an unregistered Interact lifetime");
    std::erase(order_,key);for(auto it=cells_.begin();it!=cells_.end();){std::erase(it->second,key);if(it->second.empty())it=cells_.erase(it);else ++it;}
    error.clear();return true;
}
bool InteractionWorld::markForUpdate(InteractionKey key,std::string& error){auto it=records_.find(key);if(it==records_.end())return fail(error,"Unknown Interact lifetime");it->second->state.dirty=true;error.clear();return true;}
bool InteractionWorld::update(const InteractionProviders& p,std::string& error){
    if(!gridReady_)return fail(error,"UseableManager.Update requires actual world grid configuration");
    auto updated=cells_;std::vector<InteractionKey> dirty;
    try{
        for(auto key:order_){auto& s=records_.at(key)->state;if(!s.dirty)continue;dirty.push_back(key);
            for(auto it=updated.begin();it!=updated.end();){std::erase(it->second,key);if(it->second.empty())it=updated.erase(it);else ++it;}
            if(boolean(s.definition,"UseableOffScreen"))continue;
            if(!p.owner)return fail(error,"Missing actual Interact owner-frame provider");
            InteractionOwnerFrame f;if(!p.owner(key,f,error)||!validOwner(f,key,error))return false;
            InteractionRect bounds;
            if(boolean(s.definition,"UseExtents"))bounds=f.area;
            else{float x,y;location(s.definition,f,x,y);const int32_t width=truncateInt(number(s.definition,"Distance")*2);bounds={truncateInt(x-float(width/2)),truncateInt(y-float(width/2)),width,width};}
            if(boolean(s.definition,"UseTextBoxes"))for(auto b:f.textAreas){if(b.width<0||b.height<0)return fail(error,"Negative actual Interact text bounds");bounds=unite(bounds,b);}
            if(bounds.width<0||bounds.height<0)return fail(error,"Negative native Interact indexing bounds");
            const int64_t right=int64_t(bounds.x)+bounds.width,bottom=int64_t(bounds.y)+bounds.height;
            if(right>INT32_MAX||bottom>INT32_MAX)return fail(error,"Interact indexing bounds exceed int32");
            const int64_t x0=sparseGrid_?sparseCell(bounds.x):cell(bounds.x,grid_.originX,grid_.columns),x1=sparseGrid_?sparseCell(int32_t(right)):cell(int32_t(right),grid_.originX,grid_.columns);
            const int64_t y0=sparseGrid_?sparseCell(bounds.y):cell(bounds.y,grid_.originY,grid_.rows),y1=sparseGrid_?sparseCell(int32_t(bottom)):cell(int32_t(bottom),grid_.originY,grid_.rows);
            if((x1-x0+1)*(y1-y0+1)>1000000)return fail(error,"Actual Interact bounds exceed bounded sparse cell coverage");
            for(int64_t x=x0;x<=x1;++x)for(int64_t y=y0;y<=y1;++y)updated[{x,y}].push_back(key);
        }
    }catch(const std::exception& e){return fail(error,e.what());}
    cells_=std::move(updated);for(auto key:dirty){auto& s=records_.at(key)->state;s.dirty=false;s.global=boolean(s.definition,"UseableOffScreen");}error.clear();return true;
}
bool InteractionWorld::setEnabled(InteractionKey key,bool enabled,std::string& error){auto it=records_.find(key);if(it==records_.end())return fail(error,"Unknown Interact lifetime");it->second->state.enabled=enabled;error.clear();return true;}
bool InteractionWorld::replaceDefinition(InteractionKey key,const SourceValue& original,std::string& error){auto it=records_.find(key);if(it==records_.end())return fail(error,"Unknown Interact lifetime");SourceValue def;if(!resolveDefinition(original,def,error))return false;it->second->state.definition=std::move(def);it->second->state.dirty=true;error.clear();return true;}
bool InteractionWorld::setProperty(InteractionKey key,const std::string& property,const PropertyInput& input,PropertyChange change,InteractionClock clock,std::string& error){
    const auto existing=records_.find(key);
    SourceValue definition=existing==records_.end()?compiledDefaults():existing->second->state.definition;
    const auto* current=definition.get(property);
    if(!current)return fail(error,"Unrecovered reflected Interact property: "+property);
    // GetPropertyInfo<InteractDataDef>0x1800c7300 identifies the scalar types.
    // SoundCue and SpawnType require their native typed handlers, not strings
    // or an integer masquerading as the original enum mutation.
    if(property=="SpawnType"||property=="UseSound"||property=="DisabledUseSound")
        return fail(error,"Interact typed reflection not reconstructed: "+property);
    PropertyPrimitive before;
    if(const auto* value=std::get_if<bool>(&current->value))before=*value;
    else if(const auto* value=std::get_if<std::string>(&current->value))before=*value;
    else if(const auto* value=std::get_if<double>(&current->value)){
        if(property=="NumUses"||property=="PlayerIndex")before=int32_t(*value);
        else before=float(*value);
    }else return fail(error,"Interact complex reflection not reconstructed: "+property);
    PropertyPrimitive after;
    if(mutatePrimitiveProperty(before,input,change,after,error)!=PropertyMutation::Applied)return false;
    std::get<SourceValue::Object>(definition.value).at(property)=propertyPrimitiveToSource(after);
    if(existing==records_.end()){
        // SetInteractProperty2736ee..27373a creates the default component and
        // AddUseable before reflection. In particular, writing NumUses or
        // StartsUseable must not change the ctor-copied live counter/gate.
        if(!add(key,SourceValue{SourceValue::Object{}},clock,error))return false;
    }
    auto& state=records_.at(key)->state;state.definition=std::move(definition);
    state.dirty=true; // World dirty-owner insertion27375d..273770.
    error.clear();return true;
}
bool InteractionWorld::isUseable(InteractionKey key,const InteractionOwnerFrame& owner,InteractionClock clock,bool& result,std::string& error)const{
    result=false;const auto it=records_.find(key);if(it==records_.end())return fail(error,"Unknown Interact lifetime");if(!validClock(clock))return fail(error,"Invalid actual Interact world clock");
    const auto& s=it->second->state;if(!s.enabled||s.remainingUses<=0){error.clear();return true;}
    if(!validOwner(owner,key,error))return false;
    if(!boolean(s.definition,"UseableWhenInvisible")&&owner.alpha==0){error.clear();return true;}
    if(boolean(s.definition,"NoUpwardVelocity")&&owner.hasPhysics&&owner.upwardVelocity>0){error.clear();return true;}
    const float cooldown=number(s.definition,"Cooldown");result=!(cooldown>0&&clock.time<=s.lastUseTime+cooldown);error.clear();return true;
}
bool InteractionWorld::userCanUse(const InteractionUserFrame& u){return u.totalZ<=u.maxUseZ&&(!u.hasPrimary||!u.disableOwnerInteract);}
bool InteractionWorld::isUseableBy(InteractionKey key,const InteractionOwnerFrame& owner,const InteractionUserFrame& u,InteractionClock clock,bool useMouse,const InteractionProviders& p,bool& result,std::string& error)const{
    result=false;const auto it=records_.find(key);if(it==records_.end())return fail(error,"Unknown Interact lifetime");const auto& def=it->second->state.definition;
    if(!userCanUse(u)){error.clear();return true;}
    const auto& attached=text(def,"RequiresAttached");if(!attached.empty()){if(!p.hasAttached)return fail(error,"RequiresAttached needs actual Unit.HasAttached provider");bool has=false;if(!p.hasAttached(u,attached,has,error))return false;if(!has){error.clear();return true;}}
    if(boolean(def,"AttachOnUse")&&(!u.alive||owner.attachedToLiveEntity)){error.clear();return true;}
    const int32_t player=int32_t(field(def,"PlayerIndex").number());if(player>0&&u.playerControlled&&int64_t(u.playerIndex)+1!=player){error.clear();return true;}
    const float airborne=number(def,"AirborneMaxZ"),maxZ=airborne>0&&u.airborneAny?airborne:number(def,"MaxZ");if(owner.z>maxZ){error.clear();return true;}
    const float dz=number(def,"DistanceZ");if(dz!=0&&std::abs(owner.z-u.totalZ)>dz){error.clear();return true;}
    const auto& missing=text(def,"RequiresMissingAmmoWeapon");if(!missing.empty()){if(!p.weaponAmmo)return fail(error,"RequiresMissingAmmoWeapon needs actual owned Arsenal/HasAmmo provider");bool exists=false,ammo=false;if(!p.weaponAmmo(u,missing,exists,ammo,error))return false;if(!exists||ammo){error.clear();return true;}}
    if(!boolean(def,"AutoActivate")&&(useMouse||!boolean(def,"AutoActivateWithGamepad"))&&u.hasPrimary&&u.disableOwnerManualInteract){error.clear();return true;}
    return isUseable(key,owner,clock,result,error);
}
bool InteractionWorld::selectNormal(const InteractionUserFrame& user,InteractionClock clock,bool useMouse,const InteractionProviders& p,InteractionKey& selected,std::string& error)const{
    selected={};if(!gridReady_)return fail(error,"Use selection requires initialized actual Useable grid");if(!p.owner)return fail(error,"Use selection requires actual Thing frame provider");
    if(!finite(user.x)||!finite(user.y)||!finite(user.totalZ)||!finite(user.maxUseZ)||!finite(user.useReach))return fail(error,"Nonfinite actual Interact user frame");
    if(!user.alive||user.disposed||!userCanUse(user)){error.clear();return true;}
    std::set<InteractionKey> candidates;
    try{const int32_t x=truncateInt(user.x),y=truncateInt(user.y);const Cell c{sparseGrid_?sparseCell(x):cell(x,grid_.originX,grid_.columns),sparseGrid_?sparseCell(y):cell(y,grid_.originY,grid_.rows)};const auto at=cells_.find(c);if(at!=cells_.end())candidates.insert(at->second.begin(),at->second.end());}catch(const std::exception& e){return fail(error,e.what());}
    for(auto key:order_)if(records_.at(key)->state.global)candidates.insert(key);
    bool selectedAuto=false;float selectedDistance=0;
    for(auto key:order_){if(!candidates.contains(key))continue;const auto& s=records_.at(key)->state;
        InteractionOwnerFrame f;if(!p.owner(key,f,error)||!validOwner(f,key,error))return false;if(!f.firstGroupVisible)continue;
        bool eligible=false;if(!isUseableBy(key,f,user,clock,useMouse,p,eligible,error))return false;if(!eligible||boolean(s.definition,"CursorOnly"))continue;
        if(boolean(s.definition,"RequiresTouch")){if(!p.touches)return fail(error,"RequiresTouch needs real native projected polygon collision provider");bool touches=false;if(!p.touches(f,user,touches,error))return false;if(touches){selected=key;error.clear();return true;}continue;}
        const bool automatic=boolean(s.definition,"AutoActivate");float range=number(s.definition,"Distance");if(range<=0)range=std::max(float(f.area.width)/1.2f,125.0f);
        if(!automatic&&(useMouse||!boolean(s.definition,"AutoActivateWithGamepad")))range+=user.useReach;
        float x,y;location(s.definition,f,x,y);if(!(distanceSquared(user.x,user.y,x,y)<range*range))continue;
        const float d=distanceSquared(user.x,user.y,f.x,f.y);
        if(selected.id<0||(selectedAuto&&!automatic)||(selectedAuto==automatic&&d<=selectedDistance)){selected=key;selectedAuto=automatic;selectedDistance=d;}
    }
    error.clear();return true;
}
bool InteractionWorld::useLocation(InteractionKey key,const InteractionOwnerFrame& owner,float& x,float& y,std::string& error)const{const auto at=records_.find(key);if(at==records_.end())return fail(error,"Unknown Interact lifetime");if(!validOwner(owner,key,error))return false;location(at->second->state.definition,owner,x,y);error.clear();return true;}
bool InteractionWorld::manualRequest(InteractionKey selected,const InteractionUserFrame& u,bool useMouse,uint32_t flags,bool& accepted,std::string& error)const{
    accepted=false;if(!userCanUse(u)||selected.id<0){error.clear();return true;}
    const auto at=records_.find(selected);if(at==records_.end()){error.clear();return true;}
    const auto& d=at->second->state.definition;
    accepted=!boolean(d,"AutoActivate")&&(useMouse||!boolean(d,"AutoActivateWithGamepad"))&&(flags&4)!=0;
    error.clear();return true;
}
bool InteractionWorld::snapshot(InteractionKey key,InteractionSnapshot& out,std::string& error)const{const auto at=records_.find(key);if(at==records_.end())return fail(error,"Unknown Interact lifetime");out=at->second->state;error.clear();return true;}
std::vector<InteractionSnapshot> InteractionWorld::snapshots()const{std::vector<InteractionSnapshot> out;for(auto key:order_)out.push_back(records_.at(key)->state);return out;}
std::string InteractionWorld::dump()const{std::ostringstream out;out<<"USEABLE_STATE records="<<records_.size()<<" grid="<<(sparseGrid_?"android-sparse256":std::to_string(grid_.columns)+'x'+std::to_string(grid_.rows))<<" cells="<<cells_.size();for(auto key:order_){const auto& s=records_.at(key)->state;out<<"\nUSEABLE id="<<key.id<<" lifetime="<<key.lifetime<<" enabled="<<s.enabled<<" uses="<<s.remainingUses<<" used="<<s.hasBeenUsed<<" toggle="<<s.toggleState<<" lastTime="<<s.lastUseTime<<" lastFrame="<<s.lastUseFrame<<" dirty="<<s.dirty<<" global="<<s.global;}return out.str();}

bool InteractionUseExecution::begin(InteractionWorld& world,InteractionKey owner,InteractionKey user,InteractionClock clock,std::string& error){
    if(record_)return fail(error,"Interaction Use execution already begun");const auto at=world.records_.find(owner);if(at==world.records_.end())return fail(error,"Use needs actual retained Interact owner");if(!validClock(clock))return fail(error,"Use needs actual world clock");
    record_=at->second;user_=user;auto& s=record_->state;if(s.remainingUses<=0){done_=true;error.clear();return true;}
    --s.remainingUses;s.lastUseTime=clock.time;s.lastUseFrame=clock.frame;error.clear();return true;
}
bool InteractionUseExecution::next(const InteractionProviders& p,const std::function<bool(InteractionKey,InteractionUserFrame&,std::string&)>& userProvider,InteractionUseOperation& out,bool& done,std::string& error){
    done=done_;if(!record_)return fail(error,"Interaction Use execution not begun");if(done_){error.clear();return true;}if(waiting_){out=pending_;error.clear();return true;}
    auto& s=record_->state;
    for(;;){
        if(stage_>=17){s.hasBeenUsed=true;done_=true;done=true;error.clear();return true;}
        const auto& def=s.definition;InteractionUseOperation op;op.owner=s.key;op.user=user_;
        InteractionOwnerFrame owner;InteractionUserFrame user;
        const bool hasUser=user_.id>=0;
        if(stage_<8&&!hasUser&&stage_!=7){++stage_;continue;}
        if(stage_<=6){if(!userProvider)return fail(error,"Use needs fresh actual Unit frame provider");if(!userProvider(user_,user,error))return false;if(user.key!=user_)return fail(error,"Use user lifetime changed during callback");}
        auto ownerFrame=[&](){if(!p.owner)return fail(error,"Use needs fresh retained Thing frame provider");return p.owner(s.key,owner,error)&&validOwner(owner,s.key,error,false);};
        switch(stage_){
        case 0:op.kind=InteractionUseKind::UserAnimation;op.value=field(def,"UserAnimation");break;
        case 1:if(!user.hasPhysics){++stage_;continue;}if(!ownerFrame())return false;op.kind=InteractionUseKind::UserPlanarForce;op.angle=owner.hasBody?owner.angle:0;op.value=field(def,"UserVelocity");break;
        case 2:if(!user.hasPhysics){++stage_;continue;}op.kind=InteractionUseKind::UserUpwardForce;op.value=field(def,"UserUpwardVelocity");break;
        case 3:if(!boolean(def,"UserFaceTarget")){++stage_;continue;}if(!ownerFrame())return false;op.kind=InteractionUseKind::UserFaceTarget;op.angle=angle(owner.x-user.x,owner.y-user.y);break;
        case 4:op.kind=InteractionUseKind::UserFx;op.value=field(def,"UserFx");op.x=user.x;op.y=user.y;break;
        case 5:if(!ownerFrame())return false;if(owner.typeName.empty())return fail(error,"OnHeal needs actual owner.GetTypeName");op.kind=InteractionUseKind::OnHeal;op.value=field(def,"HealAmount");op.sourceTypeName=owner.typeName;break;
        case 6:if(!boolean(def,"AttachOnUse")){++stage_;continue;}op.kind=InteractionUseKind::AttachOwnerToUser;break;
        case 7:op.kind=InteractionUseKind::OnUsed;break;
        case 8:op.kind=InteractionUseKind::OwnerAnimation;op.value=field(def,s.toggleState?"ToggleOffAnimation":"OnUseAnimation");break;
        case 9:if(!ownerFrame())return false;op.kind=InteractionUseKind::VisualFx;op.value=field(def,"VisualFx");op.x=owner.x;op.y=owner.y;break;
        case 10:op.kind=InteractionUseKind::UseSound;op.value=field(def,"UseSound");break;
        case 11:if(text(def,"UsedFrontFx").empty()){++stage_;continue;}op.kind=InteractionUseKind::UsedFrontFx;op.value=field(def,"UsedFrontFx");break;
        case 12:if(text(def,"UsedBackFx").empty()){++stage_;continue;}op.kind=InteractionUseKind::UsedBackFx;op.value=field(def,"UsedBackFx");break;
        case 13:if(s.hasBeenUsed||!boolean(def,"DestroyOnUse")){++stage_;continue;}op.kind=InteractionUseKind::DestroyOwner;break;
        case 14:if(text(def,"UserFireWeapon").empty()){++stage_;continue;}if(!hasUser)return fail(error,"Native UserFireWeapon path requires actual Unit user");if(!userProvider||!userProvider(user_,user,error)||!ownerFrame())return false;op.kind=InteractionUseKind::UserFireWeapon;op.value=field(def,"UserFireWeapon");op.angle=angle(user.x-owner.x,user.y-owner.y);op.x=owner.x;op.y=owner.y;break;
        case 15:if(!boolean(def,"DisableOnUse")){++stage_;continue;}s.enabled=false;op.kind=InteractionUseKind::UnuseableAnimation;op.value=field(def,"UnuseableAnimation");break;
        case 16:s.hasBeenUsed=true;++stage_;continue;
        default:return fail(error,"Invalid native Use execution stage");
        }
        pending_=std::move(op);out=pending_;waiting_=true;error.clear();return true;
    }
}
bool InteractionUseExecution::acknowledge(std::string& error){
    if(!waiting_||!record_)return fail(error,"No pending native Use operation to acknowledge");auto& s=record_->state;
    if(stage_==8&&boolean(s.definition,"IsToggle"))s.toggleState=!s.toggleState;
    if(stage_==13)s.enabled=false;
    if(stage_==15)s.previousUseable=false;
    ++stage_;waiting_=false;error.clear();return true;
}
}
