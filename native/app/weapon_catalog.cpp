#include "weapon_catalog.hpp"
#include "effect_name_hash.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <stdexcept>
#include <cstdlib>
#include <optional>

namespace hades {
namespace {
struct Reader {
    std::span<const uint8_t> bytes;size_t at=0;
    template<class T>T take(){if(sizeof(T)>bytes.size()-at)throw std::runtime_error("Truncated weapon initialization catalog");T v;std::memcpy(&v,bytes.data()+at,sizeof(T));at+=sizeof(T);return v;}
    std::string text(){const auto n=take<uint32_t>();if(n>1048576||n>bytes.size()-at)throw std::runtime_error("Weapon catalog string outside bounds");std::string s(reinterpret_cast<const char*>(bytes.data()+at),n);at+=n;if(s.find('\0')!=std::string::npos)throw std::runtime_error("Embedded NUL in weapon catalog");return s;}
    bool flag(){const auto v=take<uint8_t>();if(v>1)throw std::runtime_error("Invalid weapon catalog flag");return v!=0;}
    float scalar(){const auto v=take<float>();if(!std::isfinite(v))throw std::runtime_error("Nonfinite weapon initialization value");return v;}
};
// Bounded JSON projection for the export's per-field typed records. Complex
// objects remain in effectiveFieldsJson; they are never coerced to defaults.
struct ScalarProjection {
    const std::string& s;size_t at=0;unsigned depth=0;
    void ws(){while(at<s.size()&&(s[at]==' '||s[at]=='\n'||s[at]=='\r'||s[at]=='\t'))++at;}
    void need(char c){ws();if(at==s.size()||s[at++]!=c)throw std::runtime_error("Invalid effective-field JSON");}
    std::string text(){need('"');std::string out;while(at<s.size()){char c=s[at++];if(c=='"')return out;if(static_cast<unsigned char>(c)<32)throw std::runtime_error("Invalid JSON text");if(c=='\\'){if(at==s.size())break;c=s[at++];switch(c){case '"':case '\\':case '/':out+=c;break;case 'b':out+='\b';break;case 'f':out+='\f';break;case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;case 'u':{if(s.size()-at<4)throw std::runtime_error("Invalid Unicode escape");unsigned cp=0;for(int i=0;i<4;++i){char h=s[at++];unsigned n=h>='0'&&h<='9'?h-'0':h>='a'&&h<='f'?h-'a'+10:h>='A'&&h<='F'?h-'A'+10:99;if(n>15)throw std::runtime_error("Invalid Unicode escape");cp=cp*16+n;}if(cp>=0xd800&&cp<=0xdfff)throw std::runtime_error("Unsupported surrogate in scalar text");if(cp<0x80)out+=char(cp);else if(cp<0x800){out+=char(0xc0|(cp>>6));out+=char(0x80|(cp&63));}else{out+=char(0xe0|(cp>>12));out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}break;}default:throw std::runtime_error("Invalid JSON escape");}}else out+=c;}throw std::runtime_error("Unterminated JSON text");}
    std::optional<WeaponScalar> value(){ws();if(at==s.size()||++depth>64)throw std::runtime_error("Effective JSON depth/bounds");std::optional<WeaponScalar> result;char c=s[at];
        if(c=='"')result=text();
        else if(c=='{'||c=='['){const bool object=c=='{';++at;ws();if(at<s.size()&&s[at]==(object?'}':']'))++at;else for(;;){if(object){text();need(':');}value();ws();if(at==s.size())throw std::runtime_error("Truncated JSON container");char end=s[at++];if(end==(object?'}':']'))break;if(end!=',')throw std::runtime_error("Invalid JSON container");}}
        else if(s.compare(at,4,"true")==0){at+=4;result=true;}else if(s.compare(at,5,"false")==0){at+=5;result=false;}else if(s.compare(at,4,"null")==0)at+=4;
        else{const auto start=at;while(at<s.size()&&((s[at]>='0'&&s[at]<='9')||s[at]=='-'||s[at]=='+'||s[at]=='.'||s[at]=='e'||s[at]=='E'))++at;if(at==start)throw std::runtime_error("Invalid JSON scalar");const auto token=s.substr(start,at-start);char* end=nullptr;double n=std::strtod(token.c_str(),&end);if(end!=token.c_str()+token.size()||!std::isfinite(n))throw std::runtime_error("Invalid numeric scalar");result=n;}
        --depth;return result;
    }
    std::string propertyJson(const std::string& selected,bool optional=false){need('{');std::string out;ws();if(at<s.size()&&s[at]=='}')return out;for(;;){auto key=text();need(':');ws();const size_t begin=at;value();if(key==selected)out=s.substr(begin,at-begin);ws();if(at==s.size())throw std::runtime_error("Truncated JSON record");char end=s[at++];if(end=='}')break;if(end!=',')throw std::runtime_error("Invalid JSON record");}ws();if(at!=s.size())throw std::runtime_error("Trailing JSON record");if(out.empty()&&!optional)throw std::runtime_error("Missing projected JSON property");return out;}
    std::vector<std::string> arrayRecords(){std::vector<std::string> out;need('[');ws();if(at<s.size()&&s[at]==']'){++at;return out;}for(;;){ws();const size_t begin=at;value();out.push_back(s.substr(begin,at-begin));if(out.size()>1024)throw std::runtime_error("Too many normalized projectile effects");ws();if(at==s.size())throw std::runtime_error("Truncated effect array");const char end=s[at++];if(end==']')break;if(end!=',')throw std::runtime_error("Invalid effect array");}ws();if(at!=s.size())throw std::runtime_error("Trailing effect array");return out;}
    std::map<std::string,WeaponScalar> fields(std::map<std::string,std::string>* types=nullptr){std::map<std::string,WeaponScalar> out;need('{');ws();if(at<s.size()&&s[at]=='}'){++at;return out;}for(;;){auto key=text();need(':');need('{');std::optional<WeaponScalar> projected;std::string fieldType;ws();if(at<s.size()&&s[at]=='}')++at;else for(;;){auto member=text();need(':');auto v=value();if(member=="value")projected=std::move(v);else if(member=="type"&&v&&std::holds_alternative<std::string>(*v))fieldType=std::get<std::string>(*v);ws();if(at==s.size())throw std::runtime_error("Truncated field object");char end=s[at++];if(end=='}')break;if(end!=',')throw std::runtime_error("Invalid field object");}if(types)(*types)[key]=fieldType;if(projected&&!out.emplace(std::move(key),std::move(*projected)).second)throw std::runtime_error("Duplicate field scalar");ws();if(at==s.size())throw std::runtime_error("Truncated field table");char end=s[at++];if(end=='}')break;if(end!=',')throw std::runtime_error("Invalid field table");}ws();if(at!=s.size())throw std::runtime_error("Trailing effective JSON");return out;}
};

void projectEffectRecord(WeaponEffectDefinition& e){
e.effectiveFieldsJson=ScalarProjection{e.sourceJson}.propertyJson("effective_fields",true);
if(!e.effectiveFieldsJson.empty()){
    e.fields=ScalarProjection{e.effectiveFieldsJson}.fields(&e.fieldTypes);
    for(const char* cueName:{"Sound","ClearSound"}){
        const auto record=ScalarProjection{e.effectiveFieldsJson}.propertyJson(cueName);
        const auto cueJson=ScalarProjection{record}.propertyJson("value");
        if(ScalarProjection{cueJson}.propertyJson("owner")!="null")throw std::runtime_error("Effect cue pointer must remain null in source DTO");
        const auto n=ScalarProjection{ScalarProjection{cueJson}.propertyJson("name")}.value();
        if(!n||!std::holds_alternative<std::string>(*n))throw std::runtime_error("Invalid normalized effect cue name");
        WeaponSoundCue cue;cue.name=std::get<std::string>(*n);
        const auto v=ScalarProjection{ScalarProjection{cueJson}.propertyJson("valid")}.value();
        if(v){if(!std::holds_alternative<bool>(*v))throw std::runtime_error("Invalid effect cue validity");cue.valid=std::get<bool>(*v);cue.validityKnown=true;}
        e.cues.emplace(cueName,std::move(cue));
    }
    const auto colorRecord=ScalarProjection{e.effectiveFieldsJson}.propertyJson("FlashColor");
    const auto color=ScalarProjection{colorRecord}.propertyJson("value");uint32_t packed=0;
    unsigned shift=0;for(const char* key:{"r","g","b","a"}){
        const auto channel=ScalarProjection{ScalarProjection{color}.propertyJson(key)}.value();
        if(!channel||!std::holds_alternative<double>(*channel))throw std::runtime_error("Invalid native effect color");
        const auto value=std::get<double>(*channel);if(value<0||value>255||std::floor(value)!=value)throw std::runtime_error("Invalid effect color byte");
        packed|=static_cast<uint32_t>(value)<<shift;shift+=8;
    }e.flashColorRgba=packed;
    const auto original=ScalarProjection{e.sourceJson}.propertyJson("source_fields",true);
    if(!original.empty()){
        auto identifier=[&](const char* key,uint64_t& out){
            if(ScalarProjection{original}.propertyJson(key,true).empty()){out=0;return;}
            const auto field=e.fields.find(key);if(field==e.fields.end()||!std::holds_alternative<std::string>(field->second))throw std::runtime_error("Effect native ID has no source string");
            std::string detail;if(!nativeEffectNameHash(std::get<std::string>(field->second),out,detail))throw std::runtime_error(detail);
        };
        identifier("Name",e.nativeNameId);identifier("RefreshOnExpireWithEffect",e.nativeRefreshId);identifier("RefreshOnExpireWithEffectAlt",e.nativeRefreshAltId);e.nativeIdsKnown=true;
    }
}
}
std::vector<WeaponEffectDefinition> projectileEffects(const std::string& json){std::vector<WeaponEffectDefinition> result;if(json.empty())return result;
    for(const auto& record:ScalarProjection{json}.arrayRecords()){WeaponEffectDefinition e;e.sourceJson=record;
        auto text=ScalarProjection{ScalarProjection{record}.propertyJson("name")}.value();if(!text||!std::holds_alternative<std::string>(*text))throw std::runtime_error("Projectile effect name missing");e.name=std::get<std::string>(*text);
        auto integer=[&](const char* key){auto v=ScalarProjection{ScalarProjection{record}.propertyJson(key)}.value();if(!v||!std::holds_alternative<double>(*v))throw std::runtime_error("Projectile effect native enum missing");const auto n=std::get<double>(*v);if(n<INT32_MIN||n>INT32_MAX||std::floor(n)!=n)throw std::runtime_error("Projectile effect native enum invalid");return int32_t(n);};
        e.type=integer("type");e.trigger=integer("trigger");projectEffectRecord(e);result.push_back(std::move(e));
    }return result;
}
void projectileCues(ProjectileDefinition& p,const std::string& fieldsJson){
    const auto empty=ScalarProjection{p.effectiveJson}.propertyJson("remove_effects_known_empty",true);
    if(!empty.empty()){const auto v=ScalarProjection{empty}.value();if(!v||!std::holds_alternative<bool>(*v))throw std::runtime_error("Invalid projectile RemoveEffects proof flag");p.removeEffectsKnownEmpty=std::get<bool>(*v);}
    for(const char* key:{"ImpactSound","InvincibleImpactSound","InflictedDamageSound","DetonateSound"}){
        const auto type=p.fieldTypes.find(key);if(type==p.fieldTypes.end()||type->second!="sound_cue")continue;
        const auto record=ScalarProjection{fieldsJson}.propertyJson(key);const auto json=ScalarProjection{record}.propertyJson("value");
        if(ScalarProjection{json}.propertyJson("owner")!="null")throw std::runtime_error("Projectile source cue owner must remain null");
        const auto name=ScalarProjection{ScalarProjection{json}.propertyJson("name")}.value();
        if(!name||!std::holds_alternative<std::string>(*name))throw std::runtime_error("Invalid projectile cue name");
        WeaponSoundCue cue;cue.name=std::get<std::string>(*name);
        const auto valid=ScalarProjection{ScalarProjection{json}.propertyJson("valid")}.value();
        if(valid){if(!std::holds_alternative<bool>(*valid))throw std::runtime_error("Invalid projectile cue validity");cue.valid=std::get<bool>(*valid);cue.validityKnown=true;}
        p.cues.emplace(key,std::move(cue));
    }
}

}
bool loadWeaponCatalog(std::span<const uint8_t> bytes,WeaponCatalog& output,std::string& error){
    try {
        if(bytes.size()<104||bytes.size()>16*1024*1024||(std::memcmp(bytes.data(),"HWA1",4)&&std::memcmp(bytes.data(),"HWA2",4)&&std::memcmp(bytes.data(),"HWA3",4)))throw std::runtime_error("Invalid weapon initialization catalog");
        constexpr uint8_t target[]{0xd9,0x85,0x46,0xea,0x54,0x37,0x19,0x10,0xea,0x44,0x99,0x21,0x64,0x53,0x99,0x4d,0x78,0xe9,0xb4,0xe2,0x3b,0x33,0x81,0xe4,0x57,0xf8,0xa1,0x64,0x8a,0x32,0xe2,0x54};
        if(!std::equal(std::begin(target),std::end(target),bytes.begin()+68))throw std::runtime_error("Weapon catalog native target mismatch");
        const bool extended=bytes[3]!='1'; WeaponCatalog catalog; Reader r{bytes,100};const auto count=r.take<uint32_t>();if(!count||count>4096)throw std::runtime_error("Weapon definition count outside bounds");
        std::vector<WeaponDefinition> parsed;std::set<std::string> names;
        for(uint32_t i=0;i<count;++i){
            WeaponDefinition d;d.name=r.text();d.projectile=r.text();d.effectName=r.text();
            d.type=r.take<int32_t>();d.startingAmmo=r.take<int32_t>();d.maxAmmo=r.take<int32_t>();d.clipSize=r.take<int32_t>();d.effectType=r.take<int32_t>();d.effectTrigger=r.take<int32_t>();
            d.chargeTime=r.scalar();d.clipRegenInterval=r.scalar();d.initialCooldown=r.scalar();
            d.reloadOnCreation=r.flag();d.reloadOnEquip=r.flag();d.defaultControl=r.flag();d.effectContainerResolved=r.flag();
            for(auto& v:d.controls)v=r.text();for(auto& v:d.bundled)v=r.text();
            const auto unresolved=r.take<uint32_t>();if(unresolved>1024)throw std::runtime_error("Too many unresolved weapon fields");
            for(uint32_t k=0;k<unresolved;++k)d.unresolvedFields.push_back(r.text());
            if(extended){
                d.controlWindow=r.scalar();d.cooldown=r.scalar();d.cooldownMin=r.scalar();d.cooldownMax=r.scalar();
                d.reloadTime=r.scalar();d.reloadTimeMin=r.scalar();d.reloadTimeMax=r.scalar();d.ignoreForceCooldown=r.flag();
                d.reloadFinishGraphic=r.text();d.swapOnReloadFinish=r.text();d.shareAmmoWeapon=r.text();d.effectiveFieldsJson=r.text();d.fields=ScalarProjection{d.effectiveFieldsJson}.fields(&d.fieldTypes);
                for(const char* cueName:{"FireSound","ReloadSound","ChargeSound","ChargeFullSound","OnWeaponChargingSound","ChargeFinishSound"}){
                    // Older HWA2 packets legitimately retain unresolved cue
                    // defaults; only new typed sound_cue records are projected.
                    if(d.effectiveFieldsJson.find(std::string("\"")+cueName+"\"")==std::string::npos)continue;
                    const auto record=ScalarProjection{d.effectiveFieldsJson}.propertyJson(cueName);
                    const auto kindJson=ScalarProjection{record}.propertyJson("type");
                    const auto kind=ScalarProjection{kindJson}.value();
                    if(!kind||!std::holds_alternative<std::string>(*kind)||std::get<std::string>(*kind)!="sound_cue")continue;
                    const auto cueJson=ScalarProjection{record}.propertyJson("value");
                    if(ScalarProjection{cueJson}.propertyJson("owner")!="null")throw std::runtime_error("Unexpected serialized native cue pointer");
                    const auto nameJson=ScalarProjection{cueJson}.propertyJson("name");const auto cueValue=ScalarProjection{nameJson}.value();
                    if(!cueValue||!std::holds_alternative<std::string>(*cueValue))throw std::runtime_error("Invalid normalized cue name");
                    WeaponSoundCue cue;cue.name=std::get<std::string>(*cueValue);
                    const auto validJson=ScalarProjection{cueJson}.propertyJson("valid");const auto valid=ScalarProjection{validJson}.value();
                    if(valid){if(!std::holds_alternative<bool>(*valid))throw std::runtime_error("Invalid cue validity");cue.valid=std::get<bool>(*valid);cue.validityKnown=true;}
                    d.cues.emplace(cueName,std::move(cue));
                }
                const auto effects=r.take<uint32_t>();if(effects>1024)throw std::runtime_error("Too many weapon effects");
                for(uint32_t k=0;k<effects;++k){WeaponEffectDefinition e;e.name=r.text();e.type=r.take<int32_t>();e.trigger=r.take<int32_t>();e.sourceJson=r.text();
                    projectEffectRecord(e);
                    d.effects.push_back(std::move(e));}
            }
            if(d.name.empty()||!names.insert(d.name).second)throw std::runtime_error("Empty or duplicate weapon definition");
            parsed.push_back(std::move(d));
        }
        catalog.weapons=std::move(parsed);
        if(extended){
            const auto pc=r.take<uint32_t>();if(pc>4096)throw std::runtime_error("Too many projectiles");names.clear();
            for(uint32_t k=0;k<pc;++k){ProjectileDefinition p;p.name=r.text();p.spawnType=r.take<int32_t>();for(auto& n:p.spawned)n=r.text();p.effectiveJson=r.text();const auto fieldsJson=ScalarProjection{p.effectiveJson}.propertyJson("effective_fields");p.fields=ScalarProjection{fieldsJson}.fields(&p.fieldTypes);projectileCues(p,fieldsJson);p.effectsJson=ScalarProjection{p.effectiveJson}.propertyJson("effects",true);p.effects=projectileEffects(p.effectsJson);if(p.name.empty()||!names.insert(p.name).second)throw std::runtime_error("Duplicate/empty projectile");catalog.projectiles.push_back(std::move(p));}
            const auto cc=r.take<uint32_t>();if(cc>256)throw std::runtime_error("Too many controls");names.clear();
            for(uint32_t k=0;k<cc;++k){WeaponControlDefinition c;c.name=r.text();c.nativeIndex=r.take<int32_t>();if(c.name.empty()||c.nativeIndex<0||!names.insert(c.name).second)throw std::runtime_error("Invalid control identity");catalog.controls.push_back(std::move(c));}
            const auto lc=r.take<uint32_t>();if(lc>4096)throw std::runtime_error("Too many unit loadouts");names.clear();
            for(uint32_t k=0;k<lc;++k){UnitWeaponLoadout l;l.unitName=r.text();if(l.unitName.empty()||!names.insert(l.unitName).second)throw std::runtime_error("Duplicate/empty unit loadout");const auto sc=r.take<uint32_t>();if(sc>7)throw std::runtime_error("Invalid native startup slot count");for(uint32_t j=0;j<sc;++j){auto slot=r.text();auto name=r.text();l.explicitInitialSlots.emplace_back(std::move(slot),std::move(name));}catalog.unitLoadouts.push_back(std::move(l));}
        }
        if(bytes[3]=='3'){for(auto* missing:{&catalog.knownMissingWeapons,&catalog.knownMissingProjectiles}){const auto n=r.take<uint32_t>();if(n>4096)throw std::runtime_error("Too many absent names");missing->clear();for(uint32_t k=0;k<n;++k)missing->push_back(r.text());}}
        if(r.at!=bytes.size())throw std::runtime_error("Trailing weapon catalog bytes");
        output=std::move(catalog);error.clear();return true;
    }catch(const std::exception& failure){error=failure.what();return false;}
}
const WeaponDefinition* WeaponCatalog::weapon(const std::string& name)const{for(const auto& d:weapons)if(d.name==name)return &d;return nullptr;}
const ProjectileDefinition* WeaponCatalog::projectile(const std::string& name)const{for(const auto& d:projectiles)if(d.name==name)return &d;return nullptr;}
bool WeaponCatalog::missingWeapon(const std::string& name)const{return std::find(knownMissingWeapons.begin(),knownMissingWeapons.end(),name)!=knownMissingWeapons.end();}
bool WeaponCatalog::missingProjectile(const std::string& name)const{return std::find(knownMissingProjectiles.begin(),knownMissingProjectiles.end(),name)!=knownMissingProjectiles.end();}
int32_t WeaponCatalog::controlIndex(const std::string& name)const{for(const auto& d:controls)if(d.name==name)return d.nativeIndex;return -1;}
bool loadWeaponCatalog(std::span<const uint8_t> bytes,std::vector<WeaponDefinition>& output,std::string& error){WeaponCatalog catalog;if(!loadWeaponCatalog(bytes,catalog,error))return false;output=std::move(catalog.weapons);return true;}
}

