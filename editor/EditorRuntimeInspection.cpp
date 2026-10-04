#include "EditorScene.hpp"
#include "engine/IGameRuntime.hpp"

#include <format>

namespace sage
{
    void EditorScene::refreshRuntimeInspection() const
    {
        if (!gameRuntime || !runtimeHierarchy) return;
        auto& registry = gameRuntime->InspectionRegistry();
        if (runtimeSelection && !registry.valid(*runtimeSelection)) runtimeSelection.reset();
        gui->SetRuntimeInspection(true);
        const std::vector<entt::entity> selected =
            runtimeSelection ? std::vector{*runtimeSelection} : std::vector<entt::entity>{};
        gui->SetHierarchy(
            runtimeHierarchy->CollectSceneObjectEntries(true),
            selected,
            selected,
            runtimeSelection.value_or(entt::null));
        if (!runtimeSelection)
        {
            gui->SetRuntimeScript(json::Document{rapidjson::kObjectType});
            gui->SetInspector("None", {}, {});
            return;
        }

        const auto components = inspectorRegistry.InspectRuntime(registry, *runtimeSelection);
        gui->SetRuntimeScript(gameRuntime->InspectScript(*runtimeSelection));
        gui->SetInspector(
            std::format(
                "{} [{}]",
                runtimeHierarchy->GetEntityName(*runtimeSelection),
                entt::to_integral(*runtimeSelection)),
            components,
            {});
    }

    void EditorScene::frameRuntimeObject() const
    {
        if (!gameRuntime || !runtimeSelection) return;
        const auto target = editor::ComputeFocusTarget(gameRuntime->InspectionRegistry(), {*runtimeSelection});
        if (!target) return;
        auto camera = *gameRuntime->GetCamera();
        editor::EditorCamera framing;
        const auto viewport = gameViewportScreenRect();
        framing.Focus(camera, *target, viewport.width / std::max(1.0f, viewport.height));
        json::Document command(rapidjson::kObjectType);
        auto& allocator = command.GetAllocator();
        json::Put(command, "op", "camera", allocator);
        json::Put(command, "position", json::Encode(camera.position), allocator);
        json::Put(command, "target", json::Encode(camera.target), allocator);
        gameRuntime->Command(command);
    }
} // namespace sage
