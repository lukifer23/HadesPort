#include "weapon_runtime.hpp"
#include "primitive_properties.hpp"
#include "weapon_catalog.hpp"
#include <functional>
#include <set>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <cctype>
#include <type_traits>

namespace hades {
namespace {
// All native anchors refer to DLL SHA256
// d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254,
// matching PDB15f83bcc-e740-4d0b-bf67-6ac938c8bdfa age1.
// See native/analysis/weapon-defaults-contract.json and private source export.
bool reject(std::string& error, const std::string& detail) {
    error = "Unsupported staged weapon initialization: " + detail;
    return false;
}
bool firstBranch(const WeaponDefinition& d, std::string& error) {
    if (d.name != "ArmorBreakAttack") return reject(error,"only original ArmorBreakAttack is recovered");
    if (d.type != 0) return reject(error,"weapon type must be original GUN0");
    // The literal string is retained. Original projectile source namespace has
    // no definition named "null"; Weapon::init VA0x180088c21 skips copies.
    if (d.projectile != "null") return reject(error,"projectile-data creation/lookup branch");
    for (const auto& name : d.controls)
        if (!name.empty()) return reject(error,"nonempty control binding");
    for (const auto& name : d.bundled)
        if (!name.empty()) return reject(error,"bundled weapon dependency");
    if (d.startingAmmo != -1 || d.maxAmmo != -1)
        return reject(error,"only original StartingAmmo=-1/MaxAmmo=-1 branch is supported");
    if (d.clipSize != 0) return reject(error,"nonzero ClipSize/update branch");
    if (!std::isfinite(d.chargeTime) || d.chargeTime != 0)
        return reject(error,"nonzero/nonfinite ChargeTime");
    if (!std::isfinite(d.clipRegenInterval) || d.clipRegenInterval != 0)
        return reject(error,"nonzero/nonfinite ClipRegenInterval");
    if (!std::isfinite(d.initialCooldown) || d.initialCooldown != 0)
        return reject(error,"nonzero/nonfinite InitialCooldown");
    if (d.reloadOnCreation || d.reloadOnEquip)
        return reject(error,"creation/equip reload and cooldown side effects");
    // Singular Effect -> one entry is independently recovered. UNKNOWN12 is
    // not WEAPON_FIRE8, so Arsenal::AddWeapon adds no effect dependency.
    if (!d.effectContainerResolved || d.effectName != "ArmorBreakStun" ||
        d.effectType != 12 || d.effectTrigger != 1)
        return reject(error,"first-effect normalization/type/trigger not established");
    return true;
}
}
WeaponArsenal::WeaponArsenal(int32_t ownerId, std::string ownerName)
    : ownerId_(ownerId), ownerName_(std::move(ownerName)) {
    if (ownerId_ <= 0 || ownerName_.empty() || ownerName_.size() > 1024 ||
        ownerName_.find('\0') != std::string::npos)
        throw std::invalid_argument("Staged weapon owner requires positive ID and original unit data name");
}
WeaponArsenal::WeaponArsenal(int32_t ownerId,std::string ownerName,std::shared_ptr<const WeaponCatalog> catalog)
    : WeaponArsenal(ownerId,std::move(ownerName)){catalog_=std::move(catalog);}
bool WeaponArsenal::equip(const std::string& name,std::string& error){
    if(retirementRequested_)return reject(error,"cannot equip a retired Unit arsenal");
    error.clear();if(hasWeapon(name))return true;
    if(!catalog_)return reject(error,"named equip needs source catalog");
    const auto* d=catalog_->weapon(name);if(!d)return reject(error,"unknown weapon definition "+name);
    return equip(*d,error);
}
bool WeaponArsenal::hasWeapon(const std::string& name) const {
    for (const auto& weapon : weapons_)
        if (weapon.definition.name == name) return true;
    return false;
}
bool WeaponArsenal::equip(const WeaponDefinition& definition, std::string& error) {
    if(retirementRequested_)return reject(error,"cannot equip a retired Unit arsenal");
    error.clear();
    // Native action VA0x18025c5e0 calls HasWeapon before Create/reload; an
    // existing name does not get reinitialized or rejected for new properties.
    if (hasWeapon(definition.name)) return true;
    if(catalog_){
        // Native Arsenal::Add appends before recursion, preserving cycles and
        // parent/bundle/effect dependency ordering. Stage the entire operation
        // so an unsupported/missing dependency never partially mutates owner.
        auto staged=weapons_;
        auto stagedControls=controllableOrder_;
        std::function<bool(const WeaponDefinition&,bool,size_t)> add;
        add=[&](const WeaponDefinition& d,bool bundled,size_t depth)->bool{
            for(const auto& w:staged)if(w.definition.name==d.name)return true;
            if(depth>256||staged.size()>=4096)return reject(error,"weapon dependency bounds");
            if(d.name.empty()||(d.type!=0&&d.type!=1))return reject(error,"unsupported weapon name/type "+d.name);
            const float scalars[]{d.chargeTime,d.clipRegenInterval,d.initialCooldown,d.controlWindow,d.cooldown,d.cooldownMin,d.cooldownMax,d.reloadTime,d.reloadTimeMin,d.reloadTimeMax};
            for(float v:scalars)if(!std::isfinite(v))return reject(error,"nonfinite timer "+d.name);
            if(d.chargeTime<0||d.clipRegenInterval<0||d.clipSize<0)return reject(error,"negative initialization field "+d.name);
            if(!d.effectContainerResolved)return reject(error,"unresolved effect container "+d.name);
            const bool reload=d.reloadOnCreation||(!bundled&&d.reloadOnEquip);
            if(reload&&(d.reloadTimeMin>0||d.cooldownMin!=0||d.cooldownMax!=0))return reject(error,"reload/cooldown RNG requires original VM stream: "+d.name);
            if(reload&&d.reloadTime<=0&&(!d.reloadFinishGraphic.empty()||!d.swapOnReloadFinish.empty()))return reject(error,"reload finish owner animation/control effects: "+d.name);
            WeaponSnapshot w;w.ownerId=ownerId_;w.ownerName=ownerName_;w.definition=d;w.bundled=bundled;
            w.ammo=d.startingAmmo>=0?d.startingAmmo:d.maxAmmo;
            w.clipShotsRemaining=d.clipSize;w.chargeTimeRemaining=d.chargeTime;w.nextClipRegenTime=d.clipRegenInterval;
            for(size_t i=0;i<d.controls.size();++i){
                if(d.controls[i].empty()||d.controls[i]=="null")continue; // absent in original default control namespace
                w.controlIndices[i]=catalog_->controlIndex(d.controls[i]);
                if(w.controlIndices[i]<0)return reject(error,"unresolved original control "+d.controls[i]);
            }
            if(!catalog_->missingProjectile(d.projectile)){
                const auto* p=catalog_->projectile(d.projectile);if(!p)return reject(error,"unresolved projectile "+d.projectile);
                w.projectileData=*p;w.projectileSource=std::shared_ptr<const ProjectileDefinition>(catalog_,p);w.validProjectileData=true;
                if(p->spawnType==3){for(const auto& n:p->spawned){if(n.empty())continue;const auto* spawned=catalog_->projectile(n);if(!spawned)return reject(error,"unresolved spawned projectile "+n);w.spawnedProjectileData=*spawned;w.spawnedProjectileSource=std::shared_ptr<const ProjectileDefinition>(catalog_,spawned);w.validSpawnedProjectileData=true;}}
            }
            // Fresh cancelChargeOnUnit has no owner side effects. Defaults +
            // init reset timers before creation reload/cooldown/initial force.
            if(reload){w.reloadTimeRemaining=std::max(0.0f,d.reloadTime);w.cooldownTimeRemaining=std::max(0.0f,d.cooldown);}
            if(d.initialCooldown>0&&!d.ignoreForceCooldown)w.cooldownTimeRemaining=std::max(w.cooldownTimeRemaining,d.initialCooldown);
            const size_t own=staged.size();staged.push_back(std::move(w));
            auto dependency=[&](const std::string& name)->bool{
                if(catalog_->missingWeapon(name))return true; // verified absence, not a fabricated weapon
                for(const auto& existing:staged)if(existing.definition.name==name)return true;
                const auto* target=catalog_->weapon(name);if(!target)return reject(error,"unresolved bundled/effect weapon "+name);
                return add(*target,true,depth+1);
            };
            for(const auto& name:d.bundled)if(!dependency(name))return false;
            // Native controllable registration occurs after bundle recursion.
            staged[own].controllable=staged[own].controlIndices[0]>=0&&d.defaultControl&&d.controlWindow<=0&&!bundled;
            if(staged[own].controllable)stagedControls.push_back(d.name);
            // Native adds WEAPON_FIRE dependencies even when source Active=false.
            for(const auto& e:d.effects)if(e.type==8&&!dependency(e.name))return false;
            return true;
        };
        if(!add(definition,false,0))return false;
        weapons_=std::move(staged);controllableOrder_=std::move(stagedControls);return true;
    }
    if (!firstBranch(definition,error)) return false;
    WeaponSnapshot value;
    value.ownerId = ownerId_;
    value.ownerName = ownerName_;
    value.definition = definition; // independent per-instance owned copy
    // Native init VA0x180088dc9..dde: StartingAmmo>=0 ? StartingAmmo : MaxAmmo.
    value.ammo = definition.startingAmmo >= 0 ? definition.startingAmmo : definition.maxAmmo;
    value.clipShotsRemaining = definition.clipSize; // VA0x180088da0/da6/e3f
    value.chargeTimeRemaining = definition.chargeTime; // VA0x180088d8f/d9c
    value.nextClipRegenTime = definition.clipRegenInterval; // VA0x180088dad/dc2
    // Fresh native owner ctor VA0x18008877c writes DWORD0x17 at this+0x11f,
    // setting validity bytes120/121 false. Missing projectile branch leaves
    // those false. Native allocates default projectile data storage; this DTO
    // models validity, not original native pointer addresses/pools.
    value.validProjectileData = false;
    value.validSpawnedProjectileData = false;
    // Native init VA0x180088d8b..dbe sets all these zero/false; no requested
    // reload/initial-cooldown mutation applies to the verified branch.
    value.cooldownTimeRemaining = 0;
    value.reloadTimeRemaining = 0;
    value.lastFireTime = 0;
    value.scatter = 0;
    value.charging = false;
    // Weapon::init invokes cancelChargeOnUnit at VA0x180088bfc. With fresh
    // charging=false it sets ChargeRemaining=ChargeTime, then returns at its
    // VA0x1800859cf/d3 guard, without unit/control/animation side effects.
    // All four controls are empty; AddWeapon's controllable predicate cannot
    // pass even if source DefaultControl=true. Empty seven-name bundle graph
    // and UNKNOWN12 effect have no recursively created dependencies.
    value.controllable = false;
    weapons_.push_back(std::move(value));
    return true;
}
bool WeaponArsenal::setProperty(const std::string& name,const std::string& property,const WeaponScalar& requested,std::string& error){
    error.clear();WeaponSnapshot* w=nullptr;for(auto& candidate:weapons_)if(candidate.definition.name==name){w=&candidate;break;}
    if(!w){error="No staged weapon named "+name;return false;}
    auto found=w->definition.fields.find(property);auto type=w->definition.fieldTypes.find(property);
    if(found==w->definition.fields.end()||type==w->definition.fieldTypes.end())return reject(error,"unknown/complex weapon property "+property);
    if(property=="Type")return reject(error,"changing native subtype data without changing C++ virtual subclass is not supported");
    WeaponScalar value=requested;
    const auto& kind=type->second;
    if(kind=="bool"){if(!std::holds_alternative<bool>(value))return reject(error,"weapon property requires boolean "+property);}
    else if(kind=="string"){if(!std::holds_alternative<std::string>(value))return reject(error,"weapon property requires string "+property);const auto& v=std::get<std::string>(value);if(v.size()>65536||v.find('\0')!=std::string::npos)return reject(error,"invalid weapon string property");}
    else if(kind=="float32"||kind=="int32"||kind=="uint32"||kind=="uint8"||kind=="char"){
        if(!std::holds_alternative<double>(value))return reject(error,"weapon property requires number "+property);
        double n=std::get<double>(value);if(!std::isfinite(n))return reject(error,"nonfinite weapon property");
        if(kind=="float32"){const float f=static_cast<float>(n);if(!std::isfinite(f))return reject(error,"weapon float overflow");value=static_cast<double>(f);}
        else{double low=kind=="int32"?-2147483648.0:kind=="char"?-128.0:0.0;double high=kind=="int32"?2147483647.0:kind=="uint32"?4294967295.0:kind=="char"?127.0:255.0;if(std::floor(n)!=n||n<low||n>high)return reject(error,"integer property out of range "+property);}
    }else return reject(error,"non-scalar reflected weapon property "+property);
    if(property=="Projectile")return reject(error,"Projectile setter requires native copy/retained-validity branch");
    // Independent per-instance data copy. Native SetDataProperty performs no
    // global catalog mutation, charge reset or frame postprocessing here.
    auto d=w->definition;d.fields[property]=value;auto controls=w->controlIndices;
    const std::array<std::string,4> controlNames{{"Control","AlternateControl","SecondaryControl","DisallowedControl"}};
    for(size_t i=0;i<controlNames.size();++i)if(property==controlNames[i]){
        const auto& v=std::get<std::string>(value);d.controls[i]=v;
        if(v.empty()||v=="null")controls[i]=-1;
        else{if(!catalog_||(controls[i]=catalog_->controlIndex(v))<0)return reject(error,"unresolved native control property "+v);}
    }
    const std::array<std::string,7> bundles{{"SwapOnFire","AddOnFire","SwapOnRelease","CollisionWeapon","PostFireWeapon","MovingWeapon","CooldownWeapon"}};
    for(size_t i=0;i<bundles.size();++i)if(property==bundles[i])d.bundled[i]=std::get<std::string>(value);
    if(property=="StartingAmmo")d.startingAmmo=static_cast<int32_t>(std::get<double>(value));
    else if(property=="MaxAmmo")d.maxAmmo=static_cast<int32_t>(std::get<double>(value));
    else if(property=="ClipSize")d.clipSize=static_cast<int32_t>(std::get<double>(value));
    else if(property=="ChargeTime")d.chargeTime=static_cast<float>(std::get<double>(value));
    else if(property=="ClipRegenInterval")d.clipRegenInterval=static_cast<float>(std::get<double>(value));
    else if(property=="InitialCooldown")d.initialCooldown=static_cast<float>(std::get<double>(value));
    else if(property=="ControlWindow")d.controlWindow=static_cast<float>(std::get<double>(value));
    else if(property=="Cooldown")d.cooldown=static_cast<float>(std::get<double>(value));
    else if(property=="CooldownMin")d.cooldownMin=static_cast<float>(std::get<double>(value));
    else if(property=="CooldownMax")d.cooldownMax=static_cast<float>(std::get<double>(value));
    else if(property=="ReloadTime")d.reloadTime=static_cast<float>(std::get<double>(value));
    else if(property=="ReloadTimeMin")d.reloadTimeMin=static_cast<float>(std::get<double>(value));
    else if(property=="ReloadTimeMax")d.reloadTimeMax=static_cast<float>(std::get<double>(value));
    else if(property=="ReloadOnCreation")d.reloadOnCreation=std::get<bool>(value);
    else if(property=="ReloadOnEquip")d.reloadOnEquip=std::get<bool>(value);
    else if(property=="DefaultControl")d.defaultControl=std::get<bool>(value);
    else if(property=="IgnoreForceCooldown")d.ignoreForceCooldown=std::get<bool>(value);
    else if(property=="ReloadGraphicFinish")d.reloadFinishGraphic=std::get<std::string>(value);
    else if(property=="SwapOnReloadFinish")d.swapOnReloadFinish=std::get<std::string>(value);
    else if(property=="ShareAmmoWeapon")d.shareAmmoWeapon=std::get<std::string>(value);
    w->definition=std::move(d);w->controlIndices=controls;
    if(property=="MaxAmmo")w->ammo=w->definition.maxAmmo; // native VA0x1800843da..e0
    else if(property=="ClipSize")w->clipShotsRemaining=w->definition.clipSize; // VA0x1800843ff..405
    // Controls refresh pointers; native controllable-list membership remains.
    return true;
}
const WeaponSnapshot* WeaponArsenal::weaponState(const std::string& name,std::string& error) const {
    error.clear();for(const auto& w:weapons_)if(w.definition.name==name)return &w;
    error="No owned weapon named "+name;return nullptr;
}
WeaponSnapshot* WeaponArsenal::scriptMutableWeapon(const std::string& name){for(auto& w:weapons_)if(w.definition.name==name)return &w;return nullptr;}
WeaponSnapshot::Blink* WeaponArsenal::blinkState(const std::string& name,std::string& error){
    error.clear();for(auto& w:weapons_)if(w.definition.name==name){if(w.definition.type!=1){error="Blink state requested for non-BLINK weapon "+name;return nullptr;}return &w.blink;}
    error="No owned weapon named "+name;return nullptr;
}
const ProjectileDefinition* WeaponArsenal::projectileData(const std::string& name,bool spawned,std::string& error) const {
    error.clear();for(const auto& w:weapons_)if(w.definition.name==name){
        if(spawned?!w.validSpawnedProjectileData:!w.validProjectileData){error="Native copied projectile data is not valid for "+name;return nullptr;}
        return spawned?&w.spawnedProjectileData:&w.projectileData;
    }
    error="No owned weapon named "+name;return nullptr;
}
bool WeaponArsenal::editProjectileData(const std::string& name,bool spawned,
        const std::function<bool(ProjectileDefinition&,std::string&)>& mutate,std::string& error){
    error.clear();if(!mutate){error="No projectile data mutator";return false;}
    for(auto& w:weapons_)if(w.definition.name==name){
        if(spawned?!w.validSpawnedProjectileData:!w.validProjectileData){error="Native copied projectile data is not valid for "+name;return false;}
        auto& current=spawned?w.spawnedProjectileData:w.projectileData;auto staged=current;
        if(!mutate(staged,error)){if(error.empty())error="Projectile data mutation rejected";return false;}
        if(staged.name!=current.name){error="Projectile data identity changes require native source Copy semantics";return false;}
        // Build the new snapshot before committing either representation.
        auto frozen=std::make_shared<const ProjectileDefinition>(staged);
        current=std::move(staged);
        (spawned?w.spawnedProjectileSource:w.projectileSource)=std::move(frozen);
        return true;
    }
    error="No owned weapon named "+name;return false;
}
bool WeaponArsenal::setScriptProjectileProperty(const std::string& name,const std::string& property,
        std::optional<WeaponScalar> requested,WeaponValueChange change,std::string& error){
    // Weapon::SetProjectileProperty RVA844a0 -> setProjectileData RVA84770.
    // ScriptAction RVA272030 normalizes exact lowercase literal "null" to any().
    error.clear();WeaponSnapshot* w=nullptr;for(auto& value:weapons_)if(value.definition.name==name){w=&value;break;}
    if(!w){error="No owned weapon named "+name;return false;}
    if(!w->validProjectileData){error="Weapon has no native-valid projectile data";return false;}
    if(static_cast<unsigned>(change)>4)return reject(error,"unknown projectile value change mode");
    if(property=="Type"||property=="DetonateChildGraphics"||property=="SpawnOnDetonate"||property=="SpawnOnImpact"||property=="SpawnOnDeath")
        return reject(error,"projectile property requires dedicated native enum/vector/dependency-copy handler: "+property);
    if(requested&&std::holds_alternative<std::string>(*requested)&&std::get<std::string>(*requested)=="null")requested.reset();
    PropertyInput input=std::monostate{};
    if(requested)std::visit([&](const auto& v){using T=std::decay_t<decltype(v)>;if constexpr(std::is_same_v<T,double>)input=static_cast<float>(v);else input=v;},*requested);
    auto mutate=[&](ProjectileDefinition& d){
        auto f=d.fields.find(property);auto t=d.fieldTypes.find(property);
        if(f==d.fields.end()||t==d.fieldTypes.end())return reject(error,"unknown/complex projectile property or unrecovered Thing fallback: "+property);
        PropertyPrimitive current;
        if(t->second=="float32"&&std::holds_alternative<double>(f->second))current=static_cast<float>(std::get<double>(f->second));
        else if(t->second=="int32"&&std::holds_alternative<double>(f->second))current=static_cast<int32_t>(std::get<double>(f->second));
        else if(t->second=="bool"&&std::holds_alternative<bool>(f->second))current=std::get<bool>(f->second);
        else if(t->second=="string"&&std::holds_alternative<std::string>(f->second))current=std::get<std::string>(f->second);
        else return reject(error,"unrecovered projectile reflected type "+t->second+" for "+property);
        std::optional<PropertyInput> delta;
        if(change==WeaponValueChange::MultiplyBase){
            // Native helper recognizes ProjectileData category3 and reads that
            // source record through ProjectileData reflection, unlike generic
            // WeaponData's Name/Organization/InheritFrom-only base lookup.
            if(!catalog_||!std::holds_alternative<float>(input))return reject(error,"projectile MultiplyBase requires actual source catalog and numeric multiplier");
            const auto* base=catalog_->projectile(d.name);
            if(!base)return reject(error,"missing source projectile for MultiplyBase");
            auto v=base->fields.find(property);auto bt=base->fieldTypes.find(property);
            if(v==base->fields.end()||bt==base->fieldTypes.end()||!std::holds_alternative<double>(v->second)||
               (bt->second!="float32"&&bt->second!="int32"))return reject(error,"MultiplyBase source reflection is not a proven numeric projectile field");
            const float add=static_cast<float>(std::get<double>(v->second))*(std::get<float>(input)-1.0f);
            if(!std::isfinite(add))return reject(error,"projectile MultiplyBase outside portable finite subset");
            if(bt->second=="float32")delta=PropertyInput{add};
            else {if(add>=2147483648.0f||add< -2147483648.0f)return reject(error,"projectile MultiplyBase integer outside portable subset");delta=PropertyInput{static_cast<int32_t>(add)};}
        }
        PropertyPrimitive result;
        if(mutatePrimitiveProperty(current,input,static_cast<PropertyChange>(change),result,error,delta)!=PropertyMutation::Applied)return false;
        std::visit([&](const auto& v){using T=std::decay_t<decltype(v)>;if constexpr(std::is_same_v<T,float>||std::is_same_v<T,int32_t>)f->second=static_cast<double>(v);else f->second=v;},result);
        return true;
    };
    auto primary=w->projectileData;auto spawned=w->spawnedProjectileData;
    if(!mutate(primary))return false;
    const bool applySpawned=w->validSpawnedProjectileData&&std::any_of(primary.spawned.begin(),primary.spawned.end(),[](const auto& n){return !n.empty();});
    if(applySpawned&&!mutate(spawned))return false;
    auto frozenPrimary=std::make_shared<const ProjectileDefinition>(primary);
    auto frozenSpawned=applySpawned?std::make_shared<const ProjectileDefinition>(spawned):w->spawnedProjectileSource;
    w->projectileData=std::move(primary);w->projectileSource=std::move(frozenPrimary);
    if(applySpawned){w->spawnedProjectileData=std::move(spawned);w->spawnedProjectileSource=std::move(frozenSpawned);}
    return true;
}
bool WeaponArsenal::dataProperty(const std::string& name,const std::string& property,WeaponScalar& output,std::string& error)const{
    error.clear();for(const auto& w:weapons_)if(w.definition.name==name){auto f=w.definition.fields.find(property);if(f==w.definition.fields.end())return reject(error,"unknown/complex weapon data property "+property);output=f->second;return true;}error="No staged weapon named "+name;return false;
}
bool WeaponArsenal::setScriptProperty(const std::string& name,const std::string& property,
        std::optional<WeaponScalar> requested,WeaponValueChange change,bool dataValue,std::string& error){
    // DLL-bound ScriptAction::SetWeaponProperty RVA271b40; reflection RVA8ab50,
    // 280310 and SetPropertyValue RVA202930. See weapon-property-bridge-contract.
    error.clear();WeaponSnapshot* w=nullptr;
    for(auto& candidate:weapons_)if(candidate.definition.name==name){w=&candidate;break;}
    if(!w){error="No staged weapon named "+name;return false;}
    if(static_cast<unsigned>(change)>4)return reject(error,"unknown weapon value change mode");
    if(dataValue&&requested&&std::holds_alternative<std::string>(*requested)&&std::get<std::string>(*requested)=="null")requested.reset();
    WeaponScalar current;std::string kind;
    if(dataValue){
        auto t=w->definition.fieldTypes.find(property);
        if(t==w->definition.fieldTypes.end()||!dataProperty(name,property,current,error))return reject(error,"unrepresented weapon data property "+property);
        kind=t->second;
    }else{
        if(!stateProperty(name,property,current,error))return false;
        kind=std::holds_alternative<bool>(current)?"bool":(property=="Ammo"||property=="Volley"||property=="ClipShotsRemaining")?"int32":"float32";
    }
    WeaponScalar value;
    if(kind=="bool"){
        if(!requested)return reject(error,"empty any cannot set native boolean "+property);
        if(auto b=std::get_if<bool>(&*requested))value=*b;
        else if(auto s=std::get_if<std::string>(&*requested)){
            auto lowered=*s;for(char& c:lowered)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            value=lowered=="true"; // Other native const-char* strings store false.
        }else return reject(error,"numeric any cannot set native boolean "+property);
    }else if(kind=="string"){
        if(!requested)value=std::string{};
        else if(auto s=std::get_if<std::string>(&*requested))value=*s;
        else return reject(error,"numeric/bool any cannot set native string "+property);
    }else if(kind=="float32"||kind=="int32"){
        const bool numeric=requested&&std::holds_alternative<double>(*requested);
        const double raw=numeric?std::get<double>(*requested):0;
        if(!std::isfinite(raw))return reject(error,"nonfinite script weapon value");
        const float incoming=static_cast<float>(raw);if(!std::isfinite(incoming))return reject(error,"script weapon float overflow");
        const double old=std::get<double>(current);double result=0;
        if(change==WeaponValueChange::Absolute){
            // Absolute SetPropertyValue rejects present wrong types. Relative
            // ConvertAnyToFloat/Int instead returns 0 for those same types.
            if(requested&&!numeric)return reject(error,"nonnumeric any cannot set native number "+property);
            result=kind=="float32"?static_cast<double>(incoming):std::trunc(raw);
        }else if(change==WeaponValueChange::Multiply){
            const float product=static_cast<float>(static_cast<float>(old)*incoming);
            result=kind=="float32"?static_cast<double>(product):static_cast<double>(std::round(product));
        }else if(change==WeaponValueChange::Add){
            result=kind=="float32"?static_cast<double>(static_cast<float>(static_cast<float>(old)+incoming)):old+std::trunc(raw);
        }else if(change==WeaponValueChange::MultiplyBase){
            if(!dataValue)return reject(error,"live Weapon MultiplyBase source category not recovered");
            // Generic GameData reflection has only Name/Organization/InheritFrom.
            // A numeric WeaponData key is absent there; base helper returns
            // empty any, recursive Add therefore adds 0. Do NOT use catalog base.
            result=old;
        }else return reject(error,"native ReplaceIfNull rejects int/float fields");
        if(!std::isfinite(result)||(kind=="int32"&&(result<-2147483648.0||result>2147483647.0)))return reject(error,"script weapon arithmetic outside supported finite range");
        value=result;
    }else return reject(error,"unrecovered script conversion for weapon type "+kind);
    if(dataValue)return setProperty(name,property,value,error);
    // Reflection writes live fields directly; no MaxAmmo/ClipSize data hooks.
    if(property=="Ammo")w->ammo=static_cast<int32_t>(std::get<double>(value));
    else if(property=="Volley")w->volley=static_cast<int32_t>(std::get<double>(value));
    else if(property=="ClipShotsRemaining")w->clipShotsRemaining=static_cast<int32_t>(std::get<double>(value));
    else if(property=="Charging")w->charging=std::get<bool>(value);
    else if(property=="ReloadPaused")w->reloadPaused=std::get<bool>(value);
    else if(property=="RequireControlRelease")w->requireControlRelease=std::get<bool>(value);
    else if(property=="ChargeTimeRemaining")w->chargeTimeRemaining=static_cast<float>(std::get<double>(value));
    else if(property=="CooldownTimeRemaining")w->cooldownTimeRemaining=static_cast<float>(std::get<double>(value));
    else if(property=="ReloadTimeRemaining")w->reloadTimeRemaining=static_cast<float>(std::get<double>(value));
    else if(property=="NextClipRegenTime")w->nextClipRegenTime=static_cast<float>(std::get<double>(value));
    else if(property=="LastFireTime")w->lastFireTime=static_cast<float>(std::get<double>(value));
    else if(property=="Scatter")w->scatter=static_cast<float>(std::get<double>(value));
    else if(property=="ControlTime")w->controlTime=static_cast<float>(std::get<double>(value));
    else return reject(error,"live weapon property is read-only in recovered subset "+property);
    return true;
}
bool WeaponArsenal::stateProperty(const std::string& name,const std::string& property,WeaponScalar& output,std::string& error)const{
    error.clear();for(const auto& w:weapons_)if(w.definition.name==name){
        if(property=="Ammo")output=static_cast<double>(w.ammo);
        else if(property=="Charging")output=w.charging;
        else if(property=="Volley")output=static_cast<double>(w.volley);
        else if(property=="ChargeTimeRemaining")output=static_cast<double>(w.chargeTimeRemaining);
        else if(property=="CooldownTimeRemaining")output=static_cast<double>(w.cooldownTimeRemaining);
        else if(property=="ReloadTimeRemaining")output=static_cast<double>(w.reloadTimeRemaining);
        else if(property=="ClipShotsRemaining")output=static_cast<double>(w.clipShotsRemaining);
        else if(property=="ReloadPaused")output=w.reloadPaused;
        else if(property=="NextClipRegenTime")output=static_cast<double>(w.nextClipRegenTime);
        else if(property=="LastFireTime")output=static_cast<double>(w.lastFireTime);
        else if(property=="Scatter")output=static_cast<double>(w.scatter);
        else if(property=="ControlTime")output=static_cast<double>(w.controlTime);
        else if(property=="RequireControlRelease")output=w.requireControlRelease;
        else return reject(error,"unknown live weapon property "+property);
        return true;
    }
    error="No staged weapon named "+name;return false;
}
bool WeaponArsenal::getAmmo(const std::string& name,int32_t& result,std::string& error) const {
    // Native Weapon::GetAmmo RVA83af0 follows an existing ShareAmmoWeapon,
    // otherwise returns this weapon's ammo; no sum/local extra depletion here.
    error.clear();std::set<std::string> visited;std::string selected=name;
    while(true){
        if(!visited.insert(selected).second)return reject(error,"cyclic native shared-ammo getter outside supported subset");
        const WeaponSnapshot* w=nullptr;for(const auto& value:weapons_)if(value.definition.name==selected){w=&value;break;}
        if(!w){error="No owned weapon named "+selected;return false;}
        if(!w->definition.shareAmmoWeapon.empty()&&hasWeapon(w->definition.shareAmmoWeapon)){selected=w->definition.shareAmmoWeapon;continue;}
        result=w->ammo;return true;
    }
}
bool WeaponArsenal::isPerfectCharge(const std::string& name,std::string& error) const {
    error.clear();const auto* w=weaponState(name,error);if(!w)return false;
    auto v=w->definition.fields.find("PerfectChargeWindowDuration");
    if(v==w->definition.fields.end()||!std::holds_alternative<double>(v->second)){error="Missing native PerfectChargeWindowDuration";return false;}
    return std::get<double>(v->second)>0&&(w->lockedPerfectCharge||w->perfectChargeWindowRemaining>0);
}
bool WeaponArsenal::recordProjectilePerfectCharge(const std::string& name,bool perfect,std::string& error){
    // Projectile::Init VA180069c81 stores Weapon+12; distinct from current
    // IsPerfectCharge query used by Script::OnWeaponFired before cancellation.
    error.clear();for(auto& w:weapons_)if(w.definition.name==name){w.wasLastFirePerfectCharge=perfect;return true;}
    error="No owned firing weapon named "+name;return false;
}
bool WeaponArsenal::recordProjectileHit(const std::string& name,int32_t volley,int32_t fireIndex,
        int32_t victim,bool victimIsUnit,bool invulnerable,float worldTime,bool& first,std::string& error){
    error.clear();if(!std::isfinite(worldTime)){error="Invalid native hit volley clock";return false;}
    for(auto& w:weapons_)if(w.definition.name==name){
        first=true;if(w.definition.type!=0||volley<0)return true;
        const auto field=w.definition.fields.find("TreatProjectilesAsUniqueVolleys");
        if(field==w.definition.fields.end()||!std::holds_alternative<bool>(field->second)){error="Native hit volley flag unavailable";return false;}
        if(std::get<bool>(field->second))return true;
        const size_t slot=unsigned(volley)%4;
        if(!w.volleySlotInitialized[slot]||fireIndex<0||size_t(fireIndex)>=w.volleyHitTimes[slot].size()){error="Actual GUN volley slot/fire index has not been created";return false;}
        // TakeHit VA1802b8bxx..8e5b repeatedly copies the returned vector,
        // mutates only a temporary, and frees it. Preserve that native behavior
        // rather than repairing its apparent value-return bug.
        auto copy=w.volleyHitTimes[slot];
        if(copy[size_t(fireIndex)]>=worldTime){copy[size_t(fireIndex)]=worldTime;first=std::all_of(copy.begin(),copy.end(),[&](float n){return n>=worldTime;});}
        else first=false;
        if(victimIsUnit&&!invulnerable&&w.volleyFirstHitId[slot]==-1)w.volleyFirstHitId[slot]=victim;
        return true;
    }
    error="Native hit lost actual firing weapon "+name;return false;
}
bool WeaponArsenal::hasAmmo(const std::string& name,std::string& error)const{
    error.clear();std::set<std::string> visited;std::string current=name;
    for(;;){
        if(!visited.insert(current).second)return reject(error,"cyclic shared ammo reference "+current);
        const WeaponSnapshot* selected=nullptr;for(const auto& w:weapons_)if(w.definition.name==current){selected=&w;break;}
        if(!selected){error="No staged weapon named "+current;return false;}
        const auto& shared=selected->definition.shareAmmoWeapon;
        if(!shared.empty()&&hasWeapon(shared)){current=shared;continue;}
        // Missing shared target falls back to this weapon, native VA839b7.
        return selected->definition.maxAmmo<0||selected->ammo>0;
    }
}
bool WeaponArsenal::requestFire(const std::string& name,WeaponFireRequest request,const WeaponOwnerFrame& owner,std::string& error){
    // Native RVA83160 / REA request-fire. No World obstacle search or fabricated
    // EntityLinkedObject pointer is substituted for an unsupported target.
    error.clear();if(retirementRequested_)return false;if(owner.ownerId!=ownerId_)return reject(error,"requestFire owner mismatch");
    WeaponSnapshot* w=nullptr;for(auto& candidate:weapons_)if(candidate.definition.name==name){w=&candidate;break;}
    if(!w){error="No owned weapon named "+name;return false;}
    auto flag=[&](const WeaponDefinition& d,const char* k,bool& b){auto i=d.fields.find(k);if(i==d.fields.end()||!std::holds_alternative<bool>(i->second))return reject(error,std::string("missing RequestFire flag ")+k);b=std::get<bool>(i->second);return true;};
    auto text=[&](const char* k,std::string& s){auto i=w->definition.fields.find(k);if(i==w->definition.fields.end()||!std::holds_alternative<std::string>(i->second))return reject(error,std::string("missing RequestFire text ")+k);s=std::get<std::string>(i->second);return true;};
    const float values[]{request.fromX,request.fromY,request.targetX,request.targetY,request.angle,owner.angle};
    for(float f:values)if(!std::isfinite(f))return reject(error,"nonfinite FireRequest");
    if(!owner.exists||!owner.enabled)return false;
    bool enabled=false,automatic=false,ignoreCooldown=false,priority=false,multi=false;
    if(!flag(w->definition,"Enabled",enabled)||!flag(w->definition,"FullyAutomatic",automatic)||!flag(w->definition,"IgnoreTriggerLockDuringCooldown",ignoreCooldown)||!flag(w->definition,"PriorityFireRequest",priority)||!flag(w->definition,"AllowMultiFireRequest",multi))return false;
    if(!enabled||w->requireControlRelease)return false;
    const float fraction=w->definition.chargeTime==0?1.0f:std::clamp(1.0f-w->chargeTimeRemaining/w->definition.chargeTime,0.0f,1.0f);
    if(!automatic&&fraction<=0&&w->chargeTimeRemaining<=0)return false;
    if(!hasAmmo(name,error)){if(!error.empty())return false;std::string failed;if(!text("FailedToFireCooldownAnimation",failed))return false;if(!failed.empty())return false;}
    if(w->definition.clipSize!=0&&w->clipShotsRemaining==0)return false;
    if(ignoreCooldown&&w->cooldownTimeRemaining>0)return false;
    std::string obstacle;
    for(const auto* key:{"FireFromObstacle","FireToObstacle"}){if(!text(key,obstacle))return false;if(!obstacle.empty())return reject(error,"actual World obstacle fire search required "+obstacle);}
    if(!priority&&!multi)for(const auto& other:weapons_)if(other.currentFireRequest){
        bool otherPriority=false,otherMulti=false;
        if(!flag(other.definition,"PriorityFireRequest",otherPriority)||!flag(other.definition,"AllowMultiFireRequest",otherMulti))return false;
        if(otherPriority&&!otherMulti)return false;
    }
    if(request.targetId!=-1)return reject(error,"target FireRequest needs actual parent entity-link identity");
    if(request.angle<0)request.angle=owner.angle;
    w->currentFireRequest=request;return true;
}
bool WeaponArsenal::requestTouchdownFire(const std::string& name,const WeaponOwnerFrame& owner,std::string& error){
    // Matching PDB Weapon::RequestFire argument list d686 = float,Vector2,
    // Thing*,bool,Vector2; Unit.OnTouchdown VA2f6bd6..2f6bff supplies
    // -1,zero,null,true,zero. The bool is captured FireRequest.applySelfEffects
    // at VA83368d/694, not a guessed independent auto-targeting mode.
    WeaponFireRequest request;request.angle=-1;request.targetId=-1;
    request.fromX=request.fromY=request.targetX=request.targetY=0;
    request.applySelfEffects=true;
    return requestFire(name,request,owner,error);
}
bool WeaponArsenal::clearFireRequest(const std::string& name,std::string& error){
    error.clear();for(auto& w:weapons_)if(w.definition.name==name){w.currentFireRequest.reset();return true;}
    error="No owned weapon named "+name;return false;
}
bool WeaponArsenal::removeWeaponControl(const std::string& name,bool& cancelCharge,std::string& error){
    cancelCharge=false;error.clear();WeaponSnapshot* weapon=nullptr;
    for(auto& candidate:weapons_)if(candidate.definition.name==name){weapon=&candidate;break;}
    if(!weapon)return true; // Native null Weapon pointer returns immediately.
    const auto flag=weapon->definition.fields.find("CancelChargeOnControlRemoved");
    if(flag==weapon->definition.fields.end()||!std::holds_alternative<bool>(flag->second))return reject(error,"Missing native CancelChargeOnControlRemoved");
    cancelCharge=std::get<bool>(flag->second);
    std::erase(controllableOrder_,name);weapon->controllable=false;weapon->currentFireRequest.reset();return true;
}
bool WeaponArsenal::swapWeaponControl(const std::string& from,const std::string& to,const WeaponOwnerFrame& owner,std::string& error){
    // Native RVA2fed10: remove all old pointers, append target only if absent;
    // carry input request before ForceReleaseOnSwap sets require-release flags.
    error.clear();if(owner.ownerId!=ownerId_)return reject(error,"swap control owner mismatch");
    WeaponSnapshot *old=nullptr,*target=nullptr;for(auto& w:weapons_){if(w.definition.name==from)old=&w;if(w.definition.name==to)target=&w;}
    if(!old)return reject(error,"swap source weapon is not owned");
    if(to.empty()||!target)return false;
    auto flag=[&](const WeaponSnapshot& w,const char* k,bool& b){auto i=w.definition.fields.find(k);if(i==w.definition.fields.end()||!std::holds_alternative<bool>(i->second))return reject(error,std::string("missing SwapWeaponControl flag ")+k);b=std::get<bool>(i->second);return true;};
    bool transfer=false,clear=false,oldRelease=false,newRelease=false;
    if(!flag(*old,"LockTriggerTransferFromOnSwap",transfer)||!flag(*old,"ClearFireRequestOnSwap",clear)||!flag(*old,"ForceReleaseOnSwap",oldRelease)||!flag(*target,"ForceReleaseOnSwap",newRelease))return false;
    auto order=controllableOrder_;order.erase(std::remove(order.begin(),order.end(),from),order.end());
    if(std::find(order.begin(),order.end(),to)==order.end())order.push_back(to);
    const std::string chain=old->gainedControlFrom.empty()?from:old->gainedControlFrom;
    if(old->currentFireRequest&&transfer){
        auto request=*old->currentFireRequest;request.applySelfEffects=true;request.fromX=0;request.fromY=0;
        // Native ignores an ordinary RequestFire gate failure, but a missing
        // reconstruction branch is surfaced before control membership changes.
        const bool accepted=requestFire(to,request,owner,error);if(!accepted&&!error.empty())return false;
        old->currentFireRequest.reset();
    }
    if(clear)old->currentFireRequest.reset();
    target->gainedControlFrom=chain;
    if(target->definition.controlWindow>0)target->controlTime=target->definition.controlWindow;
    if(oldRelease)old->requireControlRelease=true;
    if(newRelease)target->requireControlRelease=true;
    controllableOrder_=std::move(order);
    for(auto& w:weapons_)w.controllable=std::find(controllableOrder_.begin(),controllableOrder_.end(),w.definition.name)!=controllableOrder_.end();
    return true;
}
std::vector<std::string> WeaponArsenal::controllableWeapons()const{return controllableOrder_;}
bool WeaponArsenal::addWeaponControl(const std::string& name,std::string& error){
    error.clear();WeaponSnapshot* w=nullptr;for(auto& c:weapons_)if(c.definition.name==name){w=&c;break;}
    if(!w)return true; // native null pointer returns
    if(std::find(controllableOrder_.begin(),controllableOrder_.end(),name)==controllableOrder_.end())controllableOrder_.push_back(name);
    w->controllable=true;return true;
}
bool WeaponArsenal::checkWeaponControl(const std::function<bool(const std::string&)>& down,std::vector<std::string>& lose,std::string& error){
    // PlayerUnitInput::CheckWeaponControl2ef100 (.local/reports/native-combat-check-weapon-control-pseudocode.c).
    error.clear();lose.clear();
    auto flag=[&](const WeaponSnapshot& w,const char* k,bool& b){auto i=w.definition.fields.find(k);if(i==w.definition.fields.end()||!std::holds_alternative<bool>(i->second))return reject(error,std::string("missing weapon control flag ")+k);b=std::get<bool>(i->second);return true;};
    for(const auto& name:controllableOrder_){WeaponSnapshot* w=nullptr;for(auto& c:weapons_)if(c.definition.name==name){w=&c;break;}if(!w)continue;
        bool notCharging=false,onRelease=false;if(!flag(*w,"LoseControlIfNotCharging",notCharging)||!flag(*w,"LoseControlOnRelease",onRelease))return false;
        if(notCharging&&w->definition.controlWindow>0&&w->controlTime<=0&&!w->charging)lose.push_back(name);
        if(onRelease&&!down(w->definition.controls[0]))lose.push_back(name);
        if(!w->gainedControlFrom.empty())for(auto& g:weapons_)if(g.definition.name==w->gainedControlFrom){if(g.requireControlRelease&&!down(g.definition.controls[0]))g.requireControlRelease=false;break;}
    }
    return true;
}
void WeaponArsenal::clearReleasedControls(const std::function<bool(const std::string&)>& down){
    // Final CheckWeaponControl loop over the full arsenal (Unit+0x430).
    for(auto& w:weapons_)if(w.requireControlRelease&&!down(w.definition.controls[0]))w.requireControlRelease=false;
}
bool WeaponArsenal::setDelayedProjectile(const std::string& name,uint64_t handle,std::string& error){
    error.clear();for(auto& w:weapons_)if(w.definition.name==name){w.delayedProjectileHandle=handle;return true;}
    error="No owned weapon named "+name;return false;
}
bool WeaponArsenal::clearDelayedProjectiles(const std::function<bool(uint64_t,bool&,std::string&)>& release,std::string& error){
    error.clear();for(auto& w:weapons_)if(w.delayedProjectileHandle){bool clear=false;if(!release(w.delayedProjectileHandle,clear,error))return false;if(clear)w.delayedProjectileHandle=0;}
    return true;
}
bool WeaponArsenal::finishLostControl(const std::string& name,std::string& error){
    error.clear();WeaponSnapshot* w=nullptr;for(auto& c:weapons_)if(c.definition.name==name){w=&c;break;}
    if(!w||w->gainedControlFrom.empty())return true;
    const auto from=w->gainedControlFrom;if(!addWeaponControl(from,error))return false;w->gainedControlFrom.clear();return true;
}
bool WeaponArsenal::retireOwner(const WeaponRetirementProviders& providers,bool& released,std::string& error){
    released=false;error.clear();
    if(weapons_.empty()){retirementRequested_=true;released=true;return true;}
    if(!providers.retained){error="Arsenal retirement requires actual source-reference ownership queries";return false;}
    retirementRequested_=true;controllableOrder_.clear();for(auto& weapon:weapons_)weapon.controllable=false;
    for(auto it=weapons_.begin();it!=weapons_.end();){
        bool retained=false;if(!providers.retained(it->definition.name,retained,error))return false;
        if(retained){++it;continue;}
        // Native WeaponArsenal::~WeaponArsenal2fe3c0 last-reference actions.
        // No charge-cancel or effect callbacks are invented during destruction.
        if(it->fireFxLifetime){if(!providers.dropFireFx){error="Weapon retirement requires real pFireFx reference drop";return false;}if(!providers.dropFireFx(it->fireFxLifetime,error))return false;it->fireFxLifetime=0;}
        if(it->impactCueId){if(!providers.stopImpactCue){error="Weapon retirement requires actual impact-cue stop";return false;}if(!providers.stopImpactCue(it->impactCueId,error))return false;it->impactCueId=0;}
        it->disposed=true;it->ownerId=-1;it->ownerName.clear();
        retiredWeapons_.push_back({it->definition.name,-1,true});if(retiredWeapons_.size()>64)retiredWeapons_.erase(retiredWeapons_.begin());
        it=weapons_.erase(it); // Full copied data freed when no real references remain.
    }
    released=weapons_.empty();error.clear();return true;
}
bool WeaponArsenal::isStrafing(const std::string& ignored,std::string& error)const{
    // Native Arsenal RVA2fe7e0 excludes the supplied weapon, requires another
    // Strafe/current request/ammo, with reload remaining <=0.
    error.clear();for(const auto& w:weapons_){
        if(w.definition.name==ignored)continue;
        auto f=w.definition.fields.find("Strafe");
        if(f==w.definition.fields.end()||!std::holds_alternative<bool>(f->second))return reject(error,"missing native Strafe");
        if(!std::get<bool>(f->second)||!w.currentFireRequest)continue;
        const bool ammo=hasAmmo(w.definition.name,error);if(!error.empty())return false;
        if(ammo&&w.reloadTimeRemaining<=0)return true;
    }return false;
}
bool WeaponArsenal::finishFireRequestFrame(const std::string& name,bool fired,bool failed,std::string& error){
    // Tail of native Weapon::Update RVA83b60; caller must first complete actual
    // trigger/release, independent timers and native callback operations.
    error.clear();for(auto& w:weapons_)if(w.definition.name==name){
        auto f=w.definition.fields.find("FireOnRelease");if(f==w.definition.fields.end()||!std::holds_alternative<bool>(f->second))return reject(error,"missing FireOnRelease");
        w.previousFireRequest=w.currentFireRequest;
        if((fired||failed||std::get<bool>(f->second))&&w.currentFireRequest)w.currentFireRequest.reset();
        return true;
    }
    return reject(error,"request-frame weapon is not owned");
}
bool WeaponArsenal::canFire(const std::string& name,const WeaponOwnerFrame& owner,std::string& error)const{
    if(retirementRequested_){error.clear();return false;}
    error.clear();if(owner.ownerId!=ownerId_)return reject(error,"canFire owner ID mismatch");
    const WeaponSnapshot* w=nullptr;for(const auto& candidate:weapons_)if(candidate.definition.name==name){w=&candidate;break;}
    if(!w){error="No staged weapon named "+name;return false;}
    const auto& d=w->definition;
    auto boolean=[&](const std::string& key,bool& value){auto i=d.fields.find(key);if(i==d.fields.end()||!std::holds_alternative<bool>(i->second))return reject(error,"missing canFire boolean "+key);value=std::get<bool>(i->second);return true;};
    auto number=[&](const std::string& key,float& value){auto i=d.fields.find(key);if(i==d.fields.end()||!std::holds_alternative<double>(i->second))return reject(error,"missing canFire number "+key);value=static_cast<float>(std::get<double>(i->second));if(!std::isfinite(value))return reject(error,"nonfinite canFire number "+key);return true;};
    bool enabled=false,ignoreDisabled=false,cancelDisables=false;float minZ=0,maxZ=0;
    if(!boolean("Enabled",enabled)||!boolean("IgnoreOwnerAttackDisabled",ignoreDisabled)||!boolean("CanCancelDisables",cancelDisables)||!number("MinFireZ",minZ)||!number("MaxFireZ",maxZ))return false;
    if(!enabled)return false;
    if(owner.exists){
        if(!owner.enabled)return false;
        if((owner.animationBlocksAttack||!(cancelDisables?owner.canAttackAllowCancellation:owner.canAttack))&&!ignoreDisabled)return false;
        if(!std::isfinite(owner.totalZ))return reject(error,"nonfinite owner total Z");
        if(owner.totalZ>maxZ||minZ>owner.totalZ)return false;
    }
    // With no live owner the native common canFire still evaluates timers;
    // Weapon::Update itself has a separate UnitManager::Get early return.
    if(w->cooldownTimeRemaining>0&&d.bundled[6].empty())return false;
    if(!hasAmmo(name,error))return false;
    if(w->reloadTimeRemaining>0||(d.clipSize>0&&w->clipShotsRemaining==0))return false;
    // BlinkWeapon override requires a present, alive owner after common gate.
    if(d.type==1&&(!owner.exists||!owner.alive))return false;
    return true;
}
bool WeaponArsenal::fireOffset(const std::string& name,float angle,WeaponPoint& output,std::string& error)const{
    // Weapon::GetFireOffset86010 (.local/reports/native-combat-weapon-get-fire-offset-pseudocode.c):
    // max(BarrelLength,0)*(1-|sin a|*0.35000002) along a plus FireOffsetX/Y;
    // BarrelRequiresLoS(+0x4ac) returns zero when owner->point LOS is blocked.
    error.clear();if(!std::isfinite(angle))return reject(error,"nonfinite fire offset angle");
    const WeaponDefinition* d=nullptr;for(const auto& w:weapons_)if(w.definition.name==name){d=&w.definition;break;}
    if(!d){error="No staged weapon named "+name;return false;}
    auto number=[&](const std::string& key,float& value){auto i=d->fields.find(key);if(i==d->fields.end()||!std::holds_alternative<double>(i->second))return reject(error,"missing fire-offset number "+key);value=static_cast<float>(std::get<double>(i->second));return std::isfinite(value);};
    float length=0,x=0,y=0;if(!number("BarrelLength",length)||!number("FireOffsetX",x)||!number("FireOffsetY",y))return false;
    auto los=d->fields.find("BarrelRequiresLoS");if(los==d->fields.end()||!std::holds_alternative<bool>(los->second))return reject(error,"missing BarrelRequiresLoS");
    if(std::get<bool>(los->second))return reject(error,"BarrelRequiresLoS needs actual World line-of-sight query");
    const float sine=std::sin(angle);const float barrel=(1.0f-std::abs(sine)*0.35000002384185791015625f)*std::max(length,0.0f);
    output.x=std::cos(angle)*barrel+x;output.y=-(barrel*sine)+y;
    if(!std::isfinite(output.x)||!std::isfinite(output.y))return reject(error,"fire offset overflow");return true;
}
bool WeaponArsenal::fireOrigin(const std::string& name,const WeaponOwnerFrame& owner,float angle,WeaponPoint& output,std::string& error)const{
    error.clear();if(owner.ownerId!=ownerId_||!owner.exists)return reject(error,"fire origin needs actual matching owner");
    if(!std::isfinite(angle)||!std::isfinite(owner.x)||!std::isfinite(owner.y))return reject(error,"nonfinite fire origin input");
    const WeaponDefinition* d=nullptr;for(const auto& w:weapons_)if(w.definition.name==name){d=&w.definition;break;}
    if(!d){error="No staged weapon named "+name;return false;}
    auto number=[&](const std::string& key,float& value){auto i=d->fields.find(key);if(i==d->fields.end()||!std::holds_alternative<double>(i->second))return reject(error,"missing fire-origin number "+key);value=static_cast<float>(std::get<double>(i->second));return std::isfinite(value);};
    float length=0,x=0,y=0;if(!number("BarrelLength",length)||!number("FireOffsetX",x)||!number("FireOffsetY",y))return false;
    auto los=d->fields.find("BarrelRequiresLoS");if(los==d->fields.end()||!std::holds_alternative<bool>(los->second))return reject(error,"missing BarrelRequiresLoS");
    if(std::get<bool>(los->second))return reject(error,"BarrelRequiresLoS needs actual World line-of-sight query");
    // GetFireOffset VA0x180086040..d4. Native barrel-length foreshortening
    // constant is exact float bits0x3eb33334; do not substitute screen camera.
    const float sine=std::sin(angle);const float barrel=std::max(length,0.0f)*(1.0f-std::abs(sine)*0.35000002384185791015625f);
    WeaponPoint result;result.x=owner.x+(std::cos(angle)*barrel+x);result.y=owner.y+(-sine*barrel+y);
    if(!std::isfinite(result.x)||!std::isfinite(result.y))return reject(error,"fire origin overflow");
    output=result;return true;
}
bool WeaponArsenal::planGunProjectiles(const std::string& name,const WeaponOwnerFrame& owner,float angle,
                                      std::span<const double> randomTape,WeaponVolleyPlan& output,std::string& error)const{
    error.clear();const WeaponSnapshot* w=nullptr;for(const auto& candidate:weapons_)if(candidate.definition.name==name){w=&candidate;break;}
    if(!w){error="No staged weapon named "+name;return false;}
    const auto& d=w->definition;if(d.type!=0)return reject(error,"Gun projectile layout requires native GUN type");
    WeaponVolleyPlan plan;plan.nextVolley=w->volley;
    if(!w->validProjectileData){output=std::move(plan);return true;}
    if(w->volley==INT32_MAX)return reject(error,"volley counter overflow");plan.nextVolley=w->volley+1;
    WeaponPoint from;if(!fireOrigin(name,owner,angle,from,error))return false;
    auto number=[&](const auto& fields,const std::string& key,float& v){auto i=fields.find(key);if(i==fields.end()||!std::holds_alternative<double>(i->second))return reject(error,"missing projectile-layout number "+key);v=static_cast<float>(std::get<double>(i->second));if(!std::isfinite(v))return reject(error,"nonfinite projectile-layout number "+key);return true;};
    auto flag=[&](const auto& fields,const std::string& key,bool& v){auto i=fields.find(key);if(i==fields.end()||!std::holds_alternative<bool>(i->second))return reject(error,"missing projectile-layout flag "+key);v=std::get<bool>(i->second);return true;};
    float cap=0,waves=0,count=0,bonus=0,angleStart=0,checkOffset=0,impactVelocity=0;bool refresh=false,ammoCount=false,randomAngle=false,impactCheck=false,manualTarget=false;
    if(!number(d.fields,"ActiveProjectileCap",cap)||!number(d.fields,"NumProjectileWaves",waves)||!number(d.fields,"NumProjectiles",count)||!number(d.fields,"BonusProjectileChance",bonus)||!number(d.fields,"ProjectileAngleStartOffset",angleStart)||!number(d.fields,"InvulnerableImpactCheckOffset",checkOffset)||!flag(d.fields,"RefreshProjectileOnFire",refresh)||!flag(d.fields,"NumProjectilesIsAmmo",ammoCount)||!flag(d.fields,"RandomFireAngle",randomAngle)||!number(w->projectileData.fields,"InvulnerableImpactOwnerVelocity",impactVelocity)||!flag(w->projectileData.fields,"CheckObstacleImpact",impactCheck)||!flag(w->projectileData.fields,"ManuallySetTarget",manualTarget))return false;
    if(cap>0||refresh||ammoCount||manualTarget)return reject(error,"projectile manager cap/refresh/ammo-count/explicit-target branch");
    if(waves<0||waves>128||std::floor(waves)!=waves||count<0||count>4096||std::floor(count)!=count||waves*(count+1)>4096)return reject(error,"projectile count bounds");
    // First player data uses zero spread/offset spacing/delays. Keep other
    // source branches explicit until their native angle normalization is joined.
    for(const char* key:{"Spread","FullChargeSpread","ProjectileAngleOffset","ProjectileAngleOffsetMin","ProjectileAngleOffsetMax","ProjectileSpacing","ProjectileInterval","ProjectileWaveInterval"}){float v=0;if(!number(d.fields,key,v))return false;if(v!=0)return reject(error,std::string("projectile layout branch ")+key);}
    const float tau=6.283185482025146484375f;
    for(int wave=0;wave<static_cast<int>(waves);++wave){
        if(randomTape.size()-plan.randomValuesUsed<2)return reject(error,"Gun layout requires two original RNG samples per wave");
        const double spreadRandom=randomTape[plan.randomValuesUsed++],bonusRandom=randomTape[plan.randomValuesUsed++];
        if(!std::isfinite(spreadRandom)||!std::isfinite(bonusRandom)||spreadRandom<0||spreadRandom>=1||bonusRandom<0||bonusRandom>=1)return reject(error,"invalid original RNG sample");
        float firingAngle=randomAngle?static_cast<float>(spreadRandom)*tau:angle;
        if(!randomAngle)firingAngle-=std::floor(firingAngle/tau)*tau;
        if(impactVelocity>0&&impactCheck){
            WeaponOutput query;query.kind=WeaponOutputKind::CheckInvulnerableBarrelImpact;query.ownerId=ownerId_;query.weaponName=d.name;query.projectile=w->projectileSource;query.angle=firingAngle;query.value=impactVelocity;
            query.x=from.x;query.y=from.y;if(checkOffset>0){query.x+=std::cos(firingAngle)*checkOffset;query.y-=std::sin(firingAngle)*checkOffset;}
            query.z=owner.z;query.waveIndex=wave;plan.outputs.push_back(std::move(query));
        }
        const int projectiles=static_cast<int>(count)+(bonusRandom<=static_cast<double>(bonus)?1:0);
        for(int index=0;index<projectiles;++index){
            WeaponOutput spawn;spawn.kind=WeaponOutputKind::ProjectileSpawn;spawn.ownerId=ownerId_;spawn.weaponName=d.name;spawn.name=w->projectileData.name;spawn.projectile=w->projectileSource;
            spawn.x=from.x;spawn.y=from.y;spawn.z=owner.z;spawn.angle=firingAngle+angleStart;spawn.waveIndex=wave;spawn.projectileIndex=index+wave*projectiles;spawn.volleyIndex=plan.nextVolley;
            spawn.value=d.chargeTime<=0?1:std::clamp(1.0f-w->chargeTimeRemaining/d.chargeTime,0.0f,1.0f);
            plan.outputs.push_back(std::move(spawn));
        }
    }
    output=std::move(plan);return true;
}
bool WeaponArsenal::spendAmmo(const std::string& name,std::string& error){
    std::vector<WeaponOutput> outputs;return spendAmmo(name,outputs,{},error);
}
bool WeaponArsenal::spendAmmo(const std::string& name,std::vector<WeaponOutput>& outputs,const WeaponOutputSupport& supports,std::string& error){
    error.clear();std::vector<int32_t> staged;staged.reserve(weapons_.size());for(const auto& w:weapons_)staged.push_back(w.ammo);std::vector<WeaponOutput> events;std::set<std::string> visiting;
    std::function<bool(const std::string&)> spend=[&](const std::string& current){
        if(!visiting.insert(current).second)return reject(error,"cyclic shared ammo spend "+current);
        size_t selected=weapons_.size();for(size_t wi=0;wi<weapons_.size();++wi)if(weapons_[wi].definition.name==current){selected=wi;break;}
        if(selected==weapons_.size()){error="No staged weapon named "+current;return false;}
        const auto& shared=weapons_[selected].definition.shareAmmoWeapon;
        // Native recursively spends the shared weapon, then continues local
        // depletion as well (VA0x180088eae..eb3); this is not a redirect/return.
        if(!shared.empty()&&hasWeapon(shared)&&!spend(shared))return false;
        if(staged[selected]!=-1){
            if(staged[selected]>0)--staged[selected];
            staged[selected]=std::min(std::max(staged[selected],0),weapons_[selected].definition.maxAmmo);
            if(staged[selected]==0){WeaponOutput e;e.kind=WeaponOutputKind::ScriptCallback;e.ownerId=ownerId_;e.weaponName=current;e.name="OnOutOfAmmo";events.push_back(std::move(e));}
        }
        visiting.erase(current);return true;
    };
    if(!spend(name))return false;
    for(const auto& e:events){if(!supports)return reject(error,"finite ammo needs OnOutOfAmmo consumer");if(!supports(e,error)){if(error.empty())error="Unsupported OnOutOfAmmo callback";return false;}}
    for(size_t wi=0;wi<staged.size();++wi)weapons_[wi].ammo=staged[wi];outputs=std::move(events);return true;
}
bool WeaponArsenal::step(float dt,const WeaponOwnerFrame& owner,std::span<const WeaponTrigger> triggers,
                         std::vector<WeaponOutput>& outputs,const WeaponOutputSupport& supports,std::string& error){
    error.clear();
    if(retirementRequested_){outputs.clear();return true;}
    if(!std::isfinite(dt)||dt<0)return reject(error,"invalid simulation delta");
    if(owner.ownerId!=ownerId_)return reject(error,"owner frame actual Unit ID mismatch");
    if(!owner.exists){outputs.clear();return true;} // native Update UnitManager::Get early return
    if(!std::isfinite(owner.worldTime)||!std::isfinite(owner.x)||!std::isfinite(owner.y)||!std::isfinite(owner.z)||!std::isfinite(owner.angle))return reject(error,"invalid owner frame");
    // Active trigger handling is being reconstructed. Never advance only half
    // of a frame when the native input path requires missing side effects.
    if(owner.inputAllowed)for(const auto& t:triggers)if(t.down)return reject(error,"active trigger charge/fire path not yet reconstructed");
    for(const auto& w:weapons_)if(w.currentFireRequest||w.previousFireRequest)
        return reject(error,"pending native request needs trigger/release execution before independent timer step");
    std::vector<WeaponSimulationState> staged;staged.reserve(weapons_.size());for(const auto& w:weapons_)staged.push_back(w);std::vector<WeaponOutput> events;
    for(size_t wi=0;wi<staged.size();++wi){auto& w=staged[wi];
        const auto& d=weapons_[wi].definition;
        auto number=[&](const std::string& key,float& v)->bool{auto i=d.fields.find(key);if(i==d.fields.end()||!std::holds_alternative<double>(i->second))return reject(error,"missing source numeric update field "+key+" on "+d.name);v=static_cast<float>(std::get<double>(i->second));return std::isfinite(v);};
        auto flag=[&](const std::string& key,bool& v)->bool{auto i=d.fields.find(key);if(i==d.fields.end()||!std::holds_alternative<bool>(i->second))return reject(error,"missing source boolean update field "+key+" on "+d.name);v=std::get<bool>(i->second);return true;};
        auto emit=[&](WeaponOutputKind kind,const std::string& name){WeaponOutput e;e.kind=kind;e.ownerId=ownerId_;e.weaponName=d.name;e.name=name;events.push_back(std::move(e));};
        auto finishReload=[&](){w.clipShotsRemaining=d.clipSize;if(!d.swapOnReloadFinish.empty())emit(WeaponOutputKind::RemoveWeaponControl,d.swapOnReloadFinish);if(!d.reloadFinishGraphic.empty())emit(WeaponOutputKind::SetOwnerAnimation,d.reloadFinishGraphic);};
        bool clipRegenOnRelease=false,fullClipRegen=false,reloadOnRelease=false;float scatterSubside=0,scatterCap=0;
        if(!flag("ClipRegenOnRelease",clipRegenOnRelease)||!flag("FullClipRegen",fullClipRegen)||!flag("ReloadOnRelease",reloadOnRelease)||!number("ScatterSubside",scatterSubside)||!number("ScatterCap",scatterCap))return false;
        // Exact native Update VA0x180083d37..dbc: one refill at most per frame,
        // dt*(1/interval), then reset to interval (not a generic while loop).
        if(d.clipRegenInterval>0&&w.clipShotsRemaining!=d.clipSize){
            const bool triggerDown=owner.inputAllowed&&std::any_of(triggers.begin(),triggers.end(),[&](const auto& t){return t.down&&t.nativeControlIndex==w.controlIndices[0];});
            if(!(clipRegenOnRelease&&triggerDown)){
                w.nextClipRegenTime-=dt*(1.0f/d.clipRegenInterval);
                if(w.nextClipRegenTime<=0){w.clipShotsRemaining=fullClipRegen?d.clipSize:w.clipShotsRemaining+1;w.nextClipRegenTime=d.clipRegenInterval;}
            }
        }
        // Scatter rates are degrees/second; original converts through double
        // constant pi/180 before the float multiply and clamp.
        if(scatterSubside>0){w.scatter-=static_cast<float>(static_cast<double>(scatterSubside)*0.017453292519943295)*dt;w.scatter=std::min(std::max(w.scatter,0.0f),static_cast<float>(static_cast<double>(scatterCap)*0.017453292519943295));}
        if(w.cooldownTimeRemaining>0)w.cooldownTimeRemaining-=dt; // negative retained natively
        if(w.reloadTimeRemaining>0){if(!w.reloadPaused){w.reloadTimeRemaining-=dt;if(w.reloadTimeRemaining<=0)finishReload();}}
        else if(reloadOnRelease&&w.clipShotsRemaining!=d.clipSize){
            if(d.reloadTimeMin>0)return reject(error,"reload release requires original RNG stream");
            w.reloadTimeRemaining=d.reloadTime;if(w.reloadTimeRemaining<=0)finishReload();
        }
    }
    for(const auto& e:events){if(!supports)return reject(error,"parent has no weapon output consumer");if(!supports(e,error)){if(error.empty())error="Unsupported weapon output "+e.name;return false;}}
    for(size_t wi=0;wi<staged.size();++wi)static_cast<WeaponSimulationState&>(weapons_[wi])=staged[wi];outputs=std::move(events);return true;
}
std::vector<WeaponSnapshot> WeaponArsenal::snapshot() const { return weapons_; }
std::string WeaponArsenal::dump() const {
    std::ostringstream out;
    out << "WEAPON_ARSENAL phase=component owner_id=" << ownerId_
        << " owner_name=" << ownerName_ << " weapons=" << weapons_.size()
        << " owner_registration=parent_owned fire_supported=false\n";
    for (const auto& w : weapons_) {
        out << "WEAPON name=" << w.definition.name << " type=" << w.definition.type
            << " ammo=" << w.ammo << " clip=" << w.clipShotsRemaining
            << " charge_remaining=" << w.chargeTimeRemaining
            << " cooldown_remaining=" << w.cooldownTimeRemaining
            << " reload_remaining=" << w.reloadTimeRemaining
            << " clip_regen_time=" << w.nextClipRegenTime
            << " charging=" << w.charging << " projectile_valid=" << w.validProjectileData
            << " spawned_projectile_valid=" << w.validSpawnedProjectileData
            << " controllable=" << w.controllable << " bundled=" << w.bundled
            << " effect=" << w.definition.effectName << " effect_type=" << w.definition.effectType
            << " effect_trigger=" << w.definition.effectTrigger
            << " effect_execution_supported=false unresolved_fields=" << w.definition.unresolvedFields.size() << '\n';
    }
    return out.str();
}
} // namespace hades
