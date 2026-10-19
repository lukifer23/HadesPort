#include "../host/file_assets.hpp"
#include "../app/actor_marker.hpp"
#include <cmath>
#include <iostream>
class Assets:public hades::AssetStore{
 hades::FileAssets actors_,markers_;
public:Assets(const char* actors,const char* markers):actors_(actors),markers_(markers){}
 std::unique_ptr<hades::AssetFile> open(std::string_view name)override{return name=="actor-markers.bin"?markers_.open(name):actors_.open(name);}
};
int main(int argc,char** argv){try{
 if(argc!=3)throw std::runtime_error("actor_marker_smoke ACTIONS MARKERS");Assets assets(argv[1],argv[2]);hades::ActorAnimationBank actors;actors.load(assets);hades::ActorMarkerBank markers;markers.load(assets,actors);uint64_t sampled=0;
 for(size_t c=0;c<actors.clips().size();++c)for(const auto& slide:actors.clips()[c].slides)for(uint32_t angle=0;angle<actors.clips()[c].originalAngles;++angle){const auto point=markers.sample(c,slide.originalFrame,angle,"head_TRACKING");if(!std::isfinite(point.x)||!point.width)throw std::runtime_error("Marker sample invalid");++sampled;}
 const auto idle=actors.find("ZagreusIdle").value();const auto point=markers.sample(idle,1,0,"head_TRACKING");if(point.x!=.007531481795012951f||point.y!=.11454276740550995f||point.width!=666||point.height!=666)throw std::runtime_error("Exact original Idle first marker mismatch");
 const auto source=markers.sourceTransform(idle);hades::ActorMarkerTransform t;t.resolved=true;t.locationX=100;t.locationY=200;t.nativeAnimationScale=source.ownScale*.7f;t.ownerBodyScale=.7f;t.animationOffsetX=source.offsetX;t.animationOffsetY=source.offsetY;t.attachedThingZ=50;t.shakeX=2;t.shakeY=3;
 const auto location=markers.location(point,t);const auto dash=actors.find("ZagreusSwordDashAttack").value();const auto absent=markers.sample(dash,1,31,"head_TRACKING");if(absent.x||absent.y)throw std::runtime_error("Source partial marker zero branch");
 if(actors.originalAngleForFacing(idle,0)!=0||actors.selectedAngleForFacing(idle,0)!=0)throw std::runtime_error("Full original facing API changed selected angle");
 std::cout<<"{\"result\":\"passed\",\"clips\":"<<actors.clips().size()<<",\"full_angle_head_samples\":"<<sampled<<",\"original_idle_first\":["<<point.x<<','<<point.y<<"],\"diagnostic_transformed\":["<<location[0]<<','<<location[1]<<"],\"source_partial_zero_branch\":true,\"game_marker_runtime_observed\":false}\n";
 }catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}}
