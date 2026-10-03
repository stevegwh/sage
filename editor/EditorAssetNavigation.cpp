#include "EditorScene.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/content/ContentDocument.hpp"
#include "engine/EngineSystems.hpp"
#include "engine/ResourceManager.hpp"
#include <algorithm>

namespace sage
{
    editor::AssetUsageResults EditorScene::findAssetUsages(const editor::BrowserAsset& asset) const
    {
        std::vector<editor::UsageDocument> documents;
        const auto entities = collectMapHierarchyOrder();
        auto scene = content::Capture(*sys->registry, entities);
        editor::AddEditorAssetReferences(scene, *sys->registry, entities);
        documents.push_back(
            {.path = flatpackSession->IsActive() ? flatpackSession->Path() : mapController->Path(),
             .document = std::move(scene)});
        if (canvasEditor->HasDocument())
        {
            auto state = canvasEditor->Inspect();
            json::Document document;
            document.CopyFrom(state["document"], document.GetAllocator());
            documents.push_back({.path = canvasEditor->Path(), .document = std::move(document)});
        }
        auto result = editor::FindAssetUsages(asset, "resources", csharpScripts, documents);
        if (asset.kind == editor::BrowserAssetKind::Material)
        {
            const auto& resources = ResourceManager::GetInstance();
            for (const auto& key : resources.GetModelKeys())
                if (std::ranges::any_of(resources.GetModelMaterialKeys(key), [&](const auto& material) {
                        return material == asset.key ||
                               std::ranges::find(asset.aliases, material) != asset.aliases.end();
                    }))
                    result.usages.push_back(
                        {.path = resources.GetModelSourcePath(key),
                         .location = key,
                         .field = "Imported model material"});
        }
        if (asset.kind == editor::BrowserAssetKind::Image)
        {
            auto& resources = ResourceManager::GetInstance();
            const auto texture = resources.TextureLoad(asset.key);
            if (texture.id)
                for (const auto& key : resources.GetMaterialKeys())
                {
                    const auto& material = resources.GetMaterial(key);
                    for (int slot = 0; slot < MAX_MATERIAL_MAPS; ++slot)
                        if (material.maps[slot].texture.id == texture.id)
                            result.usages.push_back(
                                {.location = key,
                                 .field = "Imported material texture slot " + std::to_string(slot)});
                }
        }
        return result;
    }

    std::string EditorScene::navigateAssetUsage(const editor::AssetUsage& usage) const
    {
        const auto samePath = [](const auto& a, const auto& b) {
            return !a.empty() && !b.empty() &&
                   std::filesystem::absolute(a).lexically_normal() ==
                       std::filesystem::absolute(b).lexically_normal();
        };
        if (usage.path.empty() && !usage.live)
            return "This reference belongs to the packed material shown in the Object / node column.";
        if (usage.path.extension() == ".cs")
        {
            OpenURL(("file://" + std::filesystem::absolute(usage.path).string()).c_str());
            return {};
        }
        if (usage.nodeId)
        {
            if (!samePath(usage.path, canvasEditor->Path()))
            {
                if (canvasEditor->IsDirty())
                    return "Save or close the current canvas before opening another usage.";
                canvasEditor->Open(usage.path);
            }
            if (!canvasEditor->HasDocument() || !samePath(usage.path, canvasEditor->Path()))
                return "The canvas could not be opened. Check the canvas editor error.";
            canvasEditor->Resume();
            if (!canvasEditor->SelectNode(*usage.nodeId))
                return "The referenced canvas node no longer exists. Refresh the results.";
            return {};
        }
        if (usage.path.extension() == ".canvas")
        {
            if (canvasEditor->IsDirty() && !samePath(usage.path, canvasEditor->Path()))
                return "Save or close the current canvas before opening another usage.";
            canvasEditor->Open(usage.path);
            return {};
        }
        if (IsPlaying()) return "Stop Play before navigating to a scene usage.";
        const auto current = flatpackSession->IsActive() ? flatpackSession->Path() : mapController->Path();
        const bool currentDocument =
            samePath(usage.path, current) || (usage.live && usage.path.empty() && current.empty());
        if (usage.live && !currentDocument) return "The open document changed. Refresh the usage results.";
        if (!currentDocument)
        {
            if (flatpackSession->IsActive())
                return "Close the current flatpack before opening another scene usage.";
            if (mapController->HasUnsavedChanges())
                return "Save the current map before opening another scene usage.";
            if (usage.path.extension() == ".map")
                mapController->LoadMap(usage.path);
            else if (usage.path.extension() == ".flatpack")
                flatpackSession->Open(usage.path);
            else
            {
                if (!usage.path.empty())
                    OpenURL(("file://" + std::filesystem::absolute(usage.path).string()).c_str());
                return {};
            }
        }
        const auto opened = flatpackSession->IsActive() ? flatpackSession->Path() : mapController->Path();
        if (!usage.path.empty() && !samePath(opened, usage.path))
            return "The referenced document could not be opened. Refresh the results.";
        canvasEditor->ShowScene();
        if (usage.entityId)
        {
            auto entity = content::FindEntityById(*sys->registry, *usage.entityId);
            // The edit session remembers authored IDs before instantiation assigns fresh IDs.
            if (!usage.live && usage.path.extension() == ".flatpack")
            {
                const auto found = flatpackSourceEntities.find(*usage.entityId);
                entity = found != flatpackSourceEntities.end() && sys->registry->valid(found->second)
                             ? found->second
                             : entt::null;
            }
            if (entity == entt::null)
                return "Document opened; the referenced object could not be resolved. Refresh the results.";
            editorModes->SelectSceneFromHierarchy({.entity = entity});
            gui->FocusHierarchyOnEntity(entity);
            focusSelectedObject();
        }
        return {};
    }
} // namespace sage
