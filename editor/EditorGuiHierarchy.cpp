#include "EditorGui.hpp"

#include "EditorGuiInternal.hpp"
#include "engine/Settings.hpp"

#include "imgui.h"

#include "raylib.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sage::editor
{
    namespace
    {
        constexpr const char* HIERARCHY_DRAG_PAYLOAD = "SAGE_HIER_ENTITY";

        std::uint32_t EntityPayloadId(const entt::entity entity)
        {
            return static_cast<std::uint32_t>(entt::to_integral(entity));
        }

        entt::entity EntityFromPayloadId(const std::uint32_t id)
        {
            return static_cast<entt::entity>(id);
        }
    } // namespace

    void EditorGui::DrawHierarchyWindow()
    {
        if (!settings) return;

        if (focusedHierarchyEntity.has_value() &&
            !std::ranges::any_of(hierarchyEntries, [this](const SceneObjectEntry& entry) {
                return entry.entity == *focusedHierarchyEntity;
            }))
        {
            focusedHierarchyEntity.reset();
        }

        const auto viewportOffset = settings->GetViewportOffset();
        const auto viewport = settings->GetViewPort();
        const float mainMenuHeight = ImGui::GetFrameHeight();
        const float leftDockWidth = dockLayout ? dockLayout->leftDockWidth : EDITOR_LEFT_DOCK_DEFAULT_WIDTH;
        const ImVec2 windowPos{viewportOffset.x, viewportOffset.y + mainMenuHeight};
        const ImVec2 windowSize{
            settings->ScaleValueWidth(leftDockWidth), std::max(1.0f, viewport.y - mainMenuHeight)};

        ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ToImGuiColor(EDITOR_WINDOW_BACKGROUND));
        ImGui::PushStyleColor(ImGuiCol_Text, ToImGuiColor(EDITOR_TEXT));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4{0.18f, 0.26f, 0.38f, 0.90f});
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4{0.23f, 0.34f, 0.50f, 0.95f});
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4{0.25f, 0.42f, 0.68f, 1.00f});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{14.0f, 12.0f});

        std::optional<SceneSelectionRequest> sceneSelectionRequest;
        std::optional<HierarchyMoveRequest> hierarchyMoveRequest;

        auto acceptHierarchyDrop = [&](const entt::entity newParent, const entt::entity insertBefore) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(HIERARCHY_DRAG_PAYLOAD);
                payload != nullptr && payload->Delivery && payload->DataSize == sizeof(std::uint32_t))
            {
                const auto dragged = EntityFromPayloadId(*static_cast<const std::uint32_t*>(payload->Data));
                if (dragged != newParent && dragged != insertBefore)
                {
                    hierarchyMoveRequest = HierarchyMoveRequest{
                        .dragged = dragged,
                        .newParent = newParent,
                        .insertBefore = insertBefore};
                }
            }
        };

        constexpr ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

        if (ImGui::Begin("Hierarchy", nullptr, windowFlags))
        {
            const float resizeHandleWidth = dockLayout ? DOCK_RESIZE_HANDLE_THICKNESS : 0.0f;
            const float resizeHandleX = windowPos.x + windowSize.x - resizeHandleWidth;

            const float filterWidth = std::max(1.0f, resizeHandleX - ImGui::GetCursorScreenPos().x);
            DrawSearchFilter(hierarchyFilter, "hierarchy_filter", "Search...", filterWidth);
            ImGui::Spacing();

            const ImVec2 contentRegion = ImGui::GetContentRegionAvail();
            const ImVec2 treePos = ImGui::GetCursorScreenPos();
            const ImVec2 treeSize{
                dockLayout ? std::max(1.0f, resizeHandleX - treePos.x)
                           : std::max(1.0f, contentRegion.x),
                std::max(1.0f, contentRegion.y)};

            if (ImGui::BeginChild("hierarchy_scroll", treeSize, false))
            {
                auto drawBlankDropTarget = [&]() {
                    const ImVec2 available = ImGui::GetContentRegionAvail();
                    const ImVec2 dropSize{std::max(1.0f, available.x), std::max(28.0f, available.y)};
                    const ImVec2 dropMin = ImGui::GetCursorScreenPos();
                    ImGui::InvisibleButton("##hierarchy_blank_drop", dropSize);
                    bool targetActive = false;
                    if (ImGui::BeginDragDropTarget())
                    {
                        targetActive = true;
                        acceptHierarchyDrop(entt::null, entt::null);
                        ImGui::EndDragDropTarget();
                    }
                    if (targetActive)
                    {
                        const auto color = ImGui::GetColorU32(ImVec4{0.36f, 0.58f, 0.92f, 0.85f});
                        ImGui::GetWindowDrawList()->AddRect(
                            dropMin,
                            ImVec2{dropMin.x + dropSize.x, dropMin.y + dropSize.y},
                            color,
                            4.0f,
                            0,
                            2.0f);
                    }
                };

                if (hierarchyEntries.empty())
                {
                    ImGui::TextDisabled("No scene objects");
                    drawBlankDropTarget();
                }
                else
                {
                    auto subtreeContainsEntity = [this](const std::size_t rootIndex, const entt::entity entity) {
                        if (rootIndex >= hierarchyEntries.size()) return false;
                        const int rootDepth = hierarchyEntries[rootIndex].depth;
                        for (std::size_t i = rootIndex; i < hierarchyEntries.size(); ++i)
                        {
                            if (i != rootIndex && hierarchyEntries[i].depth <= rootDepth) break;
                            if (hierarchyEntries[i].entity == entity) return true;
                        }
                        return false;
                    };

                    // True if the entry at rootIndex or any of its descendants matches the
                    // active search filter. Used to keep ancestors of a match visible.
                    auto subtreePassesFilter = [this](const std::size_t rootIndex) {
                        if (rootIndex >= hierarchyEntries.size()) return false;
                        const int rootDepth = hierarchyEntries[rootIndex].depth;
                        for (std::size_t i = rootIndex; i < hierarchyEntries.size(); ++i)
                        {
                            if (i != rootIndex && hierarchyEntries[i].depth <= rootDepth) break;
                            if (hierarchyFilter.PassFilter(hierarchyEntries[i].displayName.c_str())) return true;
                        }
                        return false;
                    };

                    auto drawInsertBeforeTarget = [&](const std::size_t entryIndex) {
                        const auto& entry = hierarchyEntries[entryIndex];
                        constexpr float targetHeight = 7.0f;
                        const ImVec2 targetMin = ImGui::GetCursorScreenPos();
                        const float targetWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);

                        ImGui::PushID(static_cast<int>(entryIndex));
                        ImGui::InvisibleButton("##insert_before", ImVec2{targetWidth, targetHeight});
                        bool targetActive = false;
                        if (ImGui::BeginDragDropTarget())
                        {
                            targetActive = true;
                            acceptHierarchyDrop(entry.parent, entry.entity);
                            ImGui::EndDragDropTarget();
                        }
                        ImGui::PopID();

                        if (targetActive)
                        {
                            const float indent = static_cast<float>(entry.depth) * ImGui::GetTreeNodeToLabelSpacing();
                            const float y = targetMin.y + targetHeight * 0.5f;
                            const auto color = ImGui::GetColorU32(ImVec4{0.36f, 0.58f, 0.92f, 1.00f});
                            ImGui::GetWindowDrawList()->AddLine(
                                ImVec2{targetMin.x + indent, y},
                                ImVec2{targetMin.x + targetWidth, y},
                                color,
                                2.0f);
                        }
                    };

                    auto skipSubtree = [this](std::size_t& index, const int depth) {
                        ++index;
                        while (index < hierarchyEntries.size() && hierarchyEntries[index].depth > depth)
                        {
                            ++index;
                        }
                    };

                    std::function<void(std::size_t&)> drawEntry = [&](std::size_t& index) {
                        if (index >= hierarchyEntries.size()) return;

                        const std::size_t entryIndex = index;
                        const auto& entry = hierarchyEntries[entryIndex];

                        if (hierarchyFilter.IsActive() && !subtreePassesFilter(entryIndex))
                        {
                            skipSubtree(index, entry.depth);
                            return;
                        }

                        const bool hasChildren = entryIndex + 1 < hierarchyEntries.size() &&
                                                 hierarchyEntries[entryIndex + 1].depth > entry.depth;
                        const bool selected =
                            std::ranges::find(selectedSceneEntities, entry.entity) != selectedSceneEntities.end();

                        drawInsertBeforeTarget(entryIndex);

                        ImGuiTreeNodeFlags nodeFlags =
                            ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
                            ImGuiTreeNodeFlags_OpenOnDoubleClick;
                        if (selected) nodeFlags |= ImGuiTreeNodeFlags_Selected;
                        if (!hasChildren) nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

                        if (hasChildren)
                        {
                            if (hierarchyFilter.IsActive() ||
                                (focusedHierarchyEntity.has_value() &&
                                 subtreeContainsEntity(entryIndex, *focusedHierarchyEntity)))
                            {
                                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                            }
                            else
                            {
                                ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                            }
                        }

                        const auto imguiEntityId =
                            static_cast<std::uintptr_t>(EntityPayloadId(entry.entity)) + 1u;
                        const bool open = ImGui::TreeNodeEx(
                            reinterpret_cast<void*>(imguiEntityId),
                            nodeFlags,
                            "%s  %s",
                            entry.icon ? entry.icon : "",
                            entry.displayName.c_str());

                        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
                        {
                            sceneSelectionRequest = makeSceneSelectionRequest(entry.entity);
                        }

                        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                        {
                            pendingHierarchyContextEntity = entry.entity;
                        }

                        if (focusedHierarchyEntity.has_value() && *focusedHierarchyEntity == entry.entity)
                        {
                            ImGui::SetScrollHereY(0.5f);
                            focusedHierarchyEntity.reset();
                        }

                        if (ImGui::BeginDragDropSource())
                        {
                            const std::uint32_t payload = EntityPayloadId(entry.entity);
                            ImGui::SetDragDropPayload(HIERARCHY_DRAG_PAYLOAD, &payload, sizeof(payload));
                            ImGui::TextUnformatted(entry.displayName.c_str());
                            ImGui::EndDragDropSource();
                        }

                        if (ImGui::BeginDragDropTarget())
                        {
                            acceptHierarchyDrop(entry.entity, entt::null);
                            ImGui::EndDragDropTarget();
                        }

                        if (!hasChildren)
                        {
                            ++index;
                        }
                        else if (open)
                        {
                            ++index;
                            while (index < hierarchyEntries.size() && hierarchyEntries[index].depth > entry.depth)
                            {
                                drawEntry(index);
                            }
                            ImGui::TreePop();
                        }
                        else
                        {
                            skipSubtree(index, entry.depth);
                        }

                    };

                    std::size_t index = 0;
                    while (index < hierarchyEntries.size())
                    {
                        drawEntry(index);
                    }
                    drawBlankDropTarget();
                }
            }
            ImGui::EndChild();

            if (dockLayout)
            {
                const float handleTop = windowPos.y + ImGui::GetFrameHeight();
                dockLayoutChanged |= DrawDockResizeHandle(
                    "##hierarchy_resize",
                    ImVec2{resizeHandleX, handleTop},
                    ImVec2{resizeHandleWidth, std::max(1.0f, windowSize.y - ImGui::GetFrameHeight())},
                    ImGuiMouseCursor_ResizeEW,
                    [this, viewport](const ImVec2 delta) {
                        const float logicalDelta =
                            delta.x * Settings::TARGET_SCREEN_WIDTH / std::max(1.0f, viewport.x);
                        return SetLeftDockWidth(*dockLayout, dockLayout->leftDockWidth + logicalDelta);
                    });
            }
        }
        ImGui::End();

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(5);

        if (sceneSelectionRequest.has_value() && onSceneObjectSelectedCb)
        {
            onSceneObjectSelectedCb(*sceneSelectionRequest);
        }
        if (hierarchyMoveRequest.has_value() && onHierarchyMoveCb)
        {
            onHierarchyMoveCb(*hierarchyMoveRequest);
        }
    }


    EditorGui::SceneSelectionRequest EditorGui::makeSceneSelectionRequest(const entt::entity clicked) const
    {
        const ImGuiIO& io = ImGui::GetIO();

        // Shift selects every entity in the displayed tree between the anchor and
        // the clicked entity, mirroring file-explorer range selection.
        if (io.KeyShift && hierarchySelectionAnchor != entt::null)
        {
            const auto indexOf = [this](const entt::entity entity) -> std::optional<std::size_t> {
                for (std::size_t i = 0; i < hierarchyEntries.size(); ++i)
                {
                    if (hierarchyEntries[i].entity == entity) return i;
                }
                return std::nullopt;
            };

            const auto anchorIndex = indexOf(hierarchySelectionAnchor);
            const auto clickedIndex = indexOf(clicked);
            if (anchorIndex.has_value() && clickedIndex.has_value())
            {
                const auto [lo, hi] = std::minmax(*anchorIndex, *clickedIndex);
                std::vector<entt::entity> rangeEntities;
                rangeEntities.reserve(hi - lo + 1);
                for (std::size_t i = lo; i <= hi; ++i)
                {
                    rangeEntities.push_back(hierarchyEntries[i].entity);
                }
                return SceneSelectionRequest{
                    .entity = clicked, .mode = SceneSelectionMode::Range, .rangeEntities = std::move(rangeEntities)};
            }
        }

        // Alt/meta toggles a single entity into or out of the selection.
        if (io.KeyAlt)
        {
            return SceneSelectionRequest{.entity = clicked, .mode = SceneSelectionMode::Toggle};
        }

        return SceneSelectionRequest{.entity = clicked, .mode = SceneSelectionMode::Replace};
    }

    void EditorGui::SetHierarchy(
        const std::vector<SceneObjectEntry>& entries,
        std::vector<entt::entity> selectedEntities,
        const entt::entity selectionAnchor)
    {
        hierarchyEntries = entries;
        selectedSceneEntities = std::move(selectedEntities);
        hierarchySelectionAnchor = selectionAnchor;
    }

    std::optional<entt::entity> EditorGui::ConsumeHierarchyContextEntity()
    {
        const auto entity = pendingHierarchyContextEntity;
        pendingHierarchyContextEntity.reset();
        return entity;
    }

    void EditorGui::FocusHierarchyOnEntity(const entt::entity entity)
    {
        focusedHierarchyEntity = entity;
    }

} // namespace sage::editor
