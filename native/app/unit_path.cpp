#include "unit_path.hpp"
#include <array>
#include <cmath>
#include <map>
#include <stdexcept>
#include <utility>
namespace hades {
namespace {
using Key=std::pair<int32_t,int32_t>;
Key key(const UnitPathNode& n){return {n.x,n.y};}
struct Item {int64_t priority;UnitPathNode node;};
// EASTL push VA1802ead40: equal priority does not rise. CalcPath pop
// VA1802e9db0 selects the right child on equal priority, pulls a hole down,
// then reinserts the final element upward. std::priority_queue tie ordering
// is not part of this recovered contract.
void push(std::vector<Item>& heap,Item value){size_t hole=heap.size();heap.push_back(value);while(hole){const size_t parent=(hole-1)/2;if(heap[parent].priority<=value.priority)break;heap[hole]=heap[parent];hole=parent;}heap[hole]=value;}
Item pop(std::vector<Item>& heap){const Item result=heap.front(),value=heap.back();heap.pop_back();if(heap.empty())return result;size_t hole=0,child=2;while(child<heap.size()){if(heap[child].priority>heap[child-1].priority)--child;heap[hole]=heap[child];hole=child;child=hole*2+2;}if(child==heap.size()){heap[hole]=heap[child-1];hole=child-1;}while(hole){const size_t parent=(hole-1)/2;if(heap[parent].priority<=value.priority)break;heap[hole]=heap[parent];hole=parent;}heap[hole]=value;return result;}
int32_t distance(float x,float y,float gx,float gy){const float dx=x-gx,dy=y-gy;return int32_t(std::sqrt(dx*dx+dy*dy));}
}
bool planUnitPath(float x,float y,float gx,float gy,const UnitPathConfig& c,const std::function<bool(float,float,bool&,std::string&)>& validateNode,std::vector<UnitPathNode>& out,std::string& error){try{
 for(const float v:{x,y,gx,gy,c.nodeDistance,c.nodeSuccessDistance,c.lookAheadDistance,c.lookAheadMultiplier,c.successDistance})if(!std::isfinite(v)||std::abs(v)>1e7f)throw std::runtime_error("Native Unit path numeric bounds");
 if(c.nodeDistance<=0||c.successDistance<=0||c.lookAheadDistance<0||c.lookAheadMultiplier<0||c.nodeCountLimit<1||c.skippedNodeLimit<0)throw std::runtime_error("Native Unit path configuration bounds");
 out.clear();const int32_t initialDistance=distance(x,y,gx,gy),success=int32_t(c.successDistance);
 if(initialDistance<success){error.clear();return true;}
 UnitPathNode start{int32_t(std::round(x)),int32_t(std::round(y)),0,initialDistance,false};
 const Key roundedGoal{int32_t(std::round(gx)),int32_t(std::round(gy))};
 // VA1802e9bdf..c98: native six offsets and rounded Euclidean edge costs.
 const float d=c.nodeDistance;const std::array<std::pair<float,float>,6> offsets{{{2*d,d},{-2*d,d},{2*d,-d},{-2*d,-d},{0,2*d},{0,-2*d}}};
 const int32_t maxCost=int32_t(c.lookAheadMultiplier*c.lookAheadDistance),maxDistance=maxCost+initialDistance;
 std::map<Key,UnitPathNode> predecessor;predecessor.emplace(key(start),start);
 std::vector<Item> heap;push(heap,{int64_t(initialDistance*11/10),start});
 UnitPathNode end;bool found=false;int32_t tested=0;
 while(!heap.empty()){
  auto n=pop(heap).node;++tested;
  if(key(n)==roundedGoal){end=n;found=true;break;}
  if(n.cost>0){bool blocked=false;if(!validateNode||!validateNode(float(n.x),float(n.y),blocked,error))return false;if(blocked)continue;}
  if(n.distance<=success){end=n;found=true;break;}
  // Native default partial paths retain the last popped graph node.
  if(n.distance>maxDistance||n.cost>=maxCost||tested>=c.nodeCountLimit){
   // CalcPath VA1802ea14d..ea4f8: default OnFailBehavior1 selects
   // the current popped node for partial/bounded failure BuildMoveList.
   // FindPath ignores CalcPath's return enum and consumes the actual list.
   end=n;found=true;break;
  }
  if(tested>1000000)throw std::runtime_error("Native Pathfinder portable work bound exceeded");
  for(const auto [ox,oy]:offsets){const int32_t sx=int32_t(std::round(ox)),sy=int32_t(std::round(oy));const int32_t step=int32_t(std::round(std::sqrt(ox*ox+oy*oy)));
   UnitPathNode next{n.x+sx,n.y+sy,n.cost+step,0,false};
   const auto previous=predecessor.find(key(next));
   // AddNode VA1802ea5bd compares candidate cost to stored predecessor.cost
   // (map value+8), then stores the parent node at map value, not candidate.
   if(previous!=predecessor.end()&&next.cost>=previous->second.cost)continue;
   next.distance=distance(float(next.x),float(next.y),gx,gy);predecessor[key(next)]=n;
   push(heap,{int64_t(n.cost+next.distance*11/10),next});
  }
 }
 // CalcPath VA1802ea2a6..2db: exhausted queue clears the move list, return4.
 if(!found){error.clear();return true;}
 // BuildMoveList VA1802eaa10..abd5: walk predecessors backwards, retain turns
 // and at most SkippedNodeLimit consecutive equal-direction omissions.
 int32_t previousDx=0,previousDy=0,skipped=0;
 for(size_t count=0;count<=predecessor.size();++count){const auto i=predecessor.find(key(end));if(i==predecessor.end())throw std::runtime_error("Native path predecessor missing");const auto parent=i->second;if(key(end)==key(parent)){if(out.empty())out.push_back(end);error.clear();return true;}
  const int32_t dx=end.x-parent.x,dy=end.y-parent.y;const float changeX=float(previousDx-dx),changeY=float(previousDy-dy);
  if(skipped<c.skippedNodeLimit&&changeX*changeX+changeY*changeY<=1.f)++skipped;else{out.push_back(end);skipped=0;}
  previousDx=dx;previousDy=dy;end=parent;
 }
 throw std::runtime_error("Native path predecessor cycle");
 }catch(const std::exception& e){error=e.what();return false;}}
}
