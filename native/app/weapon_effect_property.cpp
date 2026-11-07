#include "weapon_runtime.hpp"
#include "primitive_properties.hpp"
#include "effect_name_hash.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <string_view>
#include <type_traits>
#include <utility>

namespace hades {
namespace {
// Matching DLL d98546ea...254: EffectData reflection b29d0, typed setter
// 8c6c0, Weapon::SetEffectProperty84890. No source-only property aliases.
enum class EffectField { Boolean, Integer, Float, String, Type, Trigger, ChangeType, Cue };
struct EffectProperty { const char* dataKey; EffectField kind; };
const std::map<std::string,EffectProperty> effectProperties{
    {"Active",{"Active",EffectField::Boolean}},
    {"Amount",{"Amount",EffectField::Float}},
    {"AngleCoverage",{"AngleCoverage",EffectField::Float}},
    {"AngleCoverageOffset",{"AngleCoverageOffset",EffectField::Float}},
    {"AngleOffset",{"AngleOffset",EffectField::Float}},
    {"BackFx",{"BackFx",EffectField::String}},
    {"BlinkRangeMultiplier",{"BlinkRangeMultiplier",EffectField::Float}},
    {"BlockDurationOnExpire",{"BlockDurationOnExpire",EffectField::Float}},
    {"BoostMaxStacks",{"BoostMaxStacks",EffectField::Integer}},
    {"BoostMaxStacksByName",{"BoostMaxStacksByName",EffectField::String}},
    {"CanAffectInvulnerable",{"CanAffectInvulnerable",EffectField::Boolean}},
    {"CancelAnimationOnClear",{"CancelAnimationOnClear",EffectField::String}},
    {"Cancelable",{"Cancelable",EffectField::Boolean}},
    {"ClearAnimation",{"ClearAnimation",EffectField::String}},
    {"ClearFx",{"ClearFx",EffectField::String}},
    {"ClearOnAttack",{"ClearOnAttack",EffectField::Boolean}},
    {"ClearOnCollision",{"ClearOnCollision",EffectField::Boolean}},
    {"ClearOnCritted",{"ClearOnCritted",EffectField::Boolean}},
    {"ClearOnDamageInflictedInstances",{"ClearOnDamageInflictedInstances",EffectField::Integer}},
    {"ClearOnSourceDeath",{"ClearOnSourceDeath",EffectField::Boolean}},
    {"ClearSound",{"ClearSound",EffectField::Cue}},
    {"ConsumedFx",{"ConsumedFx",EffectField::String}},
    {"Cooldown",{"Cooldown",EffectField::Float}},
    {"CritAddition",{"CritAddition",EffectField::Float}},
    {"CritChance",{"CritChance",EffectField::Float}},
    {"CritMultiplierVulnerabilityAddition",{"CritMultiplierVulnerabilityAddition",EffectField::Float}},
    {"CritVulnerabilityAddition",{"CritVulnerabilityAddition",EffectField::Float}},
    {"CritVulnerabilityMultiplier",{"CritVulnerabilityMultiplier",EffectField::Float}},
    {"CriticalHitMultiplier",{"CriticalHitMultiplier",EffectField::Float}},
    {"DamageFx",{"DamageFx",EffectField::String}},
    {"DamageOnExpire",{"DamageOnExpire",EffectField::Boolean}},
    {"Deflect",{"Deflect",EffectField::Boolean}},
    {"DestroyProjectileOnStart",{"DestroyProjectileOnStart",EffectField::Boolean}},
    {"DisableAttack",{"DisableAttack",EffectField::Boolean}},
    {"DisableDodge",{"DisableDodge",EffectField::Boolean}},
    {"DisableMove",{"DisableMove",EffectField::Boolean}},
    {"DisableRotate",{"DisableRotate",EffectField::Boolean}},
    {"DodgeChance",{"DodgeChance",EffectField::Float}},
    {"Duration",{"Duration",EffectField::Float}},
    {"DurationFrames",{"DurationFrames",EffectField::Integer}},
    {"ElapsedTimeMultiplier",{"ElapsedTimeMultiplier",EffectField::Float}},
    {"EnableWeaponForDuration",{"EnableWeaponForDuration",EffectField::String}},
    {"ExpiringFrontFx",{"ExpiringFrontFx",EffectField::String}},
    {"ExpiringModifierFalloff",{"ExpiringModifierFalloff",EffectField::Float}},
    {"ExpiringRapidFlashThreshold",{"ExpiringRapidFlashThreshold",EffectField::Float}},
    {"ExpiringTimeThreshold",{"ExpiringTimeThreshold",EffectField::Float}},
    {"ExtendDurationOnReapply",{"ExtendDurationOnReapply",EffectField::Boolean}},
    {"FlashBackFxWhenExpiring",{"FlashBackFxWhenExpiring",EffectField::Boolean}},
    {"FlashDuration",{"FlashDuration",EffectField::Float}},
    {"FlashFrontFxWhenExpiring",{"FlashFrontFxWhenExpiring",EffectField::Boolean}},
    {"FlashSpeed",{"FlashSpeed",EffectField::Float}},
    {"FrontFx",{"FrontFx",EffectField::String}},
    {"HaltOnEnd",{"HaltOnEnd",EffectField::Boolean}},
    {"HaltOnStart",{"HaltOnStart",EffectField::Boolean}},
    {"IdleOnClear",{"IdleOnClear",EffectField::Boolean}},
    {"IgnoreExactName",{"IgnoreExactName",EffectField::String}},
    {"IgnoreName",{"IgnoreName",EffectField::String}},
    {"ImmuneToForce",{"ImmuneToForce",EffectField::Boolean}},
    {"IndependentStackCooldown",{"IndependentStackCooldown",EffectField::Boolean}},
    {"InitialDelay",{"InitialDelay",EffectField::Float}},
    {"IsVulnerabilityEffect",{"IsVulnerabilityEffect",EffectField::Boolean}},
    {"JumpRange",{"JumpRange",EffectField::Integer}},
    {"LerpTime",{"LerpTime",EffectField::Float}},
    {"MaxStacks",{"MaxStacks",EffectField::Integer}},
    {"MaxTriggerDamage",{"MaxTriggerDamage",EffectField::Float}},
    {"MinSpeed",{"MinSpeed",EffectField::Float}},
    {"MinTriggerDamage",{"MinTriggerDamage",EffectField::Float}},
    {"Modifier",{"Modifier",EffectField::Float}},
    {"Name",{"Name",EffectField::String}},
    {"NoUnitCollision",{"NoUnitCollision",EffectField::Boolean}},
    {"NumJumps",{"NumJumps",EffectField::Integer}},
    {"OnlyAffectName",{"OnlyAffectName",EffectField::String}},
    {"OverheadFx",{"OverheadFx",EffectField::String}},
    {"PlayStunGraphic",{"PlayStunGraphic",EffectField::Boolean}},
    {"ProcChance",{"ProcChance",EffectField::Float}},
    {"Range",{"Range",EffectField::Float}},
    {"Rate",{"Rate",EffectField::Float}},
    {"ReapplyFx",{"ReapplyFx",EffectField::String}},
    {"ReflectAddition",{"ReflectAddition",EffectField::Float}},
    {"RefreshOnExpireWithEffect",{"RefreshOnExpireWithEffect",EffectField::String}},
    {"RefreshOnExpireWithEffectAlt",{"RefreshOnExpireWithEffectAlt",EffectField::String}},
    {"RefreshOnExpireWithEffectAltMultiplier",{"RefreshOnExpireWithEffectAltMultiplier",EffectField::Float}},
    {"RefreshOnExpireWithEffectMultiplier",{"RefreshOnExpireWithEffectMultiplier",EffectField::Float}},
    {"RequestTriggerLock",{"RequestTriggerLock",EffectField::Boolean}},
    {"RequireExistingEffect",{"RequireExistingEffect",EffectField::String}},
    {"RequirePerfectCharge",{"RequirePerfectCharge",EffectField::Boolean}},
    {"RequiresAttachedLua",{"RequiresAttachedLua",EffectField::Boolean}},
    {"ResetCooldownOnStack",{"ResetCooldownOnStack",EffectField::Boolean}},
    {"RotationMultiplier",{"RotationMultiplier",EffectField::Float}},
    {"ScaleFxToProjectileRadius",{"ScaleFxToProjectileRadius",EffectField::Boolean}},
    {"SilentImpact",{"SilentImpact",EffectField::Boolean}},
    {"Sound",{"Sound",EffectField::Cue}},
    {"SpeedCap",{"SpeedCap",EffectField::Float}},
    {"StackAmount",{"StackAmount",EffectField::Float}},
    {"Stacks",{"Stacks",EffectField::Boolean}},
    {"StartAnimation",{"StartAnimation",EffectField::String}},
    {"StartFx",{"StartFx",EffectField::String}},
    {"StopSoundOnEnd",{"StopSoundOnEnd",EffectField::Boolean}},
    {"StopSoundOnFinishFade",{"StopSoundOnFinishFade",EffectField::Float}},
    {"TagName",{"TagName",EffectField::String}},
    {"TimeModifierFraction",{"TimeModifierFraction",EffectField::Float}},
    {"Trigger",{"Trigger",EffectField::Trigger}},
    {"Type",{"Type",EffectField::Type}},
    {"UnitPropertyName",{"UnitPropertyName",EffectField::String}},
    {"m_changeType",{"ChangeType",EffectField::ChangeType}},
};
bool sameAscii(std::string_view a,std::string_view b){
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i){auto x=uint8_t(a[i]),y=uint8_t(b[i]);if(x>='a'&&x<='z')x-=32;if(y>='a'&&y<='z')y-=32;if(x!=y)return false;}return true;
}
bool nativeEnumText(const PropertyInput& input,std::string& out,std::string& error){
    if(const auto* value=std::get_if<std::string>(&input)){out=*value;return true;}
    if(std::holds_alternative<std::monostate>(input)){out.clear();return true;}
    if(const auto* value=std::get_if<bool>(&input)){out=*value?"true":"false";return true;}
    const auto* value=std::get_if<float>(&input);
    if(!value){error="Effect enum conversion requires actual Lua scalar boxing";return false;}
    char buffer[64];
    // Native ConvertAnyToString2023d0 uses CVTTSS2SI, yielding INT_MIN
    // outside signed32. The formatted fractional comparison uses that result.
    const double integer=std::trunc(double(*value));
    const int32_t truncated=integer>=INT32_MIN&&integer<=INT32_MAX?int32_t(integer):INT32_MIN;
    if(*value!=float(truncated))std::snprintf(buffer,sizeof buffer,"%g",double(*value));
    else std::snprintf(buffer,sizeof buffer,"%d",int(truncated));out=buffer;return true;
}
int32_t effectEnum(EffectField kind,const std::string& value){
    // Native adfd0: unknown Type spelling defaults SPEED0, while the literal
    // UNKNOWN maps to12. adf30: Fire1/FireEnd2/Charging3, other spelling0.
    if(kind==EffectField::Type){
        constexpr std::array<std::string_view,13> names{"SPEED","GRIP","DAMAGE_OUTPUT","DAMAGE_TAKEN","DAMAGE_OVER_TIME","DAMAGE_OVER_DISTANCE","DELAYED_KNOCKBACK","CHARM","WEAPON_FIRE","PROJECTILE_DEFENSE","TAG","INVULNERABLE","UNKNOWN"};
        for(size_t i=0;i<names.size();++i)if(sameAscii(value,names[i]))return int32_t(i);return 0;
    }
    if(kind==EffectField::Trigger){if(sameAscii(value,"Fire"))return 1;if(sameAscii(value,"FireEnd"))return 2;if(sameAscii(value,"Charging"))return 3;return 0;}
    // Setter8c919..98a accepts m_changeType; all non-MULTIPLY text gives ADD0.
    return sameAscii(value,"MULTIPLY")?1:0;
}
bool mutateEffect(WeaponEffectDefinition& effect,const std::string& property,
        const PropertyInput& input,WeaponValueChange change,const WeaponEffectCueResolver& resolveCue,
        bool& changed,std::string& error){
    const auto descriptor=effectProperties.find(property);
    // GetPropertyInfo returns UNKNOWN0; SetProperty returns false and its
    // caller ignores that bool. Proven unknown keys have no native write.
    if(descriptor==effectProperties.end())return true;
    if(change==WeaponValueChange::MultiplyBase){
        // EffectData vtable+28 returns category0. Setter8c73b..83c passes a
        // null base to GetMultipliedBaseDataValue, which dereferences it.
        error="Effect MultiplyBase reaches the native null-base lookup; unsupported execution domain";return false;
    }
    const auto& field=descriptor->second;
    if(field.kind==EffectField::Cue){
        if(effect.fieldTypes.find(field.dataKey)==effect.fieldTypes.end()||effect.fieldTypes.at(field.dataKey)!="sound_cue"||!effect.cues.contains(field.dataKey)){
            error="Reflected EffectData sound cue is not projected: "+property;return false;
        }
        WeaponSoundCue next;
        if(std::holds_alternative<std::monostate>(input))next={"/EmptyCue",false,true};
        else if(const auto* name=std::get_if<std::string>(&input)){
            // Nil/literal null selects INVALID_CUE(/EmptyCue); empty string
            // instead assigns default SoundCue(empty name,invalid).
            if(name->empty())next={"",false,true};
            else{
                if(!resolveCue){error="Effect sound cue mutation requires actual audio namespace lookup: "+*name;return false;}
                if(!resolveCue(*name,next,error))return false;
                if(!next.validityKnown){error="Effect cue resolver did not establish native validity";return false;}
            }
        }else{error="Effect sound cue setter requires text or empty any";return false;}
        effect.cues.at(field.dataKey)=std::move(next);changed=true;
    }else{
        auto value=effect.fields.find(field.dataKey);const auto type=effect.fieldTypes.find(field.dataKey);
        if(value==effect.fields.end()||type==effect.fieldTypes.end()){error="Reflected EffectData field is not projected: "+property;return false;}
        if(field.kind==EffectField::Type||field.kind==EffectField::Trigger||field.kind==EffectField::ChangeType){
            if(type->second!="int32"||!std::holds_alternative<double>(value->second)){error="Effect enum DTO type mismatch: "+property;return false;}
            std::string text;if(!nativeEnumText(input,text,error))return false;const auto result=effectEnum(field.kind,text);
            value->second=double(result);if(field.kind==EffectField::Type)effect.type=result;else if(field.kind==EffectField::Trigger)effect.trigger=result;changed=true;
        }else{
            PropertyPrimitive current;
            if(field.kind==EffectField::Boolean&&type->second=="bool"&&std::holds_alternative<bool>(value->second))current=std::get<bool>(value->second);
            else if(field.kind==EffectField::String&&type->second=="string"&&std::holds_alternative<std::string>(value->second))current=std::get<std::string>(value->second);
            else if(field.kind==EffectField::Float&&type->second=="float32"&&std::holds_alternative<double>(value->second)){
                const double number=std::get<double>(value->second);if(!std::isfinite(number)||std::abs(number)>std::numeric_limits<float>::max()){error="Effect float DTO outside finite native subset";return false;}current=float(number);
            }else if(field.kind==EffectField::Integer&&type->second=="int32"&&std::holds_alternative<double>(value->second)){
                const double number=std::get<double>(value->second);if(!std::isfinite(number)||number<INT32_MIN||number>INT32_MAX||number!=std::trunc(number)){error="Effect int DTO outside signed32";return false;}current=int32_t(number);
            }else{error="Reflected EffectData type/DTO mismatch: "+property;return false;}
            PropertyPrimitive result;const auto status=mutatePrimitiveProperty(current,input,static_cast<PropertyChange>(change),result,error);
            if(status==PropertyMutation::Applied){
                std::visit([&](const auto& number){using T=std::decay_t<decltype(number)>;if constexpr(std::is_same_v<T,float>||std::is_same_v<T,int32_t>)value->second=double(number);else value->second=number;},result);
                if(property=="Name")effect.name=std::get<std::string>(value->second);changed=true;
            }else if(status==PropertyMutation::InvalidValue)return false;
            else{
                // Native rejected type or numeric ReplaceIfNull: return false
                // from reflection, ignored by Weapon. This is an observed
                // rejection, not a missing implementation of the field.
                error.clear();
            }
        }
    }
    // Weapon848967..49fd repeats this after each reflection call, even a
    // rejected write. Name mutation deliberately leaves nativeNameId unchanged.
    if(property=="RefreshOnExpireWithEffect"||property=="RefreshOnExpireWithEffectAlt"){
        const auto value=effect.fields.find(property);
        if(value==effect.fields.end()||!std::holds_alternative<std::string>(value->second)){error="Effect refresh ID string unavailable";return false;}
        uint64_t hash=0;if(!nativeEffectNameHash(std::get<std::string>(value->second),hash,error))return false;
        (property=="RefreshOnExpireWithEffect"?effect.nativeRefreshId:effect.nativeRefreshAltId)=hash;changed=true;
    }
    return true;
}
}

