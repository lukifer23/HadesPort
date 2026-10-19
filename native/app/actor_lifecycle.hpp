#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace hades {
// Ordered real provider boundaries in ObstacleManager::Dispose ->
// Thing::Dispose() -> virtual Obstacle::Dispose(true). Every stage is delivered:
// an absent component is established by its actual owning registry, not guessed.
enum class ActorDisposalStage : uint8_t {
    AttachedAnimationsPreDispose, OwnerDeathAnimations, DrawGridRemove, LifeDispose,
    ClearGridRecord, RemoveAutoLockableAndUseable, StopAmbientSound,
    AudioOwnerDeath, ReleasePrimaryAnimation, ClearAnimationCollections,
    RemoveLight, ResetComponents, WorldOnDisposed,
    ObstacleTail, FreeRemainingComponents, RetireManagerHandle, RetireEntity
};
const char* actorDisposalStageName(ActorDisposalStage);
struct ActorDisposalEvent {
    int32_t id=0;
    uint64_t lifetime=0; // Original portable lifetime, retained after entity clear.
    ActorDisposalStage stage{};
    bool hasLife=false,hasText=false,hasPhysics=false,hasBody=true;
    bool actorAnimationActive=false;
    uint64_t primaryAnimationLifetime=0,primaryEffectAnimation=0;
    int32_t structuralParent=0,attachedParent=0;
    uint32_t structuralChildCount=0,attachedChildCount=0;
};
using ActorDisposalCallback=std::function<bool(const ActorDisposalEvent&,std::string&)>;
}
