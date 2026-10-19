#include "lua_runtime.hpp"
#include <stdexcept>
#include <cmath>
#include "recovered_runtime.hpp"
namespace {
bool spawnPointInside(const std::vector<hades::ActorPoint>& polygon,float x,float y){
 bool inside=false;if(polygon.empty())return false;auto a=polygon.back();for(const auto b:polygon){if((a.y>y)!=(b.y>y)&&x<(b.x-a.x)*(y-a.y)/(b.y-a.y)+a.x)inside=!inside;a=b;}return inside;
}
}
bool ScriptRuntime::spawnPointBlocked(float x,float y,bool& blocked,std::string& error){try{
 // GridManager::IsLocationBlocked307f70, selected argument tuple(true,false,true):
 // StopsUnits obstacle point polygons, then Unit polygons with BlockSpawns.
 // Broadphase uses the documented portable sparse grid, not Windows map clamping.
 blocked=false;std::vector<int32_t> candidates;if(!actorWorld_.spatialObstacleCandidates({x,y,x,y},hades::ObstacleGridCategory::StopsUnits,candidates,error))return false;
 for(const auto id:candidates){const auto* actor=actorWorld_.actor(id);if(!actor||!actor->registered)continue;bool stops=false;if(!actorWorld_.obstacleStops(id,false,stops,error))return false;if(stops&&spawnPointInside(actor->geometry,x,y)){blocked=true;error.clear();return true;}}
 for(const auto id:actorWorld_.registrationOrder()){const auto* actor=actorWorld_.actor(id);if(!actor||!actor->registered||!actor->definition||actor->definition->kind!="Unit")continue;const auto* flag=actorWorld_.currentProperty(id,"BlockSpawns");if(!flag)throw std::runtime_error("Spawn point query requires native BlockSpawns");if(flag->boolean()&&spawnPointInside(actor->geometry,x,y)){blocked=true;break;}}
 error.clear();return true;
 }catch(const std::exception& e){error=e.what();return false;}}
bool ScriptRuntime::closestNonBlockedLocation(float x,float y,float& outX,float& outY,std::string& error){
 // FindClosestNonBlockedLocation307da0: original point first, then16 attempts.
 // Each consumes radius and angle draws, even the first fixed32-radius draw.
 bool blocked=false;if(!spawnPointBlocked(x,y,blocked,error))return false;outX=x;outY=y;if(!blocked)return true;
 for(int attempt=1;attempt<=16;++attempt){double radiusDraw=0,angleDraw=0;if(!nextRecoveredEngineRandom(state_,radiusDraw)||!nextRecoveredEngineRandom(state_,angleDraw)){error="Spawn relocation requires actual native RNG";return false;}
  const float radius=float(radiusDraw)*(float(attempt)*32.f-32.f)+32.f,angle=float(angleDraw)*6.2831854820251465f;
  const float px=x+std::cos(angle)*radius,py=y-std::sin(angle)*radius;
  if(!spawnPointBlocked(px,py,blocked,error))return false;if(!blocked){outX=px;outY=py;return true;}
 }outX=outY=0;return true;
}
bool ScriptRuntime::locationAuxiliary(int32_t id,hades::ActorLocationStage stage,std::string& error){
 try{
    auto* actor=actorWorld_.actor(id);if(!actor||!actor->registered||!actor->object)throw std::runtime_error("Location auxiliary owner unavailable");
    const auto property=[&](const char* key)->const hades::SourceValue&{const auto* value=actorWorld_.currentProperty(id,key);if(!value)throw std::runtime_error(std::string("Location source property unavailable: ")+key);return *value;};
    switch(stage){
    case hades::ActorLocationStage::Interaction:
        if(!std::holds_alternative<std::monostate>(property("Thing.Interact").value)){
            // UseableManager::MarkForUpdate: re-index the moved Thing's Interact bounds.
            for(const auto& s:interactions_.snapshots())if(s.key.id==id&&s.key.lifetime==actor->entityHandle)return interactions_.markForUpdate(s.key,error);
        }
        break;
    case hades::ActorLocationStage::Light:
        return actorLights_.locationChanged(id,error);
    case hades::ActorLocationStage::Occluder:
        return actorWorld_.updateOccluder(id,error);
    case hades::ActorLocationStage::AttachedAnimations:
        // Portable presentation and VFX owner providers resolve the actual
        // current Thing transform when sampled, rather than duplicating it in
        // a second transform cache. Structural descendants remain independent.
        actor->object->graphicsDirty=true;
        for(auto child:actor->attachedChildren){auto* attached=actorWorld_.actor(child);if(attached&&attached->registered&&attached->attachedParentLifetime==actor->entityHandle&&attached->object)attached->object->graphicsDirty=true;}
        break;
    }
    error.clear();return true;
 }catch(const std::exception& e){error=e.what();return false;}
}