bool WeaponArsenal::setScriptEffectProperty(const std::string& weaponName,const std::string& effectName,
        const std::string& property,std::optional<WeaponScalar> requested,WeaponValueChange change,
        std::string& error,const WeaponEffectCueResolver& resolveCue){
    error.clear();auto* weapon=scriptMutableWeapon(weaponName);if(!weapon){error="No owned weapon named "+weaponName;return false;}
    const auto fail=[&](){error="SetEffectProperty WeaponName="+weaponName+" EffectName="+effectName+" Property="+property+": "+error;return false;};
    if(static_cast<unsigned>(change)>4){error="Unknown native effect ValueChangeType";return fail();}
    if(weaponName.size()>65536||effectName.size()>65536||property.size()>65536||weaponName.find('\0')!=std::string::npos||effectName.find('\0')!=std::string::npos||property.find('\0')!=std::string::npos){error="Effect property name bound";return fail();}
    if(requested&&std::holds_alternative<std::string>(*requested)&&std::get<std::string>(*requested)=="null")requested.reset();
    SourceValue source;if(requested)std::visit([&](const auto& value){source.value=value;},*requested);
    PropertyInput input;if(!propertyInputFromSource(source,input,error))return fail();
    if(const auto* text=std::get_if<std::string>(&input);text&&text->size()>65536){error="Effect property text bound";return fail();}
    // Stage all mutable copies. Source JSON remains immutable provenance; live
    // effect consumers use fields/cues/enum sidecars, and data identity is the
    // preserved per-source vector index. No insertion, callbacks or reapply.
    auto effects=weapon->definition.effects;auto primary=weapon->projectileData;auto spawned=weapon->spawnedProjectileData;
    std::vector<bool> weaponWrites(effects.size()),primaryWrites(primary.effects.size()),spawnedWrites(spawned.effects.size());
    const auto apply=[&](std::vector<WeaponEffectDefinition>& values,std::vector<bool>& writes){
        for(size_t i=0;i<values.size();++i)if(values[i].name==effectName){bool changed=false;if(!mutateEffect(values[i],property,input,change,resolveCue,changed,error))return false;if(changed)writes[i]=true;}return true;
    };
    if(!apply(effects,weaponWrites))return fail();
    if(weapon->validProjectileData){
        if(!apply(primary.effects,primaryWrites))return fail();
        //848b80 and848d1d are independent passes over the SAME spawned copy.
        // SpawnOnDeath[2] does not select another pass; do not deduplicate.
        if(weapon->validSpawnedProjectileData){
            if(!primary.spawned[0].empty()&&!apply(spawned.effects,spawnedWrites))return fail();
            if(!primary.spawned[1].empty()&&!apply(spawned.effects,spawnedWrites))return fail();
        }
    }
    const auto any=[](const auto& writes){return std::any_of(writes.begin(),writes.end(),[](bool value){return value;});};
    const bool primaryChanged=any(primaryWrites),spawnedChanged=any(spawnedWrites);
    auto frozenPrimary=primaryChanged?std::make_shared<const ProjectileDefinition>(primary):weapon->projectileSource;
    auto frozenSpawned=spawnedChanged?std::make_shared<const ProjectileDefinition>(spawned):weapon->spawnedProjectileSource;
    // Keep each mutable EffectData record at its existing vector address; a
    // source callback can hold a reference while calling this action. Only
    // touched records change. Future projectiles get the newly frozen copies.
    const auto commit=[](auto& destination,auto& staged,const auto& writes){for(size_t i=0;i<writes.size();++i)if(writes[i])destination[i]=std::move(staged[i]);};
    commit(weapon->definition.effects,effects,weaponWrites);
    if(primaryChanged){commit(weapon->projectileData.effects,primary.effects,primaryWrites);weapon->projectileSource=std::move(frozenPrimary);}
    if(spawnedChanged){commit(weapon->spawnedProjectileData.effects,spawned.effects,spawnedWrites);weapon->spawnedProjectileSource=std::move(frozenSpawned);}
    error.clear();return true;
}
}
