#include "../app/unit_status.hpp"
#include <cassert>
#include <iostream>
using namespace hades;
static auto effect(uint64_t h,const char* name){auto e=std::make_shared<UnitStatusEffect>();e->handle=h;e->unitId=40000;e->name=name;return e;}
int main(){
    UnitStatus status;std::string error;assert(status.initializeFreshUnit(error));auto first=effect(1,"first"),second=effect(2,"second"),later=effect(3,"later");
    assert(status.trackAdmittedEffect(first,error));assert(status.trackAdmittedEffect(second,error));
    std::vector<uint64_t> updated,disposed;float ownerTime=0.5f;
    auto inputs=[&](const UnitStatusEffect&,float& f,float& t,std::string&){f=0;t=ownerTime;return true;};
    auto update=[&](UnitStatusEffect& e,float dt,bool& expired,std::string&){updated.push_back(e.handle);
        if(e.handle==1){assert(dt==0.5f);ownerTime=1;assert(status.trackAdmittedEffect(later,error));expired=true;e.cleared=true;
            // Cleanup is suppressed during actual effect iteration.
            assert(status.cleanupEffects({},error));assert(status.snapshot().active.size()==2);
        }else{assert(e.handle==2&&dt==0.25f);expired=false;}return true;};
    auto dispose=[&](UnitStatusEffect& e,std::string&){assert(e.unitId==-1);disposed.push_back(e.handle);return true;};
    assert(status.advanceEffects(0.25f,inputs,update,dispose,error));assert((updated==std::vector<uint64_t>{1,2}));assert((disposed==std::vector<uint64_t>{1}));
    assert((status.snapshot().active==std::vector<uint64_t>{2}));assert((status.snapshot().pending==std::vector<uint64_t>{3}));
    updated.clear();auto updateNext=[&](UnitStatusEffect& e,float,bool& expired,std::string&){updated.push_back(e.handle);expired=false;return true;};
    assert(status.advanceEffects(0.25f,inputs,updateNext,dispose,error));assert((updated==std::vector<uint64_t>{2,3}));
    UnitStatus failed;assert(failed.initializeFreshUnit(error));assert(failed.trackAdmittedEffect(effect(4,"real-callback"),error));
    assert(!failed.advanceEffects(0.25f,inputs,{},dispose,error));assert(failed.snapshot().faulted);
    std::cout<<"native UnitStatus effect phase capture/scaling/cleanup smoke passed\n";
}
