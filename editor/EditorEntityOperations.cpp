#include "EditorEntityOperations.hpp"

#include "EditorComponents.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/Renderable.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/EngineSystems.hpp"
#include "engine/Light.hpp"
#include "engine/systems/NavigationGridSystem.hpp"
#include "engine/systems/TransformSystem.hpp"

#include "cereal/archives/binary.hpp"

#include <sstream>
#include <unordered_map>
#include <vector>

namespace sage::editor
{
    EditorEntityOperations::EditorEntityOperations(EngineSystems* _sys) : sys(_sys)
    {
    }

    void EditorEntityOperations::DeleteEntityAndChildren(const entt::entity entity) const
    {
        if (!sys->registry->valid(entity)) return;

        std::vector<entt::entity> children;
        if (sys->registry->any_of<sgTransform>(entity))
        {
            children = sys->registry->get<sgTransform>(entity).GetChildren();
        }

        for (const auto child : children)
        {
            DeleteEntityAndChildren(child);
        }

        if (sys->registry->valid(entity) && sys->registry->any_of<sgTransform>(entity))
        {
            sys->registry->get<sgTransform>(entity).SetParent(entt::null);
        }

        releaseNavigationOccupation(entity);

        if (sys->registry->valid(entity))
        {
            sys->registry->destroy(entity);
        }
    }

    void EditorEntityOperations::releaseNavigationOccupation(const entt::entity entity) const
    {
        if (!sys->registry->valid(entity) || !sys->registry->any_of<Collideable>(entity)) return;

        const auto& collideable = sys->registry->get<Collideable>(entity);
        if (collideable.blocksNavigation)
        {
            sys->navigationGridSystem->MarkSquareAreaOccupied(collideable.worldBoundingBox, false, entity);
        }
    }

    void EditorEntityOperations::CopyEntities(const std::vector<entt::entity>& roots)
    {
        clipboard.clear();

        auto isDescendantOfAnotherRoot = [&](const entt::entity candidate) {
            for (auto cur = sys->registry->valid(candidate) && sys->registry->any_of<sgTransform>(candidate)
                                ? sys->registry->get<sgTransform>(candidate).GetParent()
                                : entt::null;
                 cur != entt::null;)
            {
                if (std::ranges::find(roots, cur) != roots.end()) return true;
                if (!sys->registry->valid(cur) || !sys->registry->any_of<sgTransform>(cur)) break;
                cur = sys->registry->get<sgTransform>(cur).GetParent();
            }
            return false;
        };

        for (const auto root : roots)
        {
            if (!sys->registry->valid(root) || !sys->registry->any_of<sgTransform>(root)) continue;
            if (isDescendantOfAnotherRoot(root)) continue;

            auto& subtree = clipboard.emplace_back();
            captureSubtree(root, subtree);
        }
    }

    bool EditorEntityOperations::HasClipboard() const
    {
        return !clipboard.empty();
    }

    void EditorEntityOperations::captureSubtree(const entt::entity root, ClipboardSubtree& subtree) const
    {
        subtree.originalParent = sys->registry->get<sgTransform>(root).GetParent();

        // Depth-first walk so children always follow their parent; local ids are
        // indices into subtree.records.
        std::vector<entt::entity> order;
        std::unordered_map<entt::entity, std::int32_t> localIds;
        auto visit = [&](auto& self, const entt::entity entity) -> void {
            if (!sys->registry->valid(entity) || !sys->registry->any_of<sgTransform>(entity)) return;
            localIds.emplace(entity, static_cast<std::int32_t>(order.size()));
            order.push_back(entity);
            for (const auto child : sys->registry->get<sgTransform>(entity).GetChildren())
            {
                self(self, child);
            }
        };
        visit(visit, root);

        subtree.records.reserve(order.size());
        for (const auto entity : order)
        {
            auto& record = subtree.records.emplace_back();
            const auto& transform = sys->registry->get<sgTransform>(entity);
            record.name = transform.name;

            const auto parentIter = localIds.find(transform.GetParent());
            record.parentLocalId = (parentIter != localIds.end()) ? parentIter->second : -1;

            record.worldPos = transform.GetWorldPos();
            record.worldRot = transform.GetWorldRot();
            record.worldScale = transform.GetScale();

            if (sys->registry->any_of<Collideable>(entity))
            {
                record.hasCollideable = true;
                record.collideable = sys->registry->get<Collideable>(entity);
            }
            if (sys->registry->any_of<Renderable>(entity))
            {
                record.hasRenderable = true;
                std::ostringstream stream(std::ios::binary);
                {
                    cereal::BinaryOutputArchive archive(stream);
                    archive(sys->registry->get<Renderable>(entity));
                }
                record.renderableBlob = stream.str();
            }
            if (sys->registry->any_of<Light>(entity))
            {
                record.hasLight = true;
                record.light = sys->registry->get<Light>(entity);
            }
        }
    }

    std::vector<entt::entity> EditorEntityOperations::PasteClipboard() const
    {
        std::vector<entt::entity> newRoots;
        newRoots.reserve(clipboard.size());

        for (const auto& subtree : clipboard)
        {
            const auto root = instantiateSubtree(subtree);
            if (root != entt::null) newRoots.push_back(root);
        }
        return newRoots;
    }

    entt::entity EditorEntityOperations::instantiateSubtree(const ClipboardSubtree& subtree) const
    {
        if (subtree.records.empty()) return entt::null;

        // Create entities up-front so parent local ids resolve to real entities.
        std::vector<entt::entity> created;
        created.reserve(subtree.records.size());
        for (std::size_t i = 0; i < subtree.records.size(); ++i)
        {
            created.push_back(sys->registry->create());
        }

        const bool reparentRoot = subtree.originalParent != entt::null &&
                                  sys->registry->valid(subtree.originalParent) &&
                                  sys->registry->any_of<sgTransform>(subtree.originalParent);

        // Records are in depth-first order with the root first, so applying them
        // in order is top-down: each parent is positioned before its children, and
        // SetParent recomputes the local transform while preserving world pose.
        for (std::size_t i = 0; i < subtree.records.size(); ++i)
        {
            const auto& record = subtree.records[i];
            const auto entity = created[i];

            auto& transform = sys->registry->emplace<sgTransform>(entity);
            transform.position.world = record.worldPos;
            transform.rotation.world = record.worldRot;
            transform.scale.world = record.worldScale;
            transform.name = (i == 0) ? record.name + " (Copy)" : record.name;

            if (record.parentLocalId >= 0 &&
                static_cast<std::size_t>(record.parentLocalId) < created.size())
            {
                transform.SetParent(created[static_cast<std::size_t>(record.parentLocalId)]);
            }
            else if (reparentRoot)
            {
                transform.SetParent(subtree.originalParent);
            }
        }

        // Restore optional components after the hierarchy is in place.
        for (std::size_t i = 0; i < subtree.records.size(); ++i)
        {
            const auto& record = subtree.records[i];
            const auto entity = created[i];

            sys->registry->emplace<EditorMapEntity>(entity);

            if (record.hasCollideable)
            {
                sys->registry->emplace<Collideable>(entity, record.collideable);
            }
            if (record.hasRenderable)
            {
                std::istringstream stream(record.renderableBlob, std::ios::binary);
                Renderable renderable;
                {
                    cereal::BinaryInputArchive archive(stream);
                    archive(renderable);
                }
                sys->registry->emplace<Renderable>(entity, std::move(renderable));
            }
            if (record.hasLight)
            {
                sys->registry->emplace<Light>(entity, record.light);
            }
        }

        return created.front();
    }
} // namespace sage::editor
