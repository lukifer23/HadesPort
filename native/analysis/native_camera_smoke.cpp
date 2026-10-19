#include "native_camera.hpp"
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace hades;
void need(bool v,const std::string&e){if(!v)throw std::runtime_error(e);}
void near(float actual,float expected){need(std::abs(actual-expected)<.002f,"Native camera numeric mismatch");}
int main(){try{std::string error;std::map<int32_t,NativeCameraTarget>things{{1,{1,101,1947.373f,5188.61f}},{40013,{40013,102,778.0336914f,3076.104004f}},{40014,{40014,103,5471.8125f,6046.5f}}};NativeCameraResolve resolve=[&](int32_t id,NativeCameraTarget&v,std::string&e){const auto i=things.find(id);if(i==things.end()){e="Missing fixture Thing";return false;}v=i->second;return true;};
 NativeCamera camera;need(camera.adjustZoom(.75f,0,error),error);need(camera.setClamps({40013,40014},.75f,resolve,error),error);need(camera.lock({1},0,0,0,0,0,false,false,resolve,error),error);near(camera.state().x,2030.3433f);need(camera.adjustZoom(.5f,.02f,error),error);need(camera.advance(.02f,resolve,error),error);near(camera.state().zoom,.5f);need(camera.adjustZoom(.75f,8,error),error);for(int i=0;i<240;++i)need(camera.advance(1.f/60,resolve,error),error);near(camera.state().zoom,.625f);need(camera.pan({1},10,-1,-1,0,0,false,true,false,[]{return .5;},resolve,error),error);need(camera.state().panX.duration==0,"Same-target pan generated a path");const float before=camera.state().x;things[1].lifetime=999;things[1].x=4000;need(camera.advance(0,resolve,error),error);need(camera.state().x!=4000,"Retired camera target ID was reused");need(!camera.pan({40013},10,-1,-1,0,0,false,true,false,[]{return .5;},resolve,error)&&error.find("no live previous")!=std::string::npos,"Pan from a retired tether target silently acknowledged");
 NativeCamera timed;need(timed.lock({40013},2,0,0,10,-20,false,false,resolve,error),error);need(timed.advance(1,resolve,error),error);near(timed.state().x,(778.0336914f+10)*.5f);near(timed.state().y,(3076.104004f-20)*.5f);need(timed.adjustZoom(.5f,0,error),error);need(timed.focus(2,0,true,"",[]{return 0.;},error),error);need(timed.advance(0,resolve,error),error);near(timed.state().zoom,1);
 things[1]={1,101,1947.373f,5188.61f};things[410007]={410007,104,5177.9267578f,4083.625f};things[410008]={410008,105,1726.3673096f,5012.44140625f};NativeCamera weighted;need(weighted.setZoomWeight({410007},1.25f,1,resolve,error),error);need(weighted.setZoomWeight({410008},1.f,1,resolve,error),error);need(weighted.lock({1},0,0,0,0,0,false,false,resolve,error),error);need(weighted.adjustZoom(.75f,0,error,resolve),error);near(weighted.state().zoom,.763851f);need(weighted.adjustZoom(.5f,.02f,error,resolve),error);need(weighted.advance(.02f,resolve,error),error);near(weighted.state().zoom,.509234f);

 // New-target PanCamera with LeaveRoomPresentation (RoomPresentation.lua:1140) arguments: { Id = heroExitPointId, Duration = 10.0 }.
 things[1]={1,101,1947.373f,5188.61f};things[50]={50,106,2600.f,4700.f};NativeCamera panned;int draws=0;const NativeCameraRandom rng=[&]{++draws;return .5;};
 need(panned.lock({1},0,0,0,0,0,false,false,resolve,error),error);need(panned.pan({50},10,-1,-1,0,0,false,true,false,rng,resolve,error),error);need(draws==8,"SetPan must draw GenerateEase RNG 4x per axis");
 need(panned.state().panX.duration==10&&std::abs(panned.state().panY.duration-8)<1e-5f&&std::abs(panned.state().panY.startTime-.5f)<1e-6f,"SetPan Y helper timing");
 need(panned.advance(0,resolve,error),error);near(panned.state().x,1947.373f);near(panned.state().y,5188.61f);
 for(int i=0;i<24;++i)need(panned.advance(1.f/60,resolve,error),error);near(panned.state().y,5188.61f);need(panned.state().x>1947.373f&&panned.state().x<2600,"Pan X did not start");
 for(int i=0;i<276;++i)need(panned.advance(1.f/60,resolve,error),error);need(panned.state().x>1947.373f&&panned.state().x<2600&&panned.state().y<5188.61f&&panned.state().y>4700,"Pan midpoint outside path");
 for(int i=0;i<330;++i)need(panned.advance(1.f/60,resolve,error),error);near(panned.state().x,2600);near(panned.state().y,4700);need(panned.state().panX.duration==0&&panned.state().panY.duration==0,"Pan helpers not cleared at completion");
 need(panned.advance(1.f/60,resolve,error),error);near(panned.state().x,2600);
 // RoomEntranceStandard (RoomPresentation.lua:435) with the base IntroSequenceDuration 0.1, then LockCamera hero Duration 2.0.
 NativeCamera entry;draws=0;need(entry.lock({1},0,0,0,0,0,false,false,resolve,error),error);need(entry.pan({50},.1f,-1,-1,0,0,false,true,false,rng,resolve,error),error);for(int i=0;i<8;++i)need(entry.advance(1.f/60,resolve,error),error);near(entry.state().x,2600);near(entry.state().y,4700);
 need(entry.lock({1},2,0,0,0,0,false,false,resolve,error),error);for(int i=0;i<121;++i)need(entry.advance(1.f/60,resolve,error),error);near(entry.state().x,1947.373f);
 std::cout<<"{\"result\":\"passed\",\"source_opening_zoom_sequence\":true,\"actual_two_original_clamp_coordinates\":true,\"actual_original_inverse_distance_zoom_targets\":true,\"source_double_smoothstep\":true,\"same_target_pan_early_return\":true,\"lifetime_reuse_rejected\":true,\"new_target_pan_setpan_ease_splines\":true,\"android_observed\":false}\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
