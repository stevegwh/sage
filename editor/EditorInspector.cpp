#include "EditorInspector.hpp"
#include "engine/AssetKey.hpp"
#include "engine/components/EntityVisibility.hpp"

#include "EditorComponents.hpp"
#include "engine/CollisionLayers.hpp"
#include "engine/components/Animation.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/CollisionIntent.hpp"
#include "engine/components/CustomShaderComponent.hpp"
#include "engine/components/MoveableActor.hpp"
#include "engine/components/ParticleEmitterComponent.hpp"
#include "engine/components/ParticleSystemComponent.hpp"
#include "engine/components/Renderable.hpp"
#include "engine/components/ScriptComponent.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/CursorTypes.hpp"
#include "engine/Light.hpp"
#include "engine/ResourceManager.hpp"
#include "engine/SceneTags.hpp"
#include "engine/ui/CanvasSystem.hpp"
#include "InspectorFieldUI.hpp"
#include "project/CustomArchetypes.hpp"
#include "project/CustomCursors.hpp"
#include "project/CustomSceneTags.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

namespace sage::editor
{
    namespace
    {
        std::optional<ModelPickerField> DescribeModelPicker(
            const entt::registry& registry, const std::vector<entt::entity>& entities)
        {
            if (entities.empty() || !registry.valid(entities.front())) return std::nullopt;

            const auto* firstRenderable = registry.try_get<Renderable>(entities.front());
            if (firstRenderable == nullptr) return std::nullopt;
            const auto firstModel = firstRenderable->GetModel();

            ModelPickerField picker{
                .currentKey = firstModel.has_value() ? firstModel->get().GetKey() : std::string{},
                .options = ResourceManager::GetInstance().GetModelKeys(),
                .animationCompatibleOnly = std::ranges::any_of(entities, [&](const entt::entity entity) {
                    return registry.valid(entity) && registry.any_of<Animation>(entity);
                })};

            picker.mixed = std::ranges::any_of(entities, [&](const entt::entity entity) {
                if (!registry.valid(entity)) return true;
                const auto* renderable = registry.try_get<Renderable>(entity);
                const auto model = renderable ? renderable->GetModel() : std::nullopt;
                return !model || model->get().GetKey() != picker.currentKey;
            });

            if (picker.animationCompatibleOnly)
            {
                std::erase_if(picker.options, [](const std::string& key) {
                    return !ResourceManager::GetInstance().HasModelAnimation(key);
                });
            }

            picker.displayOptions = AssetLabels(picker.options);

            return picker;
        }

        std::vector<MaterialPickerField> DescribeMaterialPickers(
            const entt::registry& registry, const std::vector<entt::entity>& entities)
        {
            if (entities.empty() || !registry.valid(entities.front())) return {};
            const auto* first = registry.try_get<Renderable>(entities.front());
            if (first == nullptr || !first->GetModel()) return {};

            const auto& firstKeys = first->GetMaterialKeys();
            if (std::ranges::any_of(entities, [&](const entt::entity entity) {
                    const auto* renderable =
                        registry.valid(entity) ? registry.try_get<Renderable>(entity) : nullptr;
                    return renderable == nullptr || !renderable->GetModel() ||
                           renderable->GetMaterialKeys().size() != firstKeys.size();
                }))
            {
                return {};
            }

            const auto options = ResourceManager::GetInstance().GetMaterialKeys();
            std::vector<MaterialPickerField> pickers;
            pickers.reserve(firstKeys.size());
            for (unsigned int i = 0; i < firstKeys.size(); ++i)
            {
                MaterialPickerField picker{.materialIndex = i, .currentKey = firstKeys.at(i), .options = options};
                picker.mixed = std::ranges::any_of(entities, [&](const entt::entity entity) {
                    return registry.get<Renderable>(entity).GetMaterialKeys().at(i) != picker.currentKey;
                });
                pickers.push_back(std::move(picker));
            }
            return pickers;
        }

        bool HasCatalogModel(const std::optional<ModelPickerField>& picker)
        {
            return picker.has_value() && !picker->mixed && !picker->currentKey.empty() &&
                   std::ranges::find(picker->options, picker->currentKey) != picker->options.end();
        }

