#include "actor_lighting.hpp"
#include <algorithm>
#include <cstddef>
namespace hades {
namespace {
double cross(ActorOccluderPoint a,ActorOccluderPoint b,ActorOccluderPoint c){return (double(b.x)-a.x)*(double(c.y)-a.y)-(double(b.y)-a.y)*(double(c.x)-a.x);}
bool inSegment(ActorOccluderPoint p,ActorOccluderPoint a,ActorOccluderPoint b){return cross(a,b,p)==0&&p.x>=std::min(a.x,b.x)&&p.x<=std::max(a.x,b.x)&&p.y>=std::min(a.y,b.y)&&p.y<=std::max(a.y,b.y);}
}
bool actorOccluderIntersects(const ActorOccluder& occluder,ActorOccluderPoint from,ActorOccluderPoint to){
    if(occluder.polygon.size()<3)return false;
    for(size_t i=0;i<occluder.polygon.size();++i){const auto a=occluder.polygon[i],b=occluder.polygon[(i+1)%occluder.polygon.size()];const double c1=cross(from,to,a),c2=cross(from,to,b),c3=cross(a,b,from),c4=cross(a,b,to);
        if(((c1<0&&c2>0)||(c1>0&&c2<0))&&((c3<0&&c4>0)||(c3>0&&c4<0)))return true;
        if((c1==0&&inSegment(a,from,to))||(c2==0&&inSegment(b,from,to))||(c3==0&&inSegment(from,a,b))||(c4==0&&inSegment(to,a,b)))return true;
    }
    // A complete segment inside a filled occluder also has no light visibility.
    bool inside=false;for(size_t i=0,j=occluder.polygon.size()-1;i<occluder.polygon.size();j=i++){
        const auto a=occluder.polygon[i],b=occluder.polygon[j];
        if((a.y>from.y)!=(b.y>from.y)&&double(from.x)<(double(b.x)-a.x)*(double(from.y)-a.y)/(double(b.y)-a.y)+a.x)inside=!inside;
    }
    return inside;
}
}
