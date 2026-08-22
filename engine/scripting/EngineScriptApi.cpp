#include "EngineScriptApi.hpp"

#include "engine/components/Animation.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/MoveableActor.hpp"
#include "engine/components/sgTransform.hpp"
#include "ScriptApi.hpp"

namespace sage
{
    void RegisterEngineScriptApi(ScriptApiRegistry& api)
    {
        api.SetDefaultManagedNamespace("Sage");
        api.RegisterComponent<sgTransform>("Transform");
        api.RegisterComponent<Collideable>("Collideable");
        api.RegisterComponent<MoveableActor>("MoveableActor");
        api.RegisterComponent<Animation>("Animation");
    }
} // namespace sage
