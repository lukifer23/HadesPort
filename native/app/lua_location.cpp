#include "lua_runtime.hpp"
#include <stdexcept>
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
