#include "cursor_interaction.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace hades {
namespace {
const SourceValue& field(const SourceValue&d,const char*k){const auto*v=d.get(k);if(!v)throw std::runtime_error(std::string("Cursor interaction source field unavailable: ")+k);return *v;}
bool flag(const SourceValue&d,const char*k){return field(d,k).boolean();}
struct Hit {InteractionKey key;int32_t group;float distance;bool polygon,text;};
bool better(const Hit&a,const Hit&b){return a.group>b.group||(a.group==b.group&&a.distance<b.distance);}
bool tied(const Hit&a,const Hit&b){return a.group==b.group&&a.distance==b.distance;}
}

bool cursorPointInPolygon(const std::vector<std::array<float,2>>&points,float x,float y){
#ifdef __clang__
#pragma clang fp contract(off)
#endif
 // Native initializes the previous vertex to the final source vertex and
 // compares >=Y. Equality of the cross-product comparison with currentAbove
 // flips parity; replacing these tests with a generic <= ray changes edges.
 if(points.empty())return false;
 auto previous=points.back();bool previousAbove=previous[1]>=y,inside=false;
 for(const auto&current:points){
  const bool currentAbove=current[1]>=y;
  if(previousAbove!=currentAbove){
   const float lhs=(previous[0]-current[0])*(current[1]-y);
   const float rhs=(current[0]-x)*(previous[1]-current[1]);
   if((lhs>=rhs)==currentAbove)inside=!inside;
  }
  previous=current;previousAbove=currentAbove;
 }
 return inside;
}
bool cursorPointInTextArea(const InteractionRect&r,float x,float y){
 // Native adds the integer dimensions before conversion to float. Reject
 // malformed overflowing input instead of reproducing signed integer UB.
 const int64_t right=int64_t(r.x)+r.width,bottom=int64_t(r.y)+r.height;
 if(r.width<0||r.height<0||right<INT32_MIN||right>INT32_MAX||bottom<INT32_MIN||bottom>INT32_MAX)throw std::runtime_error("Cursor text area outside native integer bounds");
 return x>=float(r.x)&&x<float(int32_t(right))&&y>=float(r.y)&&y<float(int32_t(bottom));
}
bool selectCursorInteraction(const InteractionWorld&world,const InteractionUserFrame&user,
 InteractionClock clock,bool useMouse,float x,float y,const InteractionProviders&providers,
 const CursorInteractionGeometryProvider&geometry,InteractionKey&selected,std::string&error){try{
 selected={};
 if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(clock.time)){error="Nonfinite actual cursor coordinate/clock";return false;}
 if(!providers.owner||!geometry){error="Actual cursor owner/geometry providers unavailable";return false;}
 if(user.disposed||!InteractionWorld::userCanUse(user)){error.clear();return true;}
 std::vector<Hit>hits;
 for(const auto&snapshot:world.snapshots()){
  const auto&d=snapshot.definition;if(!flag(d,"CursorOnly"))continue;
  InteractionOwnerFrame owner;if(!providers.owner(snapshot.key,owner,error))return false;
  if(!owner.present)continue;
  if(owner.key!=snapshot.key){error="Cursor actual owner lifetime mismatch";return false;}
  if(!owner.firstGroupVisible)continue;
  bool eligible=false;if(!world.isUseableBy(snapshot.key,owner,user,clock,useMouse,providers,eligible,error))return false;if(!eligible)continue;
  CursorInteractionGeometry shape;if(!geometry(snapshot.key,shape,error))return false;if(!shape.screenSpace)continue;
  if(shape.key!=snapshot.key||shape.drawGroup<0){error="Cursor actual screen geometry/group lifetime unavailable";return false;}
  if(!flag(d,"UseExtents")){error="CursorOnly non-extent range selection not reconstructed";return false;}
  if(flag(d,"UseableOffScreen")){error="Global screen useable cursor indexing not reconstructed";return false;}
  if(shape.parallax!=1||owner.z!=0){error="Cursor screen parallax/Z projection not reconstructed";return false;}
  if(shape.polygon.size()<3||shape.polygon.size()>65536){error="Cursor actual screen polygon unavailable/outside bound";return false;}
  for(const auto&p:shape.polygon)if(!std::isfinite(p[0])||!std::isfinite(p[1])){error="Nonfinite actual cursor polygon";return false;}
  bool textHit=false;if(flag(d,"UseTextBoxes"))for(const auto&area:owner.textAreas)textHit=cursorPointInTextArea(area,x,y)||textHit;
  const bool polygonHit=cursorPointInPolygon(shape.polygon,x,y);if(!textHit&&!polygonHit)continue;
  float ux=0,uy=0;if(!world.useLocation(snapshot.key,owner,ux,uy,error))return false;
  const float dx=ux-x,dy=uy-y,distance=dx*dx+dy*dy;if(!std::isfinite(distance)){error="Cursor actual use distance overflow";return false;}
  hits.push_back({snapshot.key,shape.drawGroup,distance,polygonHit,textHit});
 }
 // FindUseable's polygon path gates by higher group, then strictly smaller
 // distance. Its later text-area path is OUTSIDE that priority gate and can
 // overwrite a previous winner. Preserve that distinction; do not silently
 // sort all matches as though text and polygons shared one priority rule.
 const Hit*text=nullptr;for(const auto&h:hits)if(h.text){if(text){error="Cursor overlapping text targets require native pointer-hash traversal order";return false;}text=&h;}
 if(text){for(const auto&h:hits)if(h.key!=text->key&&h.polygon&&better(h,*text)){error="Cursor text/polygon target priority depends on native pointer-hash traversal order";return false;}selected=text->key;error.clear();return true;}
 const Hit*best=nullptr;for(const auto&h:hits)if(h.polygon){if(!best||better(h,*best))best=&h;}
 if(best){for(const auto&h:hits)if(h.key!=best->key&&h.polygon&&tied(h,*best)){error="Cursor equal-priority targets require native pointer-hash traversal order";return false;}selected=best->key;}
 error.clear();return true;
 }catch(const std::exception&e){error=e.what();return false;}}
}
