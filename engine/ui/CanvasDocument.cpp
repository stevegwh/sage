#include "CanvasDocument.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>
#include <utility>

namespace sage
{
    Rectangle CanvasNode::WindowBounds(Vector2 canvasSize) const
    {
        auto bounds = rectangle;
        switch (windowHorizontal)
        {
        case WindowHorizontalAlignment::LEFT:
            bounds.x = 0;
            break;
        case WindowHorizontalAlignment::CENTER:
            bounds.x = (canvasSize.x - bounds.width) / 2;
            break;
        case WindowHorizontalAlignment::RIGHT:
            bounds.x = canvasSize.x - bounds.width;
            break;
        case WindowHorizontalAlignment::FREE:
            break;
        }
        switch (windowVertical)
        {
        case WindowVerticalAlignment::TOP:
            bounds.y = 0;
            break;
        case WindowVerticalAlignment::MIDDLE:
            bounds.y = (canvasSize.y - bounds.height) / 2;
            break;
        case WindowVerticalAlignment::BOTTOM:
            bounds.y = canvasSize.y - bounds.height;
            break;
        case WindowVerticalAlignment::FREE:
            break;
        }
        return bounds;
    }

    bool CanContain(UINodeKind parent, UINodeKind child)
    {
        return (parent == UINodeKind::Canvas && child == UINodeKind::Window) ||
               (parent == UINodeKind::Window && child == UINodeKind::Table) ||
               (parent == UINodeKind::Table && child == UINodeKind::Row) ||
               (parent == UINodeKind::Row && child == UINodeKind::Cell) ||
               (parent == UINodeKind::Cell && child == UINodeKind::Table);
    }
    const char* UINodeKindName(UINodeKind kind)
    {
        switch (kind)
        {
        case UINodeKind::Canvas:
            return "Canvas";
        case UINodeKind::Window:
            return "Window";
        case UINodeKind::Table:
            return "Table";
        case UINodeKind::Row:
            return "Row";
        case UINodeKind::Cell:
            return "Cell";
        }
        return "Invalid";
    }
    std::optional<std::reference_wrapper<CanvasNode>> CanvasDocument::Find(unsigned int id)
    {
        for (auto& n : nodes)
            if (n.id == id) return std::ref(n);
        return std::nullopt;
    }
    std::optional<std::reference_wrapper<const CanvasNode>> CanvasDocument::Find(unsigned int id) const
    {
        for (const auto& n : nodes)
            if (n.id == id) return std::ref(n);
        return std::nullopt;
    }
    void CanvasDocument::Validate() const
    {
        if (format != "sage-canvas" || version != 1) throw std::runtime_error("Unsupported canvas format/version");
        if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0)
            throw std::runtime_error("Canvas dimensions must be positive");
        if (nodes.empty() || nodes.size() > 10000) throw std::runtime_error("Invalid canvas node count");
        std::set<unsigned int> ids;
        unsigned int roots = 0;
        for (const auto& n : nodes)
        {
            if (!n.id || n.id >= nextId || !ids.insert(n.id).second)
                throw std::runtime_error("Duplicate or zero canvas node ID");
            if (n.kind == UINodeKind::Canvas)
            {
                ++roots;
                if (n.parent) throw std::runtime_error("Canvas must be the root");
            }
            else
            {
                const auto parent = Find(n.parent);
                if (!parent || !CanContain(parent->get().kind, n.kind))
                    throw std::runtime_error("Invalid UI parent for " + n.name);
            }
            std::set<unsigned int> ancestors{n.id};
            auto parent = n.parent;
            while (parent)
            {
                if (!ancestors.insert(parent).second) throw std::runtime_error("Cycle in canvas hierarchy");
                const auto p = Find(parent);
                if (!p) throw std::runtime_error("Missing canvas parent");
                parent = p->get().parent;
            }
            int tables = 0;
            for (const auto& child : nodes)
                if (child.parent == n.id && child.kind == UINodeKind::Table) ++tables;
            if ((n.kind == UINodeKind::Window && tables != 1) || (n.kind == UINodeKind::Cell && tables > 1))
                throw std::runtime_error("Window needs one table; cell permits at most one: " + n.name);
            if (tables && (!n.text.empty() || !n.image.empty()))
                throw std::runtime_error("Nested table replaces cell content: " + n.name);
            if (!n.text.empty() && !n.image.empty())
                throw std::runtime_error("Cell cannot contain both text and image");
            for (float v :
                 {n.percent,
                  n.gap,
                  n.fontSize,
                  n.borderWidth,
                  n.rectangle.x,
                  n.rectangle.y,
                  n.rectangle.width,
                  n.rectangle.height,
                  n.padding.top,
                  n.padding.bottom,
                  n.padding.left,
                  n.padding.right})
                if (!std::isfinite(v)) throw std::runtime_error("Non-finite canvas property");
            if (n.percent < 0 || n.percent > 100 || n.gap < 0 || n.fontSize <= 0 || n.borderWidth < 0 ||
                n.rectangle.width < 0 || n.rectangle.height < 0)
                throw std::runtime_error("Invalid canvas size/style");
            if (static_cast<int>(n.horizontal) < 0 || static_cast<int>(n.horizontal) > 2 ||
                static_cast<int>(n.vertical) < 0 || static_cast<int>(n.vertical) > 2)
                throw std::runtime_error("Invalid text alignment");
            if (static_cast<int>(n.windowHorizontal) < 0 || static_cast<int>(n.windowHorizontal) > 3 ||
                static_cast<int>(n.windowVertical) < 0 || static_cast<int>(n.windowVertical) > 3)
                throw std::runtime_error("Invalid window alignment");
            std::set<std::string> fields;
            for (const auto& ref : n.references)
                if (ref.field.empty() || !fields.insert(ref.field).second)
                    throw std::runtime_error("Invalid script reference on " + n.name);
        }
        if (nodes.front().kind != UINodeKind::Canvas)
            throw std::runtime_error("Canvas root must be the first node");
        if (roots != 1) throw std::runtime_error("Canvas needs exactly one root");
    }
    CanvasDocument CanvasDocument::FromJson(const json::Value& value)
    {
        json::Document source;
        source.CopyFrom(value, source.GetAllocator());
        // Alignment is optional in existing version-1 documents. Default to their saved position.
        const auto& nodes = json::Require(source, "nodes");
        if (!nodes.IsArray()) throw std::runtime_error("Canvas nodes must be an array");
        for (auto& node : source["nodes"].GetArray())
        {
            if (!node.IsObject()) throw std::runtime_error("Canvas node must be an object");
            for (const auto& [field, alignment] :
                 {std::pair{"windowHorizontal", static_cast<int>(WindowHorizontalAlignment::FREE)},
                  std::pair{"windowVertical", static_cast<int>(WindowVerticalAlignment::FREE)}})
            {
                if (!node.HasMember(field))
                    json::Put(node, field, static_cast<std::uint64_t>(alignment), source.GetAllocator());
                if (!node[field].IsInt() || node[field].GetInt() < 0 || node[field].GetInt() > 3)
                    throw std::runtime_error("Invalid window alignment");
            }
        }
        CanvasDocument result;
        json::Decode(source, result);
        result.Validate();
        return result;
    }
    CanvasDocument CanvasDocument::Load(const std::filesystem::path& path)
    {
        return FromJson(json::Parse(json::Read(path)));
    }
    void CanvasDocument::Save(const std::filesystem::path& path) const
    {
        Validate();
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        const auto temporary = path.string() + ".writing-" +
                               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        try
        {
            std::ofstream output(temporary);
            output << json::Stringify(json::Encode(*this));
            output.close();
            if (!output) throw std::runtime_error("Cannot save canvas: " + path.string());
            Load(temporary);
            std::filesystem::rename(temporary, path);
        }
        catch (...)
        {
            std::filesystem::remove(temporary);
            throw;
        }
    }
    unsigned int CanvasDocument::Add(unsigned int parent, UINodeKind kind)
    {
        auto p = Find(parent);
        if (!p || !CanContain(p->get().kind, kind)) throw std::runtime_error("Invalid UI child type");
        if (kind == UINodeKind::Table)
        {
            for (const auto& n : nodes)
                if (n.parent == parent) throw std::runtime_error("This node already has a table");
            p->get().text.clear();
            p->get().image.clear();
        }
        const unsigned int id = nextId++;
        CanvasNode n;
        n.id = id;
        n.parent = parent;
        n.kind = kind;
        n.name = UINodeKindName(kind);
        if (kind == UINodeKind::Window) n.background = {.r = 24, .g = 29, .b = 38, .a = 245};
        if (kind == UINodeKind::Cell) n.text = "Label";
        nodes.push_back(n);
        if (kind == UINodeKind::Window) Add(id, UINodeKind::Table);
        return id;
    }
    void CanvasDocument::Remove(unsigned int id)
    {
        const auto n = Find(id);
        if (!n || n->get().kind == UINodeKind::Canvas) return;
        if (const auto p = Find(n->get().parent); p && p->get().kind == UINodeKind::Window) return;
        std::set<unsigned int> removed{id};
        bool changed = true;
        while (changed)
        {
            changed = false;
            for (const auto& item : nodes)
                if (removed.contains(item.parent) && removed.insert(item.id).second) changed = true;
        }
        std::erase_if(nodes, [&](const auto& item) { return removed.contains(item.id); });
        // Keep broken references visible to validation rather than silently retargeting them.
    }
    bool CanvasDocument::Move(unsigned int id, unsigned int parent, unsigned int before)
    {
        const auto n = Find(id);
        const auto p = Find(parent);
        if (!n || !p || !CanContain(p->get().kind, n->get().kind) || id == before) return false;
        auto old = nodes;
        auto copy = n->get();
        copy.parent = parent;
        std::erase_if(nodes, [&](const auto& item) { return item.id == id; });
        auto pos = std::ranges::find_if(
            nodes, [&](const auto& item) { return item.id == before && item.parent == parent; });
        nodes.insert(pos, copy);
        try
        {
            Validate();
            return true;
        }
        catch (...)
        {
            nodes = std::move(old);
            return false;
        }
    }

} // namespace sage
