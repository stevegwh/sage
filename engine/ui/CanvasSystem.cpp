#include "CanvasSystem.hpp"
#include "CanvasRenderer.hpp"
#include "engine/components/ScriptComponent.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace sage
{
    CanvasSystem::CanvasSystem(entt::registry& value) : registry(value)
    {
        registry.get().ctx().emplace<std::reference_wrapper<CanvasSystem>>(*this);
    }
    CanvasSystem::~CanvasSystem()
    {
        for (const auto& instance : instances)
            pendingDestroy.push_back(instance.root);
        flushDestroy();
        registry.get().ctx().erase<std::reference_wrapper<CanvasSystem>>();
    }
    entt::entity CanvasSystem::Instantiate(const std::string& path)
    {
        const auto document = CanvasDocument::Load(path);
        for (const auto& node : document.nodes)
            for (const auto& ref : node.references)
                if (!document.Find(ref.node))
                    throw std::runtime_error(node.name + ": broken script reference " + ref.field);
        Instance instance{.root = entt::null, .size = {.x = document.width, .y = document.height}, .nodes = {}};
        std::map<unsigned int, entt::entity> refs;
        try
        {
            for (const auto& node : document.nodes)
            {
                auto entity = registry.get().create();
                instance.nodes.push_back(entity);
                refs[node.id] = entity;
                registry.get().emplace<UINode>(entity).data = node;
                if (node.kind == UINodeKind::Canvas) instance.root = entity;
            }
            for (auto entity : instance.nodes)
            {
                auto& node = registry.get().get<UINode>(entity);
                node.canvas = instance.root;
                if (!node.data.script.empty())
                {
                    json::Document fields(rapidjson::kObjectType);
                    for (const auto& ref : node.data.references)
                        json::Put(
                            fields,
                            ref.field.c_str(),
                            static_cast<std::uint64_t>(entt::to_integral(refs.at(ref.node))),
                            fields.GetAllocator());
                    registry.get().emplace<ScriptFields>(entity).json = json::Stringify(fields);
                    registry.get().emplace<ScriptComponent>(entity).className = node.data.script;
                }
            }
        }
        catch (...)
        {
            for (auto e : instance.nodes)
                if (registry.get().valid(e)) registry.get().destroy(e);
            throw;
        }
        auto root = instance.root;
        instances.push_back(std::move(instance));
        return root;
    }
    void CanvasSystem::Destroy(entt::entity root)
    {
        pendingDestroy.push_back(root);
    }
    void CanvasSystem::BringToFront(entt::entity root)
    {
        const auto found = std::ranges::find(instances, root, &Instance::root);
        if (found != instances.end()) std::rotate(found, std::next(found), instances.end());
    }
    void CanvasSystem::flushDestroy()
    {
        auto pending = std::exchange(pendingDestroy, {});
        for (auto root : pending)
        {
            auto found = std::ranges::find_if(instances, [&](const auto& i) { return i.root == root; });
            if (found == instances.end()) continue;
            auto nodes = std::move(found->nodes);
            instances.erase(found);
            for (auto e : nodes)
                if (registry.get().valid(e)) registry.get().remove<ScriptComponent>(e);
            for (auto e : nodes)
                if (registry.get().valid(e)) registry.get().destroy(e);
        }
        if (!registry.get().valid(hovered)) hovered = entt::null;
        if (!registry.get().valid(pressed)) pressed = entt::null;
        if (draggedWindow && !registry.get().valid(*draggedWindow)) draggedWindow = std::nullopt;
    }
    CanvasDocument CanvasSystem::snapshot(const Instance& instance) const
    {
        CanvasDocument doc;
        doc.width = instance.size.x;
        doc.height = instance.size.y;
        doc.nodes.clear();
        doc.nodes.reserve(instance.nodes.size());
        for (auto entity : instance.nodes)
            doc.nodes.push_back(registry.get().get<UINode>(entity).data);
        for (auto& node : doc.nodes)
        {
            auto parent = node.parent;
            while (parent)
            {
                const auto ancestor = doc.Find(parent);
                node.enabled = node.enabled && ancestor->get().enabled;
                parent = ancestor->get().parent;
            }
        }
        return doc;
    }
    void CanvasSystem::LoadInitial()
    {
        if (const auto* initial = registry.get().ctx().find<InitialCanvases>())
            for (const auto& path : initial->assets)
            {
                try
                {
                    Instantiate(path);
                }
                catch (const std::exception& error)
                {
                    TraceLog(LOG_ERROR, "Canvas %s: %s", path.c_str(), error.what());
                }
            }
    }
    bool CanvasSystem::Update(Rectangle viewport, Vector2 mouse)
    {
        flushDestroy();
        const bool captured = registry.get().valid(pressed);
        hovered = entt::null;
        bool blocksWorld = false;
        for (const auto& instance : instances)
        {
            auto doc = snapshot(instance);
            auto layout = RenderCanvas(doc, viewport, 0, 0, false);
            const bool inside = std::ranges::any_of(doc.nodes, [&](const auto& node) {
                return node.kind == UINodeKind::Window && layout.bounds.contains(node.id) &&
                       CheckCollisionPointRec(mouse, layout.bounds.at(node.id));
            });
            if (!inside) continue;
            blocksWorld = true;
            hovered = entt::null;
            auto id = layout.Hit(mouse);
            const auto hit = doc.Find(id);
            if (!hit || !hit->get().enabled) continue;
            for (auto e : instance.nodes)
                if (registry.get().get<UINode>(e).data.id == id) hovered = e;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            pressed = hovered;
            draggedWindow = std::nullopt;
            dragging = false;
            pressPosition = mouse;
            if (registry.get().valid(pressed))
            {
                const auto& cell = registry.get().get<UINode>(pressed);
                BringToFront(cell.canvas);
                if (cell.data.dragsWindow)
                {
                    auto parent = cell.data.parent;
                    const auto& instance = instances.back();
                    while (parent)
                    {
                        const auto found = std::ranges::find_if(instance.nodes, [&](const auto entity) {
                            return registry.get().get<UINode>(entity).data.id == parent;
                        });
                        if (found == instance.nodes.end()) break;
                        const auto& node = registry.get().get<UINode>(*found).data;
                        if (node.kind == UINodeKind::Window)
                        {
                            draggedWindow = *found;
                            const auto bounds = node.WindowBounds(instance.size);
                            windowPosition = {.x = bounds.x, .y = bounds.y};
                            break;
                        }
                        parent = node.parent;
                    }
                }
            }
        }
        if (draggedWindow && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            const float scale = std::min(
                viewport.width / Settings::TARGET_SCREEN_WIDTH, viewport.height / Settings::TARGET_SCREEN_HEIGHT);
            const Vector2 delta{.x = mouse.x - pressPosition.x, .y = mouse.y - pressPosition.y};
            dragging = dragging || std::abs(delta.x) > 1 || std::abs(delta.y) > 1;
            if (dragging && scale > 0)
            {
                auto& node = registry.get().get<UINode>(*draggedWindow).data;
                node.windowHorizontal = WindowHorizontalAlignment::FREE;
                node.windowVertical = WindowVerticalAlignment::FREE;
                node.rectangle.x = windowPosition.x + delta.x / scale;
                node.rectangle.y = windowPosition.y + delta.y / scale;
            }
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        {
            const auto target = pressed;
            pressed = entt::null;
            draggedWindow = std::nullopt;
            if (!dragging && target == hovered && registry.get().valid(target))
            {
                // Event-bearing components have stable storage; destruction is deferred until the next update.
                auto& node = registry.get().get<UINode>(target);
                if (node.data.enabled) node.clicked.Publish();
            }
        }
        return blocksWorld || captured;
    }
    void CanvasSystem::CancelInput()
    {
        hovered = pressed = entt::null;
        draggedWindow = std::nullopt;
        dragging = false;
        flushDestroy();
    }
    void CanvasSystem::Draw(Rectangle viewport) const
    {
        for (const auto& instance : instances)
        {
            const auto id = [&](entt::entity e) {
                const auto* n = registry.get().try_get<UINode>(e);
                return n && n->canvas == instance.root ? n->data.id : 0u;
            };
            std::map<unsigned int, CellImage> images;
            for (const auto entity : instance.nodes)
            {
                const auto& node = registry.get().get<UINode>(entity);
                if (node.image) images.emplace(node.data.id, *node.image);
            }
            RenderCanvas(snapshot(instance), viewport, id(hovered), id(pressed), true, images);
        }
    }
    entt::entity CanvasSystem::ScriptInstantiate(entt::registry& registry, const std::string& path)
    {
        try
        {
            return registry.ctx().get<std::reference_wrapper<CanvasSystem>>().get().Instantiate(path);
        }
        catch (const std::exception& error)
        {
            TraceLog(LOG_ERROR, "Canvas %s: %s", path.c_str(), error.what());
            return entt::null;
        }
    }
    void CanvasSystem::ScriptDestroy(entt::registry& registry, entt::entity root)
    {
        if (auto* system = registry.ctx().find<std::reference_wrapper<CanvasSystem>>())
            system->get().Destroy(root);
    }
} // namespace sage