        template <class T>
        bool leafValueEquals(const LeafField<T>& lhs, const LeafField<T>& rhs)
        {
            if (!lhs.data || !rhs.data) return lhs.data.has_value() == rhs.data.has_value();
            return lhs.data->get() == rhs.data->get();
        }

        bool leafValueEquals(const LeafField<Vector2>& lhs, const LeafField<Vector2>& rhs)
        {
            if (!lhs.data || !rhs.data) return lhs.data.has_value() == rhs.data.has_value();
            return lhs.data->get().x == rhs.data->get().x && lhs.data->get().y == rhs.data->get().y;
        }

        bool leafValueEquals(const LeafField<Vector3>& lhs, const LeafField<Vector3>& rhs)
        {
            if (!lhs.data || !rhs.data) return lhs.data.has_value() == rhs.data.has_value();
            return lhs.data->get().x == rhs.data->get().x && lhs.data->get().y == rhs.data->get().y &&
                   lhs.data->get().z == rhs.data->get().z;
        }

        bool leafValueEquals(const LeafField<::Color>& lhs, const LeafField<::Color>& rhs)
        {
            if (!lhs.data || !rhs.data) return lhs.data.has_value() == rhs.data.has_value();
            return lhs.data->get().r == rhs.data->get().r && lhs.data->get().g == rhs.data->get().g &&
                   lhs.data->get().b == rhs.data->get().b && lhs.data->get().a == rhs.data->get().a;
        }

        template <class T>
        void commitLeaf(const LeafField<T>& field, const T& value)
        {
            if (field.setter)
                field.setter(value);
            else if (field.data.has_value())
                field.data->get() = value;
        }

        // Fills the per-axis multi-selection data on a vector aggregate: a bitmask of
        // which axes differ across the selection, and a setter that writes one axis to
        // every selected entity while preserving each entity's other axes. `axes` are the
        // vector's component members (e.g. {&Vector3::x, &Vector3::y, &Vector3::z}).
        template <class VecT, std::size_t N>
        void populateVectorAggregate(
            LeafField<VecT>& aggregate,
            const std::vector<LeafField<VecT>>& leafFields,
            const std::array<float VecT::*, N>& axes)
        {
            for (std::size_t axis = 0; axis < N; ++axis)
            {
                const float base = leafFields.front().data ? leafFields.front().data->get().*axes.at(axis) : 0.0f;
                const bool differs = std::ranges::any_of(leafFields, [&](const LeafField<VecT>& field) {
                    return (field.data ? field.data->get().*axes.at(axis) : 0.0f) != base;
                });
                if (differs) aggregate.mixedComponents |= 1u << axis;
            }
            aggregate.componentSetter = [leafFields, axes](const std::size_t axis, const float value) {
                for (const auto& field : leafFields)
                {
                    VecT v = field.data ? field.data->get() : VecT{};
                    v.*axes.at(axis) = value;
                    commitLeaf(field, v);
                }
            };
        }

        bool enumOptionsMatch(const EnumField& lhs, const EnumField& rhs)
        {
            return lhs.options == rhs.options;
        }

        bool enumValueEquals(const EnumField& lhs, const EnumField& rhs)
        {
            if (!lhs.getIndex || !rhs.getIndex) return !lhs.getIndex && !rhs.getIndex;
            return lhs.getIndex() == rhs.getIndex();
        }

