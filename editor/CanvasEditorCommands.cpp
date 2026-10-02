#include "CanvasEditor.hpp"
#include "engine/content/AutomationInbox.hpp"

namespace sage::editor
{
    json::Document CanvasEditor::Inspect() const
    {
        json::Document result(rapidjson::kObjectType);
        auto& a = result.GetAllocator();
        json::Put(result, "active", active, a);
        json::Put(result, "visible", IsActive(), a);
        json::Put(result, "dirty", IsDirty(), a);
        json::Put(result, "path", path.generic_string(), a);
        json::Put(result, "error", error, a);
        json::Put(result, "selection", static_cast<std::uint64_t>(selected), a);
        if (active)
        {
            const auto encoded = json::Encode(document);
            json::Put(result, "revision", content::Revision(encoded), a);
            json::Put(result, "document", encoded, a);
        }
        return result;
    }
    json::Document CanvasEditor::Command(const json::Value& request)
    {
        const auto action = json::String(request, "action");
        if (action == "inspect") return Inspect();
        if (action == "open")
        {
            if (active) throw std::runtime_error("Close the current canvas first");
            Open(json::String(request, "path"));
            if (!active) throw std::runtime_error(error);
            return Inspect();
        }
        if (!active) throw std::runtime_error("No canvas is open");
        if (transaction || moving || resizing || dividerNeighbor)
            throw std::runtime_error("Canvas edit in progress");
        if (json::String(request, "revision") != content::Revision(json::Encode(document)))
            throw std::runtime_error("Canvas revision changed; inspect it again");
        if (action == "select")
        {
            selected = json::Id(request, "node");
            if (!document.Find(selected)) throw std::runtime_error("Unknown canvas node");
        }
        else if (action == "replace")
        {
            auto replacement = CanvasDocument::FromJson(json::Require(request, "document"));
            undo.push_back(state());
            redo.clear();
            document = std::move(replacement);
            if (!document.Find(selected)) selected = document.nodes.front().id;
        }
        else if (action == "undo")
            history(false);
        else if (action == "redo")
            history(true);
        else if (action == "save")
        {
            save();
            if (!error.empty()) throw std::runtime_error(error);
        }
        else if (action == "close")
        {
            if (IsDirty()) throw std::runtime_error("Save or undo canvas changes before closing");
            active = false;
        }
        else
            throw std::runtime_error("Unknown canvas action");
        return Inspect();
    }
} // namespace sage::editor
