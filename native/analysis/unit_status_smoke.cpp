#include "unit_status.hpp"
#include <cassert>
#include <iostream>
using namespace hades;
static auto effect(uint64_t id,std::string name,bool cancelable,bool disableAttack=true){auto e=std::make_shared<UnitStatusEffect>();e->handle=id;e->unitId=40000;e->name=std::move(name);e->cancelable=cancelable;e->disableAttack=disableAttack;return e;}
int main(){
    UnitStatus s;std::string error;assert(s.timers.attack==0);assert(s.initializeFreshUnit(error));assert(s.timers.attack==-1);
    assert(s.canMove());s.timers.move=0.25f;assert(!s.canMove());s.timers.move=0;assert(s.canMove());
    UnitStatus movement;assert(movement.initializeFreshUnit(error));auto halt=effect(50,"Halt",true,false);halt->disableMove=true;halt->cleared=true;halt->disposed=true;
    assert(movement.trackAdmittedEffect(halt,error));assert(!movement.canMove());assert(movement.promotePendingEffects(error));assert(!movement.canMove());halt->disableMove=false;assert(movement.canMove());
    assert(s.canAttack(false,false,false));assert(!s.canAttack(true,false,true));s.timers.attack=0.5f;s.timers.attackCancelable=false;
    assert(!s.canAttack(false,false,false));assert(s.canAttack(false,false,true));assert(s.advanceDisableTimers(0.25f,2,error)&&s.timers.attack==0);assert(s.advanceDisableTimers(1,2,error)&&s.timers.attack==0);
    auto external=effect(1,"ExternalStun",false);assert(s.trackAdmittedEffect(external,error));assert(!s.canAttack(false,false,true));assert(s.canAttack(false,true,false));external->selfApplied=true;assert(!s.canAttack(false,true,true));external->cancelable=true;assert(s.canAttack(false,true,true));
    auto own=effect(2,"Own",true),visual=effect(3,"Visual",true,false),during=effect(4,"DuringClear",true);assert(s.trackAdmittedEffect(own,error));assert(s.trackAdmittedEffect(visual,error));std::vector<uint64_t> calls;
    auto clear=[&](UnitStatusEffect& e,std::string&){calls.push_back(e.handle);e.cleared=true;if(e.handle==1)assert(s.trackAdmittedEffect(during,error));return true;};
    assert(s.cancelDisables({"Own"},true,clear,error));assert((calls==std::vector<uint64_t>{1,3}));assert((s.snapshot().toDelete==std::vector<uint64_t>{1,3}));assert(s.snapshot().pending.size()==4);assert(!s.canAttack(false,false,false));
    assert(!s.getEffect("ExternalStun"));assert(s.getEffect("Own")==own);assert(s.getEffect("DuringClear")==during);
    std::vector<uint64_t> disposed;assert(s.cleanupEffects([&](UnitStatusEffect& e,std::string&){assert(e.unitId==-1);disposed.push_back(e.handle);return true;},error));assert((disposed==std::vector<uint64_t>{1,3}));assert((s.snapshot().pending==std::vector<uint64_t>{2,4}));
    assert(s.promotePendingEffects(error));calls.clear();assert(s.cancelDisables({"Own","DuringClear"},false,clear,error)&&calls.empty());assert(s.cancelDisables({"Own","DuringClear"},true,clear,error));assert((calls==std::vector<uint64_t>{2,4}));
    UnitStatus failed;assert(failed.initializeFreshUnit(error));assert(failed.trackAdmittedEffect(effect(9,"RealExecutorRequired",true),error));assert(!failed.cancelDisables({},false,{},error));assert(failed.snapshot().faulted&&!failed.canAttack(false,false,true)&&!failed.canMove());
    std::cout<<"Native UnitStatus query, timer and callback ordering subset passed; synthetic observers only\n";
}