        std::optional<InspectorField> aggregateMatchingFields(const std::vector<InspectorField>& fields)
        {
            if (fields.empty()) return std::nullopt;

            InspectorField result;
            result.label = fields.front().label;
            result.editable =
                std::ranges::all_of(fields, [](const InspectorField& field) { return field.editable; });
            result.scriptFile = fields.front().scriptFile;
            result.shaderFile = fields.front().shaderFile;

            if (!std::ranges::all_of(fields, [&fields](const InspectorField& field) {
                    return field.value.index() == fields.front().value.index();
                }))
            {
                return std::nullopt;
            }

            bool supported = true;
            result.value = std::visit(
                [&fields, &result, &supported]<typename T0>(const T0& firstValue) -> FieldValue {
                    using T = std::decay_t<T0>;
                    if constexpr (std::is_same_v<T, DividerField>)
                    {
                        return firstValue;
                    }
                    else if constexpr (std::is_same_v<T, BoundedCollectionField>)
                    {
                        if (fields.size() != 1)
                        {
                            supported = false;
                            return firstValue;
                        }
                        return firstValue;
                    }
                    else if constexpr (std::is_same_v<T, NoteField>)
                    {
                        for (const auto& field : fields)
                        {
                            const auto* note = std::get_if<NoteField>(&field.value);
                            if (note == nullptr)
                            {
                                supported = false;
                                return firstValue;
                            }
                            if (note->text != firstValue.text) result.mixed = true;
                        }
                        return firstValue;
                    }
                    else if constexpr (std::is_same_v<T, EnumField>)
                    {
                        std::vector<EnumField> enumFields;
                        enumFields.reserve(fields.size());
                        for (const auto& field : fields)
                        {
                            const auto* enumField = std::get_if<EnumField>(&field.value);
                            if (enumField == nullptr || !enumOptionsMatch(firstValue, *enumField))
                            {
                                supported = false;
                                return firstValue;
                            }
                            enumFields.push_back(*enumField);
                        }

                        result.mixed = std::ranges::any_of(enumFields, [&firstValue](const EnumField& field) {
                            return !enumValueEquals(firstValue, field);
                        });

                        EnumField aggregate = firstValue;
                        aggregate.setIndex = [enumFields](const std::size_t index) {
                            for (const auto& field : enumFields)
                            {
                                if (field.setIndex) field.setIndex(index);
                            }
                        };
                        return aggregate;
                    }
                    else
                    {
                        std::vector<T> leafFields;
                        leafFields.reserve(fields.size());
                        for (const auto& field : fields)
                        {
                            const auto* leaf = std::get_if<T>(&field.value);
                            if (leaf == nullptr)
                            {
                                supported = false;
                                return firstValue;
                            }
                            leafFields.push_back(*leaf);
                        }

                        result.mixed = std::ranges::any_of(leafFields, [&firstValue](const T& field) {
                            return !leafValueEquals(firstValue, field);
                        });

                        T aggregate = firstValue;
                        aggregate.setter = [leafFields](const auto& value) {
                            for (const auto& field : leafFields)
                            {
                                commitLeaf(field, value);
                            }
                        };

                        // For spatial vector fields, additionally expose per-axis info so
                        // the inspector can show "-" only on axes that differ and edit one
                        // axis across the whole selection without disturbing the others.
                        if constexpr (std::is_same_v<T, LeafField<Vector3>>)
                        {
                            populateVectorAggregate(
                                aggregate, leafFields, std::array{&Vector3::x, &Vector3::y, &Vector3::z});
                        }
                        else if constexpr (std::is_same_v<T, LeafField<Vector2>>)
                        {
                            populateVectorAggregate(aggregate, leafFields, std::array{&Vector2::x, &Vector2::y});
                        }
                        return aggregate;
                    }
                },
                fields.front().value);

            if (!supported) return std::nullopt;
            return result;
        }
    } // namespace

    void ComponentInspector::field(const std::string& label, sage::CollisionLayer& v, const bool ed)
    {
        EnumField e;
        const auto& layers = GetCollisionLayers();
        e.options.reserve(layers.size());
        for (const auto& layer : layers)
            e.options.emplace_back(layer.layerName);
        e.getIndex = [p = &v]() -> std::size_t {
            const auto& list = GetCollisionLayers();
            for (std::size_t i = 0; i < list.size(); ++i)
            {
                if (list.at(i).bit == p->bit) return i;
            }
            return 0;
        };
        e.setIndex = [p = &v](const std::size_t idx) {
            const auto& list = GetCollisionLayers();
            if (idx < list.size()) *p = list.at(idx);
        };
        fields_.push_back({.label = qualified(label), .editable = ed && editableScope_, .value = std::move(e)});
    }

