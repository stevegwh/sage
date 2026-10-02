#pragma once

#include "cereal/types/vector.hpp"
#include "engine/content/Json.hpp"
#include "engine/raylib-cereal.hpp"
#include "UI.hpp"
#include <functional>
#include <optional>

namespace sage
{
    enum class UINodeKind
    {
        Canvas,
        Window,
        Table,
        Row,
        Cell
    };

    struct UIReference
    {
        std::string field;
        unsigned int node = 0;
        template <class Archive>
        void serialize(Archive& a)
        {
            a(cereal::make_nvp("field", field), cereal::make_nvp("node", node));
        }
    };

    // Saved data only. IDs and references are local to one canvas document.
    struct CanvasNode
    {
        unsigned int id = 1;
        unsigned int parent = 0;
        UINodeKind kind = UINodeKind::Canvas;
        std::string name = "Canvas";
        std::string text;
        std::string image;
        std::string backgroundImage;
        std::string font;
        std::string script;
        std::vector<UIReference> references;
        Rectangle rectangle{24, 24, 600, 180};
        Rectangle backgroundSource{};
        float percent = 0; // Zero means share the remaining space.
        float gap = 0;
        Padding padding{};
        float fontSize = 24;
        float borderWidth = 0;
        Color background{0, 0, 0, 0};
        Color foreground{235, 231, 218, 255};
        Color border{0, 0, 0, 0};
        Color hover{0, 0, 0, 0};
        Color pressed{0, 0, 0, 0};
        HorizontalAlignment horizontal = HorizontalAlignment::LEFT;
        VerticalAlignment vertical = VerticalAlignment::MIDDLE;
        WindowHorizontalAlignment windowHorizontal = WindowHorizontalAlignment::FREE;
        WindowVerticalAlignment windowVertical = WindowVerticalAlignment::FREE;
        bool visible = true;
        bool enabled = true;
        // Resolves window placement in canvas coordinates. FREE uses rectangle.x/y.
        Rectangle WindowBounds(Vector2 canvasSize) const;
        template <class Archive>
        void serialize(Archive& a)
        {
            a(cereal::make_nvp("id", id),
              cereal::make_nvp("parent", parent),
              cereal::make_nvp("kind", kind),
              cereal::make_nvp("name", name),
              cereal::make_nvp("text", text),
              cereal::make_nvp("image", image),
              cereal::make_nvp("backgroundImage", backgroundImage),
              cereal::make_nvp("font", font),
              cereal::make_nvp("script", script),
              cereal::make_nvp("references", references),
              cereal::make_nvp("x", rectangle.x),
              cereal::make_nvp("y", rectangle.y),
              cereal::make_nvp("width", rectangle.width),
              cereal::make_nvp("height", rectangle.height),
              cereal::make_nvp("sourceX", backgroundSource.x),
              cereal::make_nvp("sourceY", backgroundSource.y),
              cereal::make_nvp("sourceWidth", backgroundSource.width),
              cereal::make_nvp("sourceHeight", backgroundSource.height),
              cereal::make_nvp("percent", percent),
              cereal::make_nvp("gap", gap),
              cereal::make_nvp("paddingTop", padding.top),
              cereal::make_nvp("paddingBottom", padding.bottom),
              cereal::make_nvp("paddingLeft", padding.left),
              cereal::make_nvp("paddingRight", padding.right),
              cereal::make_nvp("fontSize", fontSize),
              cereal::make_nvp("borderWidth", borderWidth),
              cereal::make_nvp("background", background),
              cereal::make_nvp("foreground", foreground),
              cereal::make_nvp("border", border),
              cereal::make_nvp("hover", hover),
              cereal::make_nvp("pressed", pressed),
              cereal::make_nvp("horizontal", horizontal),
              cereal::make_nvp("vertical", vertical),
              cereal::make_nvp("windowHorizontal", windowHorizontal),
              cereal::make_nvp("windowVertical", windowVertical),
              cereal::make_nvp("visible", visible),
              cereal::make_nvp("enabled", enabled));
        }
    };

    struct CanvasDocument
    {
        std::string format = "sage-canvas";
        unsigned int version = 1;
        unsigned int nextId = 2;
        float width = 1920;
        float height = 1080;
        std::vector<CanvasNode> nodes{CanvasNode{}};
        void Validate() const;
        static CanvasDocument FromJson(const json::Value& value);
        static CanvasDocument Load(const std::filesystem::path& path);
        void Save(const std::filesystem::path& path) const;
        std::optional<std::reference_wrapper<CanvasNode>> Find(unsigned int id);
        std::optional<std::reference_wrapper<const CanvasNode>> Find(unsigned int id) const;
        unsigned int Add(unsigned int parent, UINodeKind kind);
        void Remove(unsigned int id);
        bool Move(unsigned int id, unsigned int parent, unsigned int before = 0);
        template <class Archive>
        void serialize(Archive& a)
        {
            a(cereal::make_nvp("format", format),
              cereal::make_nvp("version", version),
              cereal::make_nvp("nextId", nextId),
              cereal::make_nvp("width", width),
              cereal::make_nvp("height", height),
              cereal::make_nvp("nodes", nodes));
        }
    };

    bool CanContain(UINodeKind parent, UINodeKind child);
    const char* UINodeKindName(UINodeKind kind);

} // namespace sage
