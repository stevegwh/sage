#include "EngineScriptApi.hpp"

#include "engine/components/Animation.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/sgTransform.hpp"
#include "ScriptApi.hpp"

namespace sage
{
    void RegisterEngineScriptApi(ScriptApiRegistry& registry)
    {
        registry.RegisterComponent<sgTransform>("Sage", "Transform");
        registry.RegisterComponent<Collideable>("Sage", "Collideable");
        registry.RegisterComponent<Animation>("Sage", "Animation");
    }
} // namespace sage