    void ComponentInspector::tagSet(const std::string& label, std::string& tags, const bool ed)
    {
        // A single scene tag chosen from the project's CustomSceneTags, rendered as a
        // dropdown via EnumField (same path as the CollisionLayer field). Index 0 is
        // "(none)"; the remaining options are the project tags, plus the entity's
        // current tag if it isn't one of them, so saved values stay selectable.
        EnumField e;
        e.options.emplace_back("(none)");
        for (const auto& tag : CUSTOM_SCENE_TAGS)
            e.options.emplace_back(tag);

        const auto current = std::string{TrimSceneTag(SceneTagText(tags))};
        if (!current.empty() && std::ranges::find(e.options, current) == e.options.end())
            e.options.push_back(current);

        e.getIndex = [options = e.options, p = &tags]() -> std::size_t {
            const auto cur = std::string{TrimSceneTag(SceneTagText(*p))};
            if (cur.empty()) return 0;
            const auto it = std::ranges::find(options, cur);
            return it != options.end() ? static_cast<std::size_t>(std::distance(options.begin(), it)) : 0;
        };
        e.setIndex = [options = e.options, p = &tags](const std::size_t idx) {
            if (idx == 0 || idx >= options.size())
                p->clear();
            else
                *p = options.at(idx);
        };

        fields_.push_back({.label = qualified(label), .editable = ed && editableScope_, .value = std::move(e)});
    }

    void ComponentInspector::cursorDropdown(const std::string& label, std::string& value, const bool ed)
    {
        // A cursor key chosen from the engine's own cursors plus the project's
        // (sage::CUSTOM_CURSORS), rendered as a dropdown via EnumField (same path as
        // tagSet). The current value is appended if it isn't one of the known keys,
        // so saved values stay selectable.
        EnumField e;
        const auto addOption = [&e](const std::string_view key) {
            const auto resolved = ResourceManager::GetInstance().ResolveImageKey(std::string(key));
            if (std::ranges::find(e.options, resolved) == e.options.end()) e.options.push_back(resolved);
        };
        addOption(cursors::REGULAR);
        addOption(cursors::MOVE);
        addOption(cursors::DENIED);
        for (const auto& key : CUSTOM_CURSORS)
            addOption(key);
        if (!value.empty()) addOption(value);
        e.displayOptions = AssetLabels(e.options);

        e.getIndex = [options = e.options, p = &value]() -> std::size_t {
            const auto resolved = ResourceManager::GetInstance().ResolveImageKey(*p);
            const auto it = std::ranges::find(options, resolved);
            return it != options.end() ? static_cast<std::size_t>(std::distance(options.begin(), it)) : 0;
        };
        e.setIndex = [options = e.options, p = &value](const std::size_t idx) {
            if (idx < options.size()) *p = options.at(idx);
        };

        fields_.push_back({.label = qualified(label), .editable = ed && editableScope_, .value = std::move(e)});
    }

    void ComponentInspector::stringDropdown(
        const std::string& label, std::string& value, std::vector<std::string> options, const bool editable)
    {
        options.insert(options.begin(), "(none)");
        if (!value.empty() && std::ranges::find(options, value) == options.end()) options.push_back(value);

        EnumField field{.options = std::move(options)};
        field.displayOptions = AssetLabels(field.options);
        field.getIndex = [&value, options = field.options]() {
            const auto& selected = value.empty() ? options.front() : value;
            const auto found = std::ranges::find(options, selected);
            return found == options.end() ? std::size_t{0} : static_cast<std::size_t>(found - options.begin());
        };
        field.setIndex = [&value, options = field.options](const std::size_t index) {
            if (index < options.size()) value = index == 0 ? std::string{} : options.at(index);
        };
        fields_.push_back(
            {.label = qualified(label), .editable = editable && editableScope_, .value = std::move(field)});
    }

    void ComponentInspector::textureDropdown(const std::string& label, std::string& value, const bool rw)
    {
        stringDropdown(label, value, ResourceManager::GetInstance().GetImageKeys("T_"), rw);
    }

    void ComponentInspector::particleTextureDropdown(const std::string& label, std::string& value, const bool rw)
    {
        std::vector<std::string> options;
        const std::filesystem::path directory{PARTICLE_TEXTURE_DIRECTORY};
        if (std::filesystem::exists(directory))
            for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
                if (entry.is_regular_file() && entry.path().extension() == ".png")
                    options.push_back(entry.path().lexically_relative(directory).generic_string());
        std::ranges::sort(options);
        stringDropdown(label, value, std::move(options), rw);
    }

