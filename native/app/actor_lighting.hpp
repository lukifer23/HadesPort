#pragma once
#include <array>
#include <cstdint>
#include <vector>
namespace hades {
struct ActorOccluderPoint {float x=0,y=0;};
struct ActorOccluder {
    int32_t ownerId=0;uint64_t ownerLifetime=0,revision=0;
    // Closed original world-space polygon; closing edge is last→first.
    std::vector<ActorOccluderPoint> polygon;
};
// Portable ray/edge light visibility uses actual recovered polygons. This is an
// Android shadowing adapter, not a claim to reproduce the Windows GPU material.
bool actorOccluderIntersects(const ActorOccluder&,ActorOccluderPoint from,ActorOccluderPoint to);
}
