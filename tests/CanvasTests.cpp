#include "engine/components/sgTransform.hpp"
#include "engine/content/ContentDocument.hpp"
#include "engine/ui/CanvasRenderer.hpp"
#include "engine/ui/CanvasSystem.hpp"
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }
    void rejects(sage::CanvasDocument document)
    {
        try
        {
            document.Validate();
        }
        catch (const std::exception&)
        {
            return;
        }
        throw std::runtime_error("Invalid document was accepted");
    }
} // namespace
int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) throw std::runtime_error("Expected resource directory");
        static_assert(std::is_same_v<
                      decltype(std::declval<const sage::CanvasDocument&>().Find(1)),
                      std::optional<std::reference_wrapper<const sage::CanvasNode>>>);
        sage::CanvasDocument lookup;
        require(!lookup.Find(0).has_value(), "Missing node lookup must be empty");
        const auto node = lookup.Find(lookup.nodes.front().id);
        require(node.has_value(), "Existing node lookup must have a value");
        node->get().name = "Renamed";
        require(lookup.nodes.front().name == "Renamed", "Node lookup returned a copy");
        const auto& constLookup = lookup;
        require(!constLookup.Find(0).has_value(), "Missing const node lookup must be empty");
        require(
            &constLookup.Find(lookup.nodes.front().id)->get() == &lookup.nodes.front(),
            "Const node lookup did not retain identity");
        const auto asset = std::filesystem::path(argv[1]) / "ui/SettlementHUD.canvas";
        auto doc = sage::CanvasDocument::Load(asset);
        const auto temporary = std::filesystem::temp_directory_path() / "sage-canvas-roundtrip.canvas";
        doc.Save(temporary);
        require(
            sage::json::Stringify(sage::json::Encode(doc)) ==
                sage::json::Stringify(sage::json::Encode(sage::CanvasDocument::Load(temporary))),
            "Canvas round trip changed data");
        std::filesystem::remove(temporary);
        auto invalid = doc;
        invalid.nodes.at(1).parent = invalid.nodes.at(1).id;
        rejects(invalid);
        invalid = doc;
        invalid.nodes.at(1).id = invalid.nodes.at(0).id;
        rejects(invalid);
        invalid = doc;
        invalid.nodes.at(0).references.front().node = 99999;
        invalid.Save(temporary);
        require(
            sage::CanvasDocument::Load(temporary).nodes.at(0).references.front().node == 99999,
            "Unfinished binding did not survive saving");
        {
            entt::registry draftRegistry;
            sage::CanvasSystem draftCanvases(draftRegistry);
            bool rejected = false;
            try
            {
                draftCanvases.Instantiate(temporary.string());
            }
            catch (const std::exception&)
            {
                rejected = true;
            }
            require(rejected, "Broken binding was accepted at runtime");
            require(
                draftRegistry.view<sage::UINode>().begin() == draftRegistry.view<sage::UINode>().end(),
                "Failed canvas leaked runtime nodes");
        }
        std::filesystem::remove(temporary);
        invalid = doc;
        invalid.nodes.at(1).kind = sage::UINodeKind::Row;
        rejects(invalid);
        invalid = doc;
        invalid.width = 0;
        rejects(invalid);
        const auto layout = sage::RenderCanvas(doc, {.x = 0, .y = 0, .width = 1920, .height = 1080}, 0, 0, false);
        const auto scaled = sage::RenderCanvas(doc, {.x = 0, .y = 0, .width = 960, .height = 540}, 0, 0, false);
        for (const auto& [id, b] : layout.bounds)
            require(std::abs(scaled.bounds.at(id).width * 2 - b.width) < .01f, "Layout scale mismatch");
        // Alignment passes through the renderer, including scale, letterboxing and hit testing.
        sage::CanvasDocument aligned;
        const auto alignedWindow = aligned.Add(1, sage::UINodeKind::Window);
        const auto alignedTable = aligned.nodes.back().id;
        const auto alignedCell =
            aligned.Add(aligned.Add(alignedTable, sage::UINodeKind::Row), sage::UINodeKind::Cell);
        auto& placement = aligned.Find(alignedWindow)->get();
        placement.rectangle = {.x = 71, .y = 93, .width = 600, .height = 180};
        placement.windowHorizontal = sage::WindowHorizontalAlignment::CENTER;
        placement.windowVertical = sage::WindowVerticalAlignment::MIDDLE;
        const auto centred =
            sage::RenderCanvas(aligned, {.x = 100, .y = 50, .width = 960, .height = 720}, 0, 0, false);
        const auto centredBounds = centred.bounds.at(alignedWindow);
        require(
            centredBounds.x == 430 && centredBounds.y == 365 && centredBounds.width == 300 &&
                centredBounds.height == 90,
            "Centred window ignored viewport origin, scale or letterboxing");
        require(
            centred.Hit({.x = 580, .y = 410}) == alignedCell, "Centred window hit testing used its old position");
        placement.rectangle.width = 800;
        placement.rectangle.height = 240;
        const auto resized =
            sage::RenderCanvas(aligned, {.x = 0, .y = 0, .width = 1920, .height = 1080}, 0, 0, false);
        require(
            resized.bounds.at(alignedWindow).x == 560 && resized.bounds.at(alignedWindow).y == 420,
            "Changing window size did not recalculate alignment");
        placement.windowHorizontal = sage::WindowHorizontalAlignment::RIGHT;
        placement.windowVertical = sage::WindowVerticalAlignment::BOTTOM;
        const auto bottomRight =
            sage::RenderCanvas(aligned, {.x = 0, .y = 0, .width = 1920, .height = 1080}, 0, 0, false);
        require(
            bottomRight.bounds.at(alignedWindow).x == 1120 && bottomRight.bounds.at(alignedWindow).y == 840,
            "Right/bottom window alignment failed");
        placement.windowHorizontal = sage::WindowHorizontalAlignment::LEFT;
        placement.windowVertical = sage::WindowVerticalAlignment::TOP;
        const auto topLeft =
            sage::RenderCanvas(aligned, {.x = 0, .y = 0, .width = 1920, .height = 1080}, 0, 0, false);
        require(
            topLeft.bounds.at(alignedWindow).x == 0 && topLeft.bounds.at(alignedWindow).y == 0,
            "Left/top window alignment failed");
        aligned.Save(temporary);
        const auto alignedRoundTrip = sage::CanvasDocument::Load(temporary);
        require(
            alignedRoundTrip.Find(alignedWindow)->get().windowVertical == sage::WindowVerticalAlignment::TOP,
            "Window alignment did not survive saving");
        std::filesystem::remove(temporary);
        auto oldJson = sage::json::Encode(aligned);
        for (auto& item : sage::json::Require(oldJson, "nodes").GetArray())
        {
            item.RemoveMember("windowHorizontal");
            item.RemoveMember("windowVertical");
        }
        const auto oldDocument = sage::CanvasDocument::FromJson(oldJson);
        const auto freeBounds =
            sage::RenderCanvas(oldDocument, {.x = 0, .y = 0, .width = 1920, .height = 1080}, 0, 0, false);
        require(
            freeBounds.bounds.at(alignedWindow).x == 71 && freeBounds.bounds.at(alignedWindow).y == 93,
            "Existing canvas positions changed when alignment fields were absent");
        invalid = aligned;
        invalid.Find(alignedWindow)->get().windowHorizontal = static_cast<sage::WindowHorizontalAlignment>(99);
        rejects(invalid);
        auto refs = doc.nodes.at(0).references;
        entt::registry registry;
        sage::CanvasSystem canvases(registry);
        auto first = canvases.Instantiate(asset.string());
        auto second = canvases.Instantiate(asset.string());
        require(first != second, "Canvas instances share identity");
        auto firstRefs = sage::json::Parse(registry.get<sage::ScriptFields>(first).json);
        auto secondRefs = sage::json::Parse(registry.get<sage::ScriptFields>(second).json);
        const auto firstWood = static_cast<entt::entity>(sage::json::Require(firstRefs, "WoodCount").GetUint());
        const auto secondWood = static_cast<entt::entity>(sage::json::Require(secondRefs, "WoodCount").GetUint());
        require(firstWood != secondWood, "References were not remapped per instance");
        const auto window = std::ranges::find_if(
            doc.nodes, [](const auto& node) { return node.kind == sage::UINodeKind::Window; });
        const auto bounds = scaled.bounds.at(window->id);
        const Vector2 point{.x = bounds.x + bounds.width / 2, .y = bounds.y + bounds.height / 2};
        require(
            canvases.Update({.x = 0, .y = 0, .width = 960, .height = 540}, point),
            "Runtime canvas lost its reference dimensions");
        registry.get<sage::UINode>(first).data.visible = false;
        registry.get<sage::UINode>(second).data.visible = false;
        require(
            !canvases.Update({.x = 0, .y = 0, .width = 960, .height = 540}, point),
            "Runtime canvas ignored changed node visibility");
        registry.get<sage::UINode>(second).data.visible = true;
        require(
            canvases.Update({.x = 0, .y = 0, .width = 960, .height = 540}, point),
            "Runtime canvas did not restore node visibility");
        registry.get<sage::UINode>(firstWood).data.text = "99";
        require(registry.get<sage::UINode>(secondWood).data.text == "0", "Instance state leaked");
        require(registry.view<sage::sgTransform>().empty(), "Canvas leaked into world transforms");
        canvases.Destroy(first);
        canvases.CancelInput();
        require(
            !registry.valid(firstWood) && registry.valid(secondWood),
            "Canvas destruction affected another instance");
        registry.ctx().emplace<sage::InitialCanvases>().assets.push_back(asset.string());
        auto map = sage::content::Capture(registry, {});
        entt::registry loaded;
        sage::content::Instantiate(loaded, map);
        require(
            loaded.ctx().get<sage::InitialCanvases>().assets == registry.ctx().get<sage::InitialCanvases>().assets,
            "Scene canvas settings did not round trip");
        std::cout << "Canvas persistence, validation, layout and instance tests passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