    void ComponentInspector::archetypeDropdown(const std::string& label, sage::Archetype& v, const bool ed)
    {
        // A single archetype ("kind") chosen from the project's CustomArchetypes,
        // rendered as a dropdown via EnumField (same path as the CollisionLayer
        // field). Index 0 is "(none)" — an unset/invalid id; the rest are the
        // project kinds. Matched and stored by id so the display name need not be
        // persisted on the component.
        EnumField e;
        e.options.emplace_back("(none)");
        for (const auto& archetype : CUSTOM_ARCHETYPES)
            e.options.emplace_back(archetype.name);

        e.getIndex = [p = &v]() -> std::size_t {
            if (!p->IsValid()) return 0;
            for (std::size_t i = 0; i < CUSTOM_ARCHETYPES.size(); ++i)
            {
                if (CUSTOM_ARCHETYPES.at(i).id == p->id) return i + 1;
            }
            return 0;
        };
        e.setIndex = [registry = contextRegistry_, entity = contextEntity_, p = &v](const std::size_t idx) {
            const Archetype selected =
                idx == 0 || idx > CUSTOM_ARCHETYPES.size() ? sage::Archetype{} : CUSTOM_ARCHETYPES.at(idx - 1);
            if (registry && entity != entt::null && registry->get().valid(entity) &&
                registry->get().all_of<sage::Archetype>(entity))
            {
                sage::SetArchetype(registry->get(), entity, selected);
                return;
            }

            *p = selected;
        };

        fields_.push_back({.label = qualified(label), .editable = ed && editableScope_, .value = std::move(e)});
    }

    void ComponentInspector::clipDropdown(const std::string& label, std::string& value, const bool rw)
    {
        EnumField e;
        if (contextRegistry_.has_value() && contextEntity_ != entt::null)
        {
            if (const auto* animation = contextRegistry_->get().try_get<Animation>(contextEntity_))
            {
                e.options = animation->clipNames;
            }
        }

        const bool hasClips = !e.options.empty();
        if (!hasClips)
        {
            e.options.emplace_back("---");
        }
        else if (!value.empty() && std::ranges::find(e.options, value) == e.options.end())
        {
            // Keep a configured value that matches no clip visible and selected
            // rather than silently displaying the first clip.
            e.options.insert(e.options.begin(), value);
        }

        e.getIndex = [options = e.options, p = &value]() -> std::size_t {
            const auto it = std::ranges::find(options, *p);
            return it != options.end() ? static_cast<std::size_t>(std::distance(options.begin(), it)) : 0;
        };
        e.setIndex = [options = e.options, hasClips, p = &value](const std::size_t idx) {
            if (!hasClips || idx >= options.size()) return;
            *p = options.at(idx);
        };

        fields_.push_back({.label = qualified(label), .editable = rw && editableScope_, .value = std::move(e)});
    }

    std::optional<std::reference_wrapper<const InspectorRegistry::Entry>> InspectorRegistry::findEntry(
        const EditorComponentId componentId) const
    {
        const auto it = std::ranges::find_if(
            entries_, [componentId](const Entry& entry) { return entry.componentId == componentId; });
        if (it == entries_.end()) return std::nullopt;
        return std::cref(*it);
    }

    std::vector<InspectorRegistry::DescribedEntry> InspectorRegistry::describeEntity(
        entt::registry& registry, const entt::entity entity) const
    {
        std::vector<DescribedEntry> result;
        for (const auto& entry : entries_)
        {
            if (!entry.has(registry, entity)) continue;
            result.push_back({.entry = std::cref(entry), .description = entry.describe(registry, entity, false)});
        }
        return result;
    }

    std::optional<std::reference_wrapper<InspectorRegistry::DescribedEntry>> InspectorRegistry::findDescribed(
        std::vector<DescribedEntry>& described, const Entry& entry)
    {
        const auto it = std::ranges::find_if(
            described, [&entry](const DescribedEntry& candidate) { return &candidate.entry.get() == &entry; });
        if (it == described.end()) return std::nullopt;
        return std::ref(*it);
    }

    std::optional<std::reference_wrapper<const InspectorRegistry::DescribedEntry>> InspectorRegistry::
        findDescribed(const std::vector<DescribedEntry>& described, const Entry& entry)
    {
        const auto it = std::ranges::find_if(
            described, [&entry](const DescribedEntry& candidate) { return &candidate.entry.get() == &entry; });
        if (it == described.end()) return std::nullopt;
        return std::ref(*it);
    }

