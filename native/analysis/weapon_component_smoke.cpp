#include "weapon_runtime.hpp"
#include "weapon_catalog.hpp"
#include <fstream>
#include <iterator>
#include <iostream>
#include <limits>
#include <stdexcept>
using hades::WeaponArsenal;
using hades::WeaponDefinition;
static void require(bool value,const char* text) {
    if(!value)throw std::runtime_error(text);
}
static WeaponDefinition recovered;
static WeaponDefinition first() {return recovered;}
int main(int argc,char** argv) {
    if(argc!=2){std::cerr<<"Usage: hades-weapons-debug weapons.bin\n";return 64;}
    std::string error;
    std::ifstream input(argv[1],std::ios::binary);require(bool(input),"Missing actual source catalog");
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    std::vector<WeaponDefinition> definitions;
    require(hades::loadWeaponCatalog(bytes,definitions,error)&&definitions.size()==1,"Original catalog failed");
    recovered=definitions[0];const auto unknownCount=recovered.unresolvedFields.size();
    require(unknownCount>=7,"Unknown native fields were lost");
    const auto malformed=[&](std::vector<uint8_t> packet){require(!hades::loadWeaponCatalog(packet,definitions,error),"Malformed catalog accepted");require(definitions.size()==1&&definitions[0].name==recovered.name,"Rejected load replaced catalog");};
    malformed(std::vector<uint8_t>(bytes.begin(),bytes.end()-1));
    auto badTarget=bytes;badTarget[68]^=1;malformed(badTarget);
    auto trailing=bytes;trailing.push_back(0);malformed(trailing);
    WeaponArsenal hero(40000,"_PlayerUnit"), independent(60000,"_PlayerUnit");
    auto source=first();
    require(hero.equip(source,error)&&error.empty(),"First initialization failed");
    auto values=hero.snapshot();require(values.size()==1,"Expected one actual weapon state");
    const auto& w=values[0];
    require(w.ownerId==40000&&w.ownerName=="_PlayerUnit","Incorrect owner");
    require(w.ammo==-1&&w.clipShotsRemaining==0,"Incorrect ammo/clip");
    require(w.chargeTimeRemaining==0&&w.cooldownTimeRemaining==0&&
            w.reloadTimeRemaining==0&&w.nextClipRegenTime==0,"Incorrect timer state");
    require(!w.charging&&!w.controllable&&!w.validProjectileData&&
            !w.validSpawnedProjectileData,"Incorrect control/projectile state");
    source.unresolvedFields.clear();
    require(hero.snapshot()[0].definition.unresolvedFields.size()==unknownCount,"Source copy not retained");
    auto duplicate=first();duplicate.type=99;duplicate.reloadOnCreation=true;
    require(hero.equip(duplicate,error)&&hero.snapshot().size()==1,"Duplicate reinitialized or validated");
    require(hero.snapshot()[0].definition.type==0,"Duplicate replaced original state");
    require(independent.snapshot().empty(),"Owner state leaked");
    require(independent.equip(first(),error)&&independent.snapshot()[0].ownerId==60000,"Independent owner failed");
    require(hero.hasAmmo("ArmorBreakAttack",error)&&hero.spendAmmo("ArmorBreakAttack",error)&&
            hero.snapshot()[0].ammo==-1,"Native nondepleting ammo sentinel changed");
    require(!hero.hasAmmo("Missing",error)&&!error.empty(),"Missing weapon silently accepted");
    WeaponArsenal rejects(70000,"_PlayerUnit");
    auto bad=first();bad.controls[0]="Attack";
    require(!rejects.equip(bad,error)&&rejects.snapshot().empty(),"Control branch accepted");
    bad=first();bad.projectile="PlayerAttack";
    require(!rejects.equip(bad,error)&&rejects.snapshot().empty(),"Projectile branch accepted");
    bad=first();bad.reloadOnEquip=true;
    require(!rejects.equip(bad,error)&&rejects.snapshot().empty(),"Reload branch accepted");
    bad=first();bad.chargeTime=std::numeric_limits<float>::quiet_NaN();
    require(!rejects.equip(bad,error)&&rejects.snapshot().empty(),"Nonfinite timer accepted");
    bad=first();bad.effectContainerResolved=false;
    require(!rejects.equip(bad,error)&&rejects.snapshot().empty(),"Unresolved effect container accepted");
    std::cout<<hero.dump();
    std::cout<<"WEAPON_COMPONENT_DEBUG_OK actual_source_catalog=true malformed_catalog_rejected=true original_first_branch=true duplicate_idempotent=true "
                "owners_independent=true unsupported_transactional=true "
                "world_activation=false firing=false\n";
}
