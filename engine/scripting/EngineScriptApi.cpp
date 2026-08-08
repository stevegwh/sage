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
        api.RegisterComponent<sgTransform>("Sage", "Transform");
        api.RegisterComponent<Collideable>("Sage", "Collideable");
        api.RegisterComponent<MoveableActor>("Sage", "MoveableActor");
        api.RegisterComponent<Animation>("Sage", "Animation");
    }
} // namespace sage