    ComponentRemovalState InspectorRegistry::canRemoveFromDescription(
        const Entry& target, const std::vector<DescribedEntry>& described, const bool multiSelection) const
    {
        if (!target.removable) return {.allowed = false, .blockedReason = "Component is protected"};

        for (const auto& dependent : described)
        {
            if (dependent.entry.get().componentId == target.componentId) continue;

            for (const auto requiredComponentId : dependent.entry.get().requirements)
            {
                if (requiredComponentId != target.componentId) continue;

                auto reason = "Required by " + dependent.entry.get().displayName;
                if (multiSelection)
                {
                    reason += " on one or more selected entities";
                }
                return {.allowed = false, .blockedReason = std::move(reason)};
            }
        }

        return {.allowed = true};
    }

    ComponentAddState InspectorRegistry::canAddToDescription(
        const Entry& target, const std::vector<DescribedEntry>& described, const bool multiSelection) const
    {
        auto componentName = [this](const EditorComponentId componentId) {
            if (const auto entry = findEntry(componentId)) return entry->get().displayName;
            return std::string{"Unknown component"};
        };
        auto withSelectionContext = [multiSelection](std::string reason) {
            if (multiSelection) reason += " on one or more selected entities";
            return reason;
        };
        auto hasComponent = [&described](const EditorComponentId componentId) {
            return std::ranges::any_of(described, [componentId](const DescribedEntry& candidate) {
                return candidate.entry.get().componentId == componentId;
            });
        };

        for (const auto requiredComponentId : target.requirements)
        {
            if (hasComponent(requiredComponentId)) continue;
            return {
                .allowed = false,
                .blockedReason = withSelectionContext("Requires " + componentName(requiredComponentId))};
        }

        for (const auto incompatibleComponentId : target.incompatibleComponents)
        {
            if (!hasComponent(incompatibleComponentId)) continue;
            return {
                .allowed = false,
                .blockedReason =
                    withSelectionContext("Incompatible with " + componentName(incompatibleComponentId))};
        }

        for (const auto& existing : described)
        {
            for (const auto incompatibleComponentId : existing.entry.get().incompatibleComponents)
            {
                if (incompatibleComponentId != target.componentId) continue;
                return {
                    .allowed = false,
                    .blockedReason =
                        withSelectionContext("Incompatible with " + existing.entry.get().displayName)};
            }
        }

        return {.allowed = true};
    }

    ComponentAddState InspectorRegistry::CanAdd(
        entt::registry& registry,
        const EditorComponentId componentId,
        const std::vector<entt::entity>& entities) const
    {
        const auto target = findEntry(componentId);
        if (!target) return {.allowed = false, .blockedReason = "Unknown component"};

        bool foundValidEntity = false;
        bool foundEntityToAdd = false;
        for (const auto entity : entities)
        {
            if (!registry.valid(entity)) continue;
            foundValidEntity = true;
            if (target->get().has(registry, entity)) continue;
            foundEntityToAdd = true;

            const auto described = describeEntity(registry, entity);
            const auto add = canAddToDescription(target->get(), described, entities.size() > 1);
            if (!add.allowed) return add;
        }

        if (!foundValidEntity) return {.allowed = false, .blockedReason = "No valid entity selected"};
        if (!foundEntityToAdd) return {.allowed = false, .blockedReason = "Component is already present"};
        return {.allowed = true};
    }

    std::vector<InspectorRegistry::ComponentOption> InspectorRegistry::AddableComponents() const
    {
        std::vector<ComponentOption> options;
        for (const auto& entry : entries_)
        {
            if (entry.addable)
                options.push_back({.componentId = entry.componentId, .displayName = entry.displayName});
        }
        return options;
    }

    bool InspectorRegistry::Add(
        entt::registry& registry, const EditorComponentId componentId, const entt::entity entity) const
    {
        const auto entry = findEntry(componentId);
        if (!entry || !entry->get().addable || entry->get().has(registry, entity)) return false;
        entry->get().add(registry, entity);
        return true;
    }

