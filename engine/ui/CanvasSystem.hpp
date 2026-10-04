#pragma once

#include "CanvasDocument.hpp"
#include "engine/Event.hpp"
#include "entt/entt.hpp"
#include <functional>
#include <map>

namespace sage
{
    // Runtime component: no world transform or world hierarchy membership.
    struct UINode
    {
        CanvasNode data;
        entt::entity canvas = entt::null;
        Event<> clicked;
        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.field("Name", data.name, false);
            i.field("Kind", data.kind, false);
            i.field("Text", data.text, false);
            i.field("Visible", data.visible, false);
            i.field("Enabled", data.enabled, false);
            i.note("Canvas", std::to_string(entt::to_integral(canvas)));
        }
        template <class Api>
        static void define_script_api(Api& api)
        {
            api.property(
                "Text",
                [](const UINode& n) { return n.data.text; },
                [](UINode& n, const std::string& value) { n.data.text = value; });
            api.property(
                "Visible",
                [](const UINode& n) { return n.data.visible; },
                [](UINode& n, bool value) { n.data.visible = value; });
            api.property(
                "Enabled",
                [](const UINode& n) { return n.data.enabled; },
                [](UINode& n, bool value) { n.data.enabled = value; });
            api.readonly("Name", [](const UINode& n) { return n.data.name; });
            api.readonly("Canvas", &UINode::canvas);
            api.event("Clicked", &UINode::clicked);
        }
    };

    struct ScriptFields
    {
        std::string json = "{}";
        template <class Api>
        static void define_script_api(Api& api)
        {
            api.readonly("Json", &ScriptFields::json);
        }
    };

    // Stored as scene settings, not as a world entity.
    struct InitialCanvases
    {
        std::vector<std::string> assets;
    };

    class CanvasSystem
    {
        struct Instance
        {
            entt::entity root;
            Vector2 size;
            std::vector<entt::entity> nodes;
        };
        std::reference_wrapper<entt::registry> registry;
        std::vector<Instance> instances;
        entt::entity hovered = entt::null;
        entt::entity pressed = entt::null;
        std::vector<entt::entity> pendingDestroy;
        void flushDestroy();
        [[nodiscard]] CanvasDocument snapshot(const Instance& instance) const;

      public:
        explicit CanvasSystem(entt::registry& registry);
        CanvasSystem(const CanvasSystem&) = delete;
        CanvasSystem& operator=(const CanvasSystem&) = delete;
        CanvasSystem(CanvasSystem&&) = delete;
        CanvasSystem& operator=(CanvasSystem&&) = delete;
        ~CanvasSystem();
        entt::entity Instantiate(const std::string& path);
        void Destroy(entt::entity root);
        void LoadInitial();
        bool Update(Rectangle viewport, Vector2 mouse);
        void CancelInput();
        void Draw(Rectangle viewport) const;
        static entt::entity ScriptInstantiate(entt::registry& registry, const std::string& path);
        static void ScriptDestroy(entt::registry& registry, entt::entity root);
        template <class Api>
        static void define_script_api(Api& api)
        {
            api.method("Instantiate", &ScriptInstantiate, {"asset"});
            api.method("Destroy", &ScriptDestroy, {"canvas"});
        }
    };
} // namespace sage
