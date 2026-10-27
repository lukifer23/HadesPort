#pragma once
#include "source_value.hpp"
#include "primitive_properties.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace hades {
struct InteractionKey {
    int32_t id=-1; uint64_t lifetime=0;
    bool operator==(const InteractionKey&)const=default;
    bool operator<(const InteractionKey& b)const{return id<b.id||(id==b.id&&lifetime<b.lifetime);}
};
struct InteractionRect {int32_t x=0,y=0,width=0,height=0;};
struct InteractionGrid {int32_t originX=0,originY=0,columns=0,rows=0;};
// Fully populated from current native components; none of these are actor
// defaults. location is actual attachment-aware Thing.GetLocation, z is
// Thing.GetZLocation, area is virtual Thing.GetArea. firstGroupVisible includes
// successful GroupManager lookup of first GroupName (type name if no groups).
struct InteractionOwnerFrame {
    InteractionKey key;
    std::string typeName;
    float x=0,y=0,z=0,angle=0,upwardVelocity=0;
    uint8_t alpha=0,bodyFlips=0;
    bool present=false,hasBody=false,hasPhysics=false,primaryFlipped=false;
    bool attachedToLiveEntity=false,firstGroupVisible=false;
    InteractionRect area;
    std::vector<InteractionRect> textAreas;
};
struct InteractionUserFrame {
    InteractionKey key;
    float x=0,y=0,totalZ=0,maxUseZ=0,useReach=0;
    bool alive=false,disposed=false,hasPrimary=false,hasPhysics=false;
    bool disableOwnerInteract=false,disableOwnerManualInteract=false;
    bool airborneAny=false,playerControlled=false;
    int32_t playerIndex=0; // Native zero-based index; Interact.PlayerIndex is one-based.
};
struct InteractionClock {float time=0;int32_t frame=0;};
struct InteractionSnapshot {
    InteractionKey key;
    SourceValue definition; // Every known compiled field plus original overrides.
    bool enabled=false,toggleState=false,hasBeenUsed=false,previousUseable=true;
    int32_t remainingUses=0,lastUseFrame=0;
    float lastUseTime=0,repeatTimer=0;
    bool dirty=false,global=false;
};
struct InteractionProviders {
    std::function<bool(InteractionKey,InteractionOwnerFrame&,std::string&)> owner;
    std::function<bool(const InteractionUserFrame&,const std::string&,bool&,std::string&)> hasAttached;
    // Returns weapon existence separately: missing weapon fails admission.
    std::function<bool(const InteractionUserFrame&,const std::string&,bool& exists,bool& hasAmmo,std::string&)> weaponAmmo;
    // Native RequiresTouch uses actual projected target polygon vs user polygon.
    std::function<bool(const InteractionOwnerFrame&,const InteractionUserFrame&,bool&,std::string&)> touches;
};
enum class InteractionUseKind {
    UserAnimation,UserPlanarForce,UserUpwardForce,UserFaceTarget,UserFx,
    OnHeal,AttachOwnerToUser,OnUsed,OwnerAnimation,VisualFx,UseSound,
    UsedFrontFx,UsedBackFx,DestroyOwner,UserFireWeapon,UnuseableAnimation
};
struct InteractionUseOperation {
    InteractionUseKind kind=InteractionUseKind::OnUsed;
    InteractionKey owner,user;
    SourceValue value; // Original string/SoundCue/number for this operation.
    std::string sourceTypeName; // Actual owner.GetTypeName for OnHeal.
    float x=0,y=0,angle=0;
    // All animation operations use SetAnimation(name,false,true,false).
    // UserFx attaches created FX; UsedFront/BackFx use source relative draw call.
    // UserFireWeapon must do actual owned lookup + virtual CanFire, then native
    // RequestFire(angle,auto=false,target=user,applySelfEffects=false,from=ownerXY).
};
class InteractionUseExecution;
class InteractionWorld {
    struct Record {InteractionSnapshot state;};
    std::map<InteractionKey,std::shared_ptr<Record>> records_;
    std::vector<InteractionKey> order_;
    using Cell=std::pair<int64_t,int64_t>;
    std::map<Cell,std::vector<InteractionKey>> cells_;
    InteractionGrid grid_;
    bool gridReady_=false,sparseGrid_=false;
    friend class InteractionUseExecution;
public:
    // Generated from matched-PDB constructor/assignment proof; all 76 semantic
    // fields. Native storage padding/hash functors are not fabricated.
    static SourceValue compiledDefaults();
    static bool resolveDefinition(const SourceValue& original,SourceValue&,std::string&);
    bool configureGrid(InteractionGrid,std::string&);
    // Android adaptation: sparse256-unit cells, no unavailable Windows mArea
    // clipping. Bounds/range/admission remain original; traversal is stable.
    void configureSparseGrid();
    // Call only after actual Thing/Interact construction; query metadata proxies
    // are not interaction owners. AddUseable queues non-global entries.
    bool add(InteractionKey,const SourceValue& original,InteractionClock,std::string&);
    bool contains(InteractionKey key) const{return records_.contains(key);}
    bool remove(InteractionKey,std::string&);
    bool markForUpdate(InteractionKey,std::string&);
    // Actual UseableManager phase, after ScriptManager. Refresh is atomic on
    // missing provider/unsupported bounds. Stable insertion order is a portable
    // adaptation; native pointer-hash traversal/tie order is not recovered.
    bool update(const InteractionProviders&,std::string&);
    bool setEnabled(InteractionKey,bool,std::string&);
    bool replaceDefinition(InteractionKey,const SourceValue& original,std::string&);
    // ScriptAction::SetInteractProperty 0x180273550 mutates owned InteractData,
    // creating a default Interact first if absent. Constructor-copied runtime
    // state (remainingUses/enabled/cooldown) is not reset by later data writes.
    bool setProperty(InteractionKey,const std::string&,const PropertyInput&,
        PropertyChange,InteractionClock,std::string&);
    bool isUseable(InteractionKey,const InteractionOwnerFrame&,InteractionClock,bool&,std::string&)const;
    static bool userCanUse(const InteractionUserFrame&);
    bool isUseableBy(InteractionKey,const InteractionOwnerFrame&,const InteractionUserFrame&,
        InteractionClock,bool useMouse,const InteractionProviders&,bool&,std::string&)const;
    // Only source normal player options {0,1,0,1,0,angle=-999999}; the native
    // cursor/parallax/angular selection paths are deliberately not conflated.
    bool selectNormal(const InteractionUserFrame&,InteractionClock,bool useMouse,
        const InteractionProviders&,InteractionKey& selected,std::string&)const;
    // PlayerUnitInput.HandleInput's manual-Use branch for the cached target.
    // Caller first enforces the real outer EditingMode/Unit.Enabled/input gates.
    // Uses pressed flag0x4 (Control.Use Index5); accepted consumes this input
    // phase before weapon controls. Native does not re-run IsUseableBy here.
    bool manualRequest(InteractionKey selected,const InteractionUserFrame&,
        bool useMouse,uint32_t useControlFlags,bool& accepted,std::string&)const;
    bool useLocation(InteractionKey,const InteractionOwnerFrame&,float& x,float& y,std::string&)const;
    bool snapshot(InteractionKey,InteractionSnapshot&,std::string&)const;
    std::vector<InteractionSnapshot> snapshots()const;
    std::string dump()const;
};
// A synchronous source Use call split at real external consumers. next() yields
// one operation; parent must execute it, then acknowledge(). Unsupported work
// cannot be acknowledged as success. No callback, audio, heal or destruction is
// simulated here. Callback mutations are observed on subsequent next() calls.
class InteractionUseExecution {
    std::shared_ptr<InteractionWorld::Record> record_;
    InteractionKey user_;
    unsigned stage_=0;
    bool done_=false,waiting_=false;
    InteractionUseOperation pending_;
public:
    bool begin(InteractionWorld&,InteractionKey owner,InteractionKey user,InteractionClock,std::string&);
    bool next(const InteractionProviders&,
        const std::function<bool(InteractionKey,InteractionUserFrame&,std::string&)>& userFrame,
        InteractionUseOperation&,bool& done,std::string&);
    bool acknowledge(std::string&);
    bool done()const{return done_;}
};
}