namespace hades {
namespace {
struct EffectProjection {
    const WeaponEffectDefinition& source;std::string& error;
    bool scalar(const char* key,float& v)const{auto i=source.fields.find(key);if(i==source.fields.end()||!std::holds_alternative<double>(i->second)){error=std::string("Missing normalized effect number ")+key;return false;}v=static_cast<float>(std::get<double>(i->second));if(!std::isfinite(v)){error="Nonfinite normalized effect field";return false;}return true;}
    bool integer(const char* key,int32_t& v)const{auto i=source.fields.find(key);if(i==source.fields.end()||!std::holds_alternative<double>(i->second)){error=std::string("Missing normalized effect integer ")+key;return false;}const double n=std::get<double>(i->second);if(!std::isfinite(n)||n<INT32_MIN||n>INT32_MAX||std::floor(n)!=n){error="Invalid normalized effect integer";return false;}v=static_cast<int32_t>(n);return true;}
    bool flag(const char* key,bool& v)const{auto i=source.fields.find(key);if(i==source.fields.end()||!std::holds_alternative<bool>(i->second)){error=std::string("Missing normalized effect bool ")+key;return false;}v=std::get<bool>(i->second);return true;}
    bool text(const char* key,std::string& v)const{auto i=source.fields.find(key);if(i==source.fields.end()||!std::holds_alternative<std::string>(i->second)){error=std::string("Missing normalized effect text ")+key;return false;}v=std::get<std::string>(i->second);return true;}
};
}
bool projectWeaponEffectAdmission(const WeaponEffectDefinition& source,uint64_t identity,
        WeaponSelfEffect& output,std::string& error){
    error.clear();if(!identity){error="Effect admission requires actual nonzero data identity";return false;}
    WeaponSelfEffect next;next.dataHandle=identity;EffectProjection read{source,error};
    if(!read.text("Name",next.name)||!read.integer("Trigger",next.trigger)||!read.scalar("ProcChance",next.procChance)||
       !read.flag("Active",next.active)||!read.flag("RequirePerfectCharge",next.requirePerfectCharge))return false;
    if(next.name.empty()){error="Effect admission data has no real source name";return false;}
    output=std::move(next);return true;
}
bool projectWeaponEffectRuntimeData(const WeaponEffectDefinition& source,uint64_t identity,
        WeaponEffectRuntimeData& output,std::string& error){
    error.clear();if(!identity){error="Effect factory requires actual nonzero mutable data identity";return false;}
    WeaponEffectRuntimeData next;EffectProjection read{source,error};
    if(!read.integer("Type",next.nativeType))return false;
    // EffectFactory::CreateEffect5e200 jump table (RVA5e310): 0 SpeedEffect;
    // 4 DoT, 5 DamageOverDistance, 6 DelayedKnockback, 7 Charm, 8 WeaponFire,
    // 9 ProjectileDefense, 11 Invulnerable have own classes; 1 GRIP,
    // 2 DAMAGE_OUTPUT, 3 DAMAGE_TAKEN, 10 TAG and every value >11 take the
    // TagEffect::createEffect60f00 default case. Only Speed/Tag are reconstructed.
    const bool tagFactory=next.nativeType==1||next.nativeType==2||next.nativeType==3||next.nativeType==10||next.nativeType>11;
    // 11 InvulnerableEffect::createEffect5e380: pooled base Effect (pData +0x20, source +0x38),
    // no captured Cooldown; AffectUnit5e580/Clear5e7c0 are the parent's type-11 hooks.
    if(next.nativeType!=0&&next.nativeType!=11&&!tagFactory){error="Native admitted derived effect factory not implemented for Type "+std::to_string(next.nativeType);return false;}
    auto& base=next.base;base.dataHandle=identity;base.flashColor=source.flashColorRgba;
    if(!read.scalar("Duration",base.duration)||!read.scalar("ElapsedTimeMultiplier",base.elapsedTimeMultiplier)||
       !read.scalar("FlashDuration",base.flashDuration)||!read.scalar("FlashSpeed",base.flashSpeed)||
       !read.flag("HaltOnStart",base.haltOnStart)||!read.flag("DestroyProjectileOnStart",base.destroyProjectileOnStart)||
       !read.flag("ScaleFxToProjectileRadius",base.scaleFxToProjectileRadius)||!read.flag("DisableAttack",base.disableAttack)||
       !read.text("EnableWeaponForDuration",base.enableWeaponForDuration)||!read.text("StartAnimation",base.startAnimation)||
       !read.text("StartFx",base.startFx)||!read.text("FrontFx",base.frontFx)||!read.text("OverheadFx",base.overheadFx)||
       !read.text("BackFx",base.backFx)||!read.text("UnitPropertyName",base.unitPropertyName))return false;
    auto cue=source.cues.find("Sound");if(cue==source.cues.end()){error="Native effect Sound semantic data missing";return false;}base.soundName=cue->second.name;
    if(!read.text("Name",next.statusPrototype.name)||!read.flag("Cancelable",next.statusPrototype.cancelable)||
       !read.flag("DisableAttack",next.statusPrototype.disableAttack)||!read.flag("DisableMove",next.statusPrototype.disableMove)||
       !read.flag("DisableRotate",next.statusPrototype.disableRotate))return false;
    next.update.type=next.nativeType;next.update.duration=base.duration;
    if(!read.scalar("ExpiringTimeThreshold",next.update.expiringTimeThreshold)||
       !read.scalar("ExpiringRapidFlashThreshold",next.update.expiringRapidFlashThreshold)||
       !read.flag("DisableMove",next.update.disableMove)||!read.flag("DisableRotate",next.update.disableRotate)||
       !read.flag("DisableAttack",next.update.disableAttack)||!read.flag("ClearOnSourceDeath",next.update.clearOnSourceDeath)||
       !read.flag("FlashFrontFxWhenExpiring",next.update.flashFrontFxWhenExpiring)||
       !read.flag("FlashBackFxWhenExpiring",next.update.flashBackFxWhenExpiring))return false;
    if(next.nativeType==0){
        SpeedEffectData speed;static_cast<EffectAffectData&>(speed)=base;
        if(!read.scalar("Modifier",speed.modifier)||!read.scalar("ExpiringModifierFalloff",speed.expiringModifierFalloff)||
           !read.scalar("ExpiringTimeThreshold",speed.expiringTimeThreshold)||!read.integer("ChangeType",speed.changeType)||
           !read.integer("MaxStacks",speed.maxStacks)||!read.flag("Stacks",speed.stacks))return false;
        next.speed=std::move(speed);
    }else if(next.nativeType!=11&&!read.scalar("Cooldown",next.tagCooldown))return false;
    output=std::move(next);return true;
}
}
