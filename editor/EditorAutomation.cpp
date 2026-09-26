#include "EditorScene.hpp"
#include "engine/Colors.hpp"
#include "engine/Camera.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/content/ContentDocument.hpp"
#include "engine/EngineSystems.hpp"
#include "engine/Flatpack.hpp"
#include "engine/IGameRuntime.hpp"
#include "engine/Light.hpp"
#include "engine/systems/TransformSystem.hpp"
#include <set>

namespace sage
{
    json::Document EditorScene::automationState() const
    {
        auto document = content::Capture(*sys->registry, collectMapHierarchyOrder());
        json::Document state(rapidjson::kObjectType);
        auto& a = state.GetAllocator();
        json::Put(state, "revision", content::Revision(document), a);
        json::Put(state, "document", document, a);
        json::Put(
            state,
            "path",
            flatpackSession->IsActive() ? flatpackSession->Path().string() : mapController->Path().string(),
            a);
        json::Put(
            state,
            "dirty",
            flatpackSession->IsActive() ? flatpackSession->HasUnsavedChanges()
                                        : mapController->HasUnsavedChanges(),
            a);
        json::Put(state, "playing", IsPlaying(), a);
        json::Value selected(rapidjson::kArrayType);
        for (auto e : selection->Selected())
            if (auto* id = sys->registry->try_get<PersistentEntityId>(e))
                selected.PushBack(json::Value(id->id), a);
        json::Put(state, "selection", selected, a);
        if (gameRuntime) json::Put(state, "runtime", gameRuntime->Inspect(), a);
        return state;
    }
    json::Document EditorScene::automationCommand(const json::Value& request) const
    {
        const auto command = json::String(request, "command");
        if (command == "inspect") return automationState();
        if (command == "schema")
        {
            if (!request.HasMember("entity")) return content::DescribeComponents();
            const auto state = automationState();
            for (const auto& node : state["document"]["entities"].GetArray())
                if (json::Id(node, "id") == json::Id(request, "entity")) return content::DescribeComponents(&node);
            throw std::runtime_error("Unknown entity");
        }
        if (command == "validate")
        {
            auto state = automationState();
            const auto errors = content::Validate(state["document"]);
            json::Put(state, "valid", errors.empty(), state.GetAllocator());
            return state;
        }
        const auto state = automationState();
        if (json::String(request, "revision") != json::String(state, "revision"))
            throw std::runtime_error("Document revision changed; inspect the session again");
        if (history->HasActiveTransaction())
            throw std::runtime_error("An inspector or transform edit is in progress");
        if (command == "quit")
        {
            if (mapController->HasUnsavedChanges() || flatpackSession->HasUnsavedChanges())
                throw std::runtime_error("Save unsaved changes before quitting");
            sys->settings->ExitProgram();
            return automationState();
        }
        if (command == "runtime")
        {
            if (!gameRuntime) throw std::runtime_error("No play session");
            return gameRuntime->Command(json::Require(request, "operation"));
        }
        if (command == "play")
        {
            bool paused = false;
            if (request.HasMember("paused")) json::Decode(request["paused"], paused);
            startPlay();
            if (!gameRuntime) throw std::runtime_error("Could not start play session");
            gameRuntime->SetPaused(paused);
        }
        else if (command == "stop")
            stopPlay();
        else if (command == "capture")
        {
            const auto capture = std::filesystem::path(json::String(request, "path"));
            if (capture.extension() != ".png") throw std::runtime_error("Capture path must end in .png");
            if (!capture.parent_path().empty()) std::filesystem::create_directories(capture.parent_path());
            std::optional<std::filesystem::path> bundle;
            if (request.HasMember("bundle"))
            {
                bundle = json::String(request, "bundle");
                if (!bundle->parent_path().empty()) std::filesystem::create_directories(bundle->parent_path());
            }
            automationCapture = capture;
            automationBundle = bundle;
            json::Document response(rapidjson::kObjectType);
            json::Put(response, "scheduled", true, response.GetAllocator());
            json::Put(response, "path", automationCapture->string(), response.GetAllocator());
            return response;
        }
        else
        {
            if (gameRuntime) throw std::runtime_error("Stop play before changing source content");
            if (command == "open")
            {
                if (mapController->HasUnsavedChanges() || flatpackSession->HasUnsavedChanges())
                    throw std::runtime_error("Save or explicitly discard unsaved edits before opening content");
                const std::filesystem::path path = json::String(request, "path");
                auto document = content::ReadDocument(path);
                if (json::String(document, "kind") == "flatpack")
                {
                    flatpackSession->Open(path);
                    if (!flatpackSession->IsActive() || flatpackSession->Path() != path)
                        throw std::runtime_error("Flatpack load failed");
                }
                else
                {
                    if (flatpackSession->IsActive()) throw std::runtime_error("Close the flatpack first");
                    mapController->LoadMap(path);
                    if (mapController->Path() != path) throw std::runtime_error("Map load failed");
                }
            }
            else if (command == "save")
            {
                if (flatpackSession->IsActive())
                    flatpackSession->Save();
                else if (request.HasMember("path"))
                    mapController->SaveAs(json::String(request, "path"));
                else
                {
                    if (mapController->Path().empty()) throw std::runtime_error("Provide a save path");
                    mapController->SaveMap();
                }
                if (flatpackSession->IsActive() ? flatpackSession->HasUnsavedChanges()
                                                : mapController->HasUnsavedChanges())
                    throw std::runtime_error("Save failed; content edits are still unsaved");
            }
            else if (command == "copy")
                CopySelection();
            else if (command == "paste")
                PasteClipboard();
            else if (command == "undo")
                history->Undo();
            else if (command == "redo")
                history->Redo();
            else if (command == "select")
            {
                const auto entity = content::FindEntityById(*sys->registry, json::Id(request, "entity"));
                if (entity == entt::null) throw std::runtime_error("Unknown entity");
                (void)selection->Select(entity);
            }
            else if (command == "place")
            {
                Vector3 position;
                json::Decode(json::Require(request, "position"), position);
                if (!PlaceFlatpackAt(json::String(request, "path"), position))
                    throw std::runtime_error("Could not place flatpack");
            }
            else if (command == "camera")
            {
                Vector3 position, target;
                json::Decode(json::Require(request, "position"), position);
                json::Decode(json::Require(request, "target"), target);
                sys->camera->SetCamera(position, target);
            }
            else if (command == "edit")
            {
                const auto& operation = json::Require(request, "operation");
                const auto op = json::String(operation, "op");
                auto candidate = json::Parse(json::Stringify(state["document"]));
                content::Apply(candidate, operation, candidate.GetAllocator());
                if (op == "create")
                {
                    std::set<std::uint32_t> original;
                    for (const auto& node : state["document"]["entities"].GetArray())
                        original.insert(json::Id(node, "id"));
                    std::vector<entt::entity> created;
                    for (const auto& node : candidate["entities"].GetArray())
                        if (!original.contains(json::Id(node, "id")))
                        {
                            Vector3 position;
                            json::Decode(node["transform"]["position"], position);
                            auto entity = entityOperations->CreateEmptyTransform(position);
                            sys->registry->emplace_or_replace<PersistentEntityId>(entity, json::Id(node, "id"));
                            sys->registry->get<sgTransform>(entity).name = json::String(node, "name");
                            if (node["parent"].IsUint())
                                sys->registry->get<sgTransform>(entity).SetParent(
                                    content::FindEntityById(*sys->registry, node["parent"].GetUint()));
                            created.push_back(entity);
                        }
                    adoptIntoFlatpackRoot(created);
                    history->RecordCreate(editor::EditAction::AddEmptyTransform, created);
                    onHistoryApplied(created);
                    return automationState();
                }
                const auto entity = content::FindEntityById(*sys->registry, json::Id(operation, "entity"));
                if (entity == entt::null) throw std::runtime_error("Unknown entity");
                if (op == "model")
                {
                    (void)selection->Select(entity);
                    changeSelectedModels(json::String(operation, "value"));
                    return automationState();
                }
                if (op == "delete")
                {
                    history->RecordDestroy(editor::EditAction::Delete, {entity});
                    entityOperations->DeleteEntityAndChildren(entity);
                    selection->Clear();
                }
                else
                {
                    history->Begin(editor::EditAction::EditField, {entity});
                    try
                    {
                        auto& transform = sys->registry->get<sgTransform>(entity);
                        if (op == "rename")
                            transform.name = json::String(operation, "value");
                        else if (op == "transform")
                        {
                            Vector3 v;
                            json::Decode(operation["value"], v);
                            const auto field = json::String(operation, "field");
                            if (field == "position")
                                transform.position.world = v;
                            else if (field == "rotation")
                                transform.rotation.world = v;
                            else
                                transform.scale.world = v;
                        }
                        else if (op == "reparent")
                        {
                            entt::entity parent = entt::null;
                            if (!operation["parent"].IsNull())
                                parent = content::FindEntityById(*sys->registry, operation["parent"].GetUint());
                            transform.SetParent(parent);
                        }
                        else if (op == "set" || op == "add" || op == "remove")
                        {
                            const auto key = json::String(operation, "component");
                            const detail::ComponentOperations* operations = nullptr;
                            for (const auto& c : detail::RegisteredComponentOperations())
                                if (c.key == key)
                                {
                                    operations = &c;
                                    break;
                                }
                            if (!operations)
                                throw std::runtime_error("Component has no registered edit operations");
                            if (op == "set")
                                operations->edit(
                                    *sys->registry,
                                    entity,
                                    json::String(operation, "field"),
                                    json::Stringify(operation["value"]));
                            else if (op == "remove")
                                operations->remove(*sys->registry, entity);
                            else
                                operations->restoreJson(
                                    *sys->registry, entity, json::Stringify(operation["value"]));
                            if (key == "sage.Light" && op == "set" &&
                                json::String(operation, "field") == "position")
                                transform.position.world = sys->registry->get<Light>(entity).position;
                            if (op == "add")
                            {
                                std::unordered_map<std::uint32_t, entt::entity> references;
                                for (auto e : sys->registry->view<PersistentEntityId>())
                                    references.emplace(
                                        static_cast<std::uint32_t>(sys->registry->get<PersistentEntityId>(e).id),
                                        e);
                                ResolveRegisteredComponentReferences(*sys->registry, entity, references);
                            }
                        }
                        else
                            throw std::runtime_error("Unsupported live edit");
                        onHistoryApplied({entity});
                        history->Commit();
                    }
                    catch (...)
                    {
                        history->Rollback();
                        throw;
                    }
                }
                refreshSceneWindows();
            }
            else
                throw std::runtime_error("Unknown editor command: " + command);
        }
        return automationState();
    }
    void EditorScene::CaptureAutomationFrame(Texture2D sceneTexture, Texture2D uiTexture) const
    {
        if (!automationCapture) return;
        const auto path = *automationCapture;
        automationCapture.reset();
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        RenderTexture2D composite{};
        if (uiTexture.id != 0)
        {
            composite = LoadRenderTexture(sceneTexture.width, sceneTexture.height);
            BeginTextureMode(composite);
            ClearBackground(sage::colors::BLANK_COLOR);
            DrawTextureRec(
                sceneTexture, {0, 0, float(sceneTexture.width), -float(sceneTexture.height)}, {0, 0}, sage::colors::WHITE_COLOR);
            DrawTextureRec(uiTexture, {0, 0, float(uiTexture.width), -float(uiTexture.height)}, {0, 0}, sage::colors::WHITE_COLOR);
            EndTextureMode();
            sceneTexture = composite.texture;
        }
        auto image = LoadImageFromTexture(sceneTexture);
        ImageFlipVertical(&image);
        const bool saved = ExportImage(image, path.string().c_str());
        UnloadImage(image);
        if (composite.id != 0) UnloadRenderTexture(composite);
        if (automationBundle)
        {
            auto state = automationState();
            json::Put(state, "screenshot", path.string(), state.GetAllocator());
            json::Put(state, "captured", saved, state.GetAllocator());
            const auto bundle = *automationBundle;
            automationBundle.reset();
            if (!bundle.parent_path().empty()) std::filesystem::create_directories(bundle.parent_path());
            std::ofstream output(bundle);
            output << json::Stringify(state);
        }
    }
} // namespace sage