    bool InspectorRegistry::Remove(
        entt::registry& registry, const EditorComponentId componentId, const entt::entity entity) const
    {
        const auto entry = findEntry(componentId);
        if (!entry || !entry->get().removable || !entry->get().has(registry, entity)) return false;
        entry->get().remove(registry, entity);
        return true;
    }

    std::vector<InspectorRegistry::PersistentComponent> InspectorRegistry::CapturePersistent(
        const entt::registry& registry, const entt::entity entity) const
    {
        std::vector<PersistentComponent> components;
        for (const auto& entry : entries_)
        {
            if (entry.persistenceKey.empty() || !entry.has(registry, entity)) continue;
            components.push_back({.key = entry.persistenceKey, .data = entry.serialize(registry, entity)});
        }
        return components;
    }

    void InspectorRegistry::RestorePersistent(
        entt::registry& registry,
        const entt::entity entity,
        const std::vector<PersistentComponent>& components) const
    {
        for (const auto& entry : entries_)
        {
            if (entry.persistenceKey.empty()) continue;
            const auto component = std::ranges::find(components, entry.persistenceKey, &PersistentComponent::key);
            if (component != components.end())
                entry.deserialize(registry, entity, component->data);
            else if (entry.has(registry, entity))
                entry.remove(registry, entity);
        }
    }

    ComponentRemovalState InspectorRegistry::CanRemove(
        entt::registry& registry,
        const EditorComponentId componentId,
        const std::vector<entt::entity>& entities) const
    {
        const auto target = findEntry(componentId);
        if (!target) return {.allowed = false, .blockedReason = "Unknown component"};

        bool presentOnAnyEntity = false;
        for (const auto entity : entities)
        {
            if (!registry.valid(entity) || !target->get().has(registry, entity)) continue;
            presentOnAnyEntity = true;

            const auto described = describeEntity(registry, entity);
            const auto removal = canRemoveFromDescription(target->get(), described, entities.size() > 1);
            if (!removal.allowed) return removal;
        }

        if (!presentOnAnyEntity) return {.allowed = false, .blockedReason = "Component is not present"};
        return {.allowed = true};
    }

    std::vector<InspectedComponent> InspectorRegistry::InspectRuntime(
        entt::registry& registry, const entt::entity entity) const
    {
        std::vector<InspectedComponent> result;
        for (const auto& entry : entries_)
        {
            if (!entry.has(registry, entity)) continue;
            auto description = entry.describe(registry, entity, true);
            result.push_back(
                {.componentId = entry.componentId,
                 .displayName = entry.displayName,
                 .fields = std::move(description.fields)});
        }
        return SnapshotInspectorComponents(std::move(result));
    }

    std::vector<InspectedComponent> InspectorRegistry::Inspect(
        entt::registry& registry, const entt::entity entity) const
    {
        std::vector<InspectedComponent> result;
        const auto described = describeEntity(registry, entity);
        result.reserve(described.size());
        for (auto& component : described)
        {
            const auto removal = canRemoveFromDescription(component.entry.get(), described, false);
            const auto modelPicker = component.entry.get().componentId == ComponentIdOf<Renderable>()
                                         ? DescribeModelPicker(registry, {entity})
                                         : std::nullopt;
            const auto materialPickers = component.entry.get().componentId == ComponentIdOf<Renderable>()
                                             ? DescribeMaterialPickers(registry, {entity})
                                             : std::vector<MaterialPickerField>{};
            result.push_back(
                {.componentId = component.entry.get().componentId,
                 .displayName = component.entry.get().displayName,
                 .fields = std::move(component.description.fields),
                 .removable = component.entry.get().removable,
                 .removeAllowed = removal.allowed,
                 .removeBlockedReason = removal.blockedReason,
                 .modelPicker = modelPicker,
                 .materialPickers = materialPickers,
                 .modelDefaultsAvailable = HasCatalogModel(modelPicker)});
        }
        return result;
    }

