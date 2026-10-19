#pragma once
#include "actor_animation.hpp"
#include <map>

namespace hades {
struct ActorMarkerPoint {float x=0,y=0;uint32_t width=0,height=0;};
struct ActorMarkerSourceTransform {float ownScale=1,offsetX=0,offsetY=0;};
struct ActorMarkerTransform {
    // All values are actual native state, including owner/body transitions and
    // ShakeOffset. The marker returns native Vector2 coordinates, not ground XYZ.
    bool resolved=false;
    float locationX=0,locationY=0,attachedThingZ=0,drawAngle=0;
    float nativeAnimationScale=1,ownerBodyScale=1,animationOffsetX=0,animationOffsetY=0;
    float shakeX=0,shakeY=0;
};
class ActorMarkerBank {
    struct Track {uint32_t first=0,last=0,width=0,height=0;std::vector<std::array<float,2>> points;};
    struct DataSet {std::map<std::string,Track> tracks;};
    struct Clip {std::string name;uint32_t dataSet=0,frames=0,angles=0;ActorMarkerSourceTransform source;};
    std::vector<DataSet> data_;
    std::vector<Clip> clips_;
public:
    void load(AssetStore&,const ActorAnimationBank&,const char* file="actor-markers.bin");
    void reset(){data_.clear();clips_.clear();}
    ActorMarkerPoint sample(size_t clip,uint32_t originalFrame,uint32_t fullOriginalAngle,std::string_view marker)const;
    // AnimationFrameDataSet::GetDataForMarker9eef0 result presence for a clip.
    bool hasMarker(size_t clip,std::string_view marker)const;
    ActorMarkerSourceTransform sourceTransform(size_t clip)const{return clips_.at(clip).source;}
    // Owner scale includes the original unit0.7 exactly once plus live body
    // transitions. Own animation multiplier is separate actual live state.
    // Marker dimensions are original units: do not use HAA1 clip.worldScale.
    void applySourceTransform(size_t clip,float actualOwnerBodyScale,float actualOwnAnimationMultiplier,ActorMarkerTransform&)const;
    static std::array<float,2> location(const ActorMarkerPoint&,const ActorMarkerTransform&);
};
}
