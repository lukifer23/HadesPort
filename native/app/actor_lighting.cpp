#include "actor_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace hades {
namespace {
const SourceValue& lightingProperty(const ActorWorld& world,int32_t id,const char* key){const auto* value=world.currentProperty(id,key);if(!value)throw std::runtime_error(std::string("Original actor lighting property unavailable: ")+key);return *value;}
}
bool ActorWorld::initializeOccluder(int32_t id,std::string& error){
    try{
        const auto* a=actor(id);if(!a||!a->registered||!a->object)throw std::runtime_error("Occluder initialization requires actual actor/body");
        const auto* source=a->sourceId?seed(a->sourceId):nullptr;bool attached=false;uint32_t override=0;
        if(source){const auto* parent=source->map.get("AttachToID");const auto* stops=source->map.get("StopsLight");if(!parent||!stops)throw std::runtime_error("Original MapThing occluder admission fields unavailable");attached=parent->number()>0;const double value=stops->number();if(value<0||value>2||value!=std::trunc(value))throw std::runtime_error("Unsupported native MapThing.StopsLight override");override=uint32_t(value);}
        float z=0;if(!zLocation(id,z,error))return false;
        const bool nearZero=std::fabs(z)<=8*std::numeric_limits<float>::denorm_min()||std::fabs(z)<=std::fabs(z)*.0001f;
        const bool permitted=!attached&&nearZero&&override!=1&&(override==2||lightingProperty(*this,id,"Thing.StopsLight").boolean());
        occluderInitialized_[a->entityHandle]=permitted;
        if(!permitted)occluders_.erase(a->entityHandle);
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool ActorWorld::updateOccluder(int32_t id,std::string& error){
    try{
        const auto* a=actor(id);if(!a||!a->registered||!a->object)throw std::runtime_error("Occluder geometry requires actual registered actor");
        // Parent should call initialize at Thing.Init. The first selected
        // update also reconstructs that missed initialization from real state.
        if(!occluderInitialized_.contains(a->entityHandle)&&!initializeOccluder(id,error))return false;
        if(!occluderInitialized_.at(a->entityHandle)){error.clear();return true;}
        ActorOccluder out;out.ownerId=id;out.ownerLifetime=a->entityHandle;
        const auto* custom=currentProperty(id,"Thing.OccluderPoints");
        // Matching ThingDataDef constructor VA1800ebb31/38/3f writes the
        // vector begin/end/capacity0. Absent original field means empty vector.
        if(!custom||custom->array().empty()){
            for(const auto& p:a->geometry)out.polygon.push_back({p.x,p.y});
        }else{
            float x=0,y=0;if(!location(id,x,y,error))return false;
            for(const auto& point:custom->array()){const auto* px=point.get("X"),*py=point.get("Y");if(!px||!py)throw std::runtime_error("Original custom occluder point lacks XY");out.polygon.push_back({float(px->number()),float(py->number())});}
            double area=0;for(size_t i=0;i<out.polygon.size();++i){const auto p=out.polygon[i],q=out.polygon[(i+1)%out.polygon.size()];area+=double(p.x)*q.y-double(q.x)*p.y;}if(area>0)std::reverse(out.polygon.begin(),out.polygon.end());
            const auto* source=a->sourceId?seed(a->sourceId):nullptr;bool flipVertical=false;if(source){const auto* flip=source->map.get("FlipVertical");if(!flip)throw std::runtime_error("Original occluder vertical flip unavailable");flipVertical=flip->boolean();}
            const float scale=a->object->scale*a->object->scaleFraction;
            for(auto& p:out.polygon){p.x=x+p.x*scale;p.y=y+p.y*scale;}
            if(a->object->flipHorizontal){for(auto& p:out.polygon)p.x=x-(p.x-x);std::reverse(out.polygon.begin(),out.polygon.end());}
            if(flipVertical){for(auto& p:out.polygon)p.y=y-(p.y-y);std::reverse(out.polygon.begin(),out.polygon.end());}
            if(lightingProperty(*this,id,"Thing.RotateGeometry").boolean()&&a->object->angle!=0){const float c=std::cos(-a->object->angle),s=std::sin(-a->object->angle);for(auto& p:out.polygon){const float dx=p.x-x,dy=p.y-y;p.x=x+dx*c-dy*s;p.y=y+dx*s+dy*c;}}
        }
        if(out.polygon.size()<3)throw std::runtime_error("Admitted light occluder requires actual original polygon");
        for(const auto& p:out.polygon)if(!std::isfinite(p.x)||!std::isfinite(p.y))throw std::runtime_error("Nonfinite original occluder geometry");
        if(nextOccluderRevision_==UINT64_MAX)throw std::runtime_error("Occluder revision space exhausted");out.revision=nextOccluderRevision_++;
        occluders_[a->entityHandle]=std::move(out);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool ActorWorld::removeOccluder(int32_t id,std::string& error){const auto* a=actor(id);if(!a){error="Occluder removal requires retained actual actor lifetime";return false;}occluders_.erase(a->entityHandle);occluderInitialized_.erase(a->entityHandle);error.clear();return true;}
std::vector<ActorOccluder> ActorWorld::occluderSnapshot()const{std::vector<ActorOccluder> out;for(const auto& [handle,o]:occluders_){const auto* a=actor(o.ownerId);if(a&&a->registered&&!a->isDisposed&&a->entityHandle==handle)out.push_back(o);}return out;}
bool ActorWorld::lightSegmentBlocked(ActorOccluderPoint from,ActorOccluderPoint to,uint64_t excluded)const{for(const auto& [handle,o]:occluders_){const auto* a=actor(o.ownerId);if(handle!=excluded&&a&&a->registered&&!a->isDisposed&&a->entityHandle==handle&&actorOccluderIntersects(o,from,to))return true;}return false;}
}
