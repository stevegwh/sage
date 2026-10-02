#include "CanvasSystem.hpp"
#include "CanvasRenderer.hpp"
#include "engine/components/ScriptComponent.hpp"
#include <utility>

namespace sage
{
    CanvasSystem::CanvasSystem(entt::registry& value) : registry(value)
    {
        registry.ctx().emplace<std::reference_wrapper<CanvasSystem>>(*this);
    }
    CanvasSystem::~CanvasSystem()
    {
        for (const auto& instance : instances)
            pendingDestroy.push_back(instance.root);
        flushDestroy();
        registry.ctx().erase<std::reference_wrapper<CanvasSystem>>();
    }
    entt::entity CanvasSystem::Instantiate(const std::string& path)
    {
        const auto document = CanvasDocument::Load(path);
        for (const auto& node : document.nodes)
            for (const auto& ref : node.references)
                if (!document.Find(ref.node))
                    throw std::runtime_error(node.name + ": broken script reference " + ref.field);
        Instance instance{entt::null, {document.width, document.height}, {}};
        std::map<unsigned int, entt::entity> refs;
        try
        {
            for (const auto& node : document.nodes)
            {
                auto entity = registry.create();
                instance.nodes.push_back(entity);
                refs[node.id] = entity;
                registry.emplace<UINode>(entity).data = node;
                if (node.kind == UINodeKind::Canvas) instance.root = entity;
            }
            for (auto entity : instance.nodes)
            {
                auto& node = registry.get<UINode>(entity);
                node.canvas = instance.root;
                if (!node.data.script.empty())
                {
                    json::Document fields(rapidjson::kObjectType);
                    for (const auto& ref : node.data.references)
                        json::Put(
                            fields,
                            ref.field.c_str(),
                            std::uint64_t(entt::to_integral(refs.at(ref.node))),
                            fields.GetAllocator());
                    registry.emplace<ScriptFields>(entity).json = json::Stringify(fields);
                    registry.emplace<ScriptComponent>(entity).className = node.data.script;
                }
            }
        }
        catch (...)
        {
            for (auto e : instance.nodes)
                if (registry.valid(e)) registry.destroy(e);
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
    void CanvasSystem::flushDestroy()
    {
        auto pending = std::exchange(pendingDestroy, {});
        for (auto root : pending)
        {
            auto found =
                std::find_if(instances.begin(), instances.end(), [&](const auto& i) { return i.root == root; });
            if (found == instances.end()) continue;
            auto nodes = std::move(found->nodes);
            instances.erase(found);
            for (auto e : nodes)
                if (registry.valid(e)) registry.remove<ScriptComponent>(e);
            for (auto e : nodes)
                if (registry.valid(e)) registry.destroy(e);
        }
        if (!registry.valid(hovered)) hovered = entt::null;
        if (!registry.valid(pressed)) pressed = entt::null;
    }
    CanvasDocument CanvasSystem::snapshot(const Instance& instance) const
    {
        CanvasDocument doc;
        doc.width = instance.size.x;
        doc.height = instance.size.y;
        doc.nodes.clear();
        doc.nodes.reserve(instance.nodes.size());
        for (auto entity : instance.nodes)
            doc.nodes.push_back(registry.get<UINode>(entity).data);
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
        if (const auto* initial = registry.ctx().find<InitialCanvases>())
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
        const bool captured = registry.valid(pressed);
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
                if (registry.get<UINode>(e).data.id == id) hovered = e;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) pressed = hovered;
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        {
            const auto target = pressed;
            pressed = entt::null;
            if (target == hovered && registry.valid(target))
            {
                // Event-bearing components have stable storage; destruction is deferred until the next update.
                auto& node = registry.get<UINode>(target);
                if (node.data.enabled) node.clicked.Publish();
            }
        }
        return blocksWorld || captured;
    }
    void CanvasSystem::CancelInput()
    {
        hovered = pressed = entt::null;
        flushDestroy();
    }
    void CanvasSystem::Draw(Rectangle viewport) const
    {
        for (const auto& instance : instances)
        {
            const auto id = [&](entt::entity e) {
                const auto* n = registry.try_get<UINode>(e);
                return n && n->canvas == instance.root ? n->data.id : 0u;
            };
            RenderCanvas(snapshot(instance), viewport, id(hovered), id(pressed));
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