    std::vector<InspectedComponent> InspectorRegistry::Inspect(
        entt::registry& registry, const std::vector<entt::entity>& entities) const
    {
        if (entities.empty()) return {};
        if (entities.size() == 1) return Inspect(registry, entities.front());

        std::vector<std::vector<DescribedEntry>> describedByEntity;
        describedByEntity.reserve(entities.size());
        for (const auto entity : entities)
        {
            describedByEntity.push_back(describeEntity(registry, entity));
        }

        std::vector<InspectedComponent> result;
        for (const auto& entry : entries_)
        {
            if (!std::ranges::all_of(describedByEntity, [&entry](const std::vector<DescribedEntry>& described) {
                    return findDescribed(described, entry).has_value();
                }))
            {
                continue;
            }

            std::vector<std::vector<InspectorField>> describedFields;
            describedFields.reserve(entities.size());
            ComponentRemovalState removal{.allowed = true};
            for (auto& described : describedByEntity)
            {
                auto component = findDescribed(described, entry);
                describedFields.push_back(std::move(component->get().description.fields));
                if (removal.allowed)
                {
                    removal = canRemoveFromDescription(entry, described, true);
                }
            }

            const auto modelPicker = entry.componentId == ComponentIdOf<Renderable>()
                                         ? DescribeModelPicker(registry, entities)
                                         : std::nullopt;
            const auto materialPickers = entry.componentId == ComponentIdOf<Renderable>()
                                             ? DescribeMaterialPickers(registry, entities)
                                             : std::vector<MaterialPickerField>{};
            InspectedComponent component{
                .componentId = entry.componentId,
                .displayName = entry.displayName,
                .removable = entry.removable,
                .removeAllowed = removal.allowed,
                .removeBlockedReason = removal.blockedReason,
                .modelPicker = modelPicker,
                .materialPickers = materialPickers,
                .modelDefaultsAvailable = HasCatalogModel(modelPicker)};
            for (const auto& firstField : describedFields.front())
            {
                std::vector<InspectorField> matchingFields;
                matchingFields.push_back(firstField);

                bool commonField = true;
                for (std::size_t i = 1; i < describedFields.size(); ++i)
                {
                    const auto& fields = describedFields.at(i);
                    const auto it = std::ranges::find_if(fields, [&firstField](const InspectorField& candidate) {
                        return candidate.label == firstField.label &&
                               candidate.value.index() == firstField.value.index();
                    });
                    if (it == fields.end())
                    {
                        commonField = false;
                        break;
                    }
                    matchingFields.push_back(*it);
                }

                if (!commonField) continue;
                if (auto aggregate = aggregateMatchingFields(matchingFields); aggregate.has_value())
                {
                    component.fields.push_back(std::move(*aggregate));
                }
            }

            if (!component.fields.empty())
            {
                result.push_back(std::move(component));
            }
        }

        return result;
    }

    void RegisterDefaultInspectorComponents(InspectorRegistry& registry)
    {
        // Keep the editor identity first; it is the user's primary handle for scene objects.
        registry.Register<sgTransform>("Transform");
        registry.RegisterPersistent<EntityVisibility>("Visibility", "sage.EntityVisibility", true, true);
        registry.Register<PersistentEntityId>("Persistent Entity Id");
        registry.Register<AssetReference>("Asset Reference", true);
        registry.Register<MetaData>("Meta Data");
        registry.Register<Renderable>("Renderable", true, true);
        registry.RegisterPersistent<CustomShaderComponent>("Custom Shader", "sage.CustomShader", true, true);
        registry.RegisterPersistent<ParticleEmitterComponent>(
            "Particle Emitter", "sage.ParticleEmitter", true, true);
        registry.RegisterPersistent<ParticleSystemComponent>("Particle System", "sage.ParticleSystem", true, true);
        registry.Register<Collideable>("Collideable", true, true);
        registry.Register<NavigationSurface>("Navigation Surface", true, true);
        registry.Register<NavigationObstacle>("Navigation Obstacle", true, true);
        registry.Register<TriggerVolume>("Trigger Volume", true, true);
        registry.Register<CursorTarget>("Cursor Target", true, true);
        registry.Register<Light>("Light", true, true);
        registry.Register<Animation>("Animation", true, true);
        registry.Register<MoveableActor>("Moveable Actor", true, true);
        registry.Register<ScriptComponent>("C# Script", true, true);
        registry.Register<UINode>("UI Node");
        registry.Register<Archetype>("Archetype", true, true);
    }
} // namespace sage::editor
