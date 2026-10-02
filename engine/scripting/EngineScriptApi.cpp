#include "EngineScriptApi.hpp"

#include "engine/components/Animation.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/MoveableActor.hpp"
#include "engine/components/Renderable.hpp"
#include "engine/components/sgTransform.hpp"
#include "ScriptApi.hpp"
#include "engine/ui/CanvasSystem.hpp"

namespace sage
{
    void RegisterEngineScriptApi(ScriptApiRegistry& api)
    {
        api.SetDefaultManagedNamespace("Sage");
        api.RegisterComponent<UINode>("UINode");
        api.RegisterComponent<ScriptFields>("ScriptFields");
        api.RegisterSystem<CanvasSystem>("UI");
        api.RegisterComponent<sgTransform>("Transform");
        api.RegisterComponent<Collideable>("Collideable");
        api.RegisterComponent<MoveableActor>("MoveableActor");
        api.RegisterComponent<Animation>("Animation");
        api.RegisterComponent<Renderable>("Renderable");
    }
} // namespace sage
