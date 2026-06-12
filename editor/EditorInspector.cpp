#include "EditorInspector.hpp"

#include "EditorComponents.hpp"
#include "engine/CollisionLayers.hpp"
#include "engine/components/Animation.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/MoveableActor.hpp"
#include "engine/components/Renderable.hpp"
#include "engine/components/ScriptComponent.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/components/Spawner.hpp"
#include "engine/Light.hpp"
#include "engine/SceneTags.hpp"
#include "project/CustomSceneTags.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <utility>

namespace sage::editor
{
    namespace
    {
        template <class T>
        bool leafValueEquals(const LeafField<T>& lhs, const LeafField<T>& rhs)
        {
            if (lhs.data == nullptr || rhs.data == nullptr) return lhs.data == rhs.data;
            return *lhs.data == *rhs.data;
        }

        bool leafValueEquals(const LeafField<Vector2>& lhs, const LeafField<Vector2>& rhs)
        {
            if (lhs.data == nullptr || rhs.data == nullptr) return lhs.data == rhs.data;
            return lhs.data->x == rhs.data->x && lhs.data->y == rhs.data->y;
        }

        bool leafValueEquals(const LeafField<Vector3>& lhs, const LeafField<Vector3>& rhs)
        {
            if (lhs.data == nullptr || rhs.data == nullptr) return lhs.data == rhs.data;
            return lhs.data->x == rhs.data->x && lhs.data->y == rhs.data->y && lhs.data->z == rhs.data->z;
        }

        bool leafValueEquals(const LeafField<::Color>& lhs, const LeafField<::Color>& rhs)
        {
            if (lhs.data == nullptr || rhs.data == nullptr) return lhs.data == rhs.data;
            return lhs.data->r == rhs.data->r && lhs.data->g == rhs.data->g && lhs.data->b == rhs.data->b &&
                   lhs.data->a == rhs.data->a;
        }

        template <class T>
        void commitLeaf(const LeafField<T>& field, const T& value)
        {
            if (field.setter)
                field.setter(value);
            else if (field.data != nullptr)
                *field.data = value;
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
                const float base = leafFields.front().data ? leafFields.front().data->*axes[axis] : 0.0f;
                const bool differs = std::ranges::any_of(leafFields, [&](const LeafField<VecT>& field) {
                    return (field.data ? field.data->*axes[axis] : 0.0f) != base;
                });
                if (differs) aggregate.mixedComponents |= 1u << axis;
            }
            aggregate.componentSetter = [leafFields, axes](const std::size_t axis, const float value) {
                for (const auto& field : leafFields)
                {
                    VecT v = field.data ? *field.data : VecT{};
                    v.*axes[axis] = value;
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
            result.editable = std::ranges::all_of(fields, [](const InspectorField& field) {
                return field.editable;
            });

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
                    if constexpr (std::is_same_v<T, NoteField>)
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
        EnumField e{.data = &v};
        const auto& layers = GetCollisionLayers();
        e.options.reserve(layers.size());
        for (const auto& layer : layers)
            e.options.emplace_back(layer.layerName);
        e.getIndex = [p = &v]() -> std::size_t {
            const auto& list = GetCollisionLayers();
            for (std::size_t i = 0; i < list.size(); ++i)
            {
                if (list[i].bit == p->bit) return i;
            }
            return 0;
        };
        e.setIndex = [p = &v](const std::size_t idx) {
            const auto& list = GetCollisionLayers();
            if (idx < list.size()) *p = list[idx];
        };
        fields_.push_back({.label = qualified(label), .editable = ed && editableScope_, .value = std::move(e)});
    }

    void ComponentInspector::tagSet(const std::string& label, std::string& tags, const bool ed)
    {
        // A single scene tag chosen from the project's CustomSceneTags, rendered as a
        // dropdown via EnumField (same path as the CollisionLayer field). Index 0 is
        // "(none)"; the remaining options are the project tags, plus the entity's
        // current tag if it isn't one of them (so legacy values stay selectable).
        EnumField e{.data = &tags};
        e.options.emplace_back("(none)");
        for (const auto& tag : CustomSceneTags)
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
                *p = options[idx];
        };

        fields_.push_back({.label = qualified(label), .editable = ed && editableScope_, .value = std::move(e)});
    }

    void ComponentInspector::clipDropdown(const std::string& label, std::string& value, const bool rw)
    {
        EnumField e{.data = &value};
        if (contextRegistry_ != nullptr && contextEntity_ != entt::null)
        {
            if (const auto* animation = contextRegistry_->try_get<Animation>(contextEntity_))
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
            // Keep an authored value that matches no clip visible and selected
            // rather than silently displaying the first clip.
            e.options.insert(e.options.begin(), value);
        }

        e.getIndex = [options = e.options, p = &value]() -> std::size_t {
            const auto it = std::ranges::find(options, *p);
            return it != options.end() ? static_cast<std::size_t>(std::distance(options.begin(), it)) : 0;
        };
        e.setIndex = [options = e.options, hasClips, p = &value](const std::size_t idx) {
            if (!hasClips || idx >= options.size()) return;
            *p = options[idx];
        };

        fields_.push_back({.label = qualified(label), .editable = rw && editableScope_, .value = std::move(e)});
    }

    std::vector<InspectedComponent> InspectorRegistry::Inspect(
        entt::registry& registry, const entt::entity entity) const
    {
        std::vector<InspectedComponent> result;
        for (const auto& entry : entries_)
        {
            if (!entry.has(registry, entity)) continue;
            result.push_back({entry.displayName, entry.describe(registry, entity), entry.removable});
        }
        return result;
    }

    std::vector<InspectedComponent> InspectorRegistry::Inspect(
        entt::registry& registry, const std::vector<entt::entity>& entities) const
    {
        if (entities.empty()) return {};
        if (entities.size() == 1) return Inspect(registry, entities.front());

        std::vector<InspectedComponent> result;
        for (const auto& entry : entries_)
        {
            if (!std::ranges::all_of(entities, [&registry, &entry](const entt::entity entity) {
                    return entry.has(registry, entity);
                }))
            {
                continue;
            }

            std::vector<std::vector<InspectorField>> describedFields;
            describedFields.reserve(entities.size());
            for (const auto entity : entities)
            {
                describedFields.push_back(entry.describe(registry, entity));
            }

            InspectedComponent component{.displayName = entry.displayName, .removable = entry.removable};
            for (const auto& firstField : describedFields.front())
            {
                std::vector<InspectorField> matchingFields;
                matchingFields.push_back(firstField);

                bool commonField = true;
                for (std::size_t i = 1; i < describedFields.size(); ++i)
                {
                    const auto& fields = describedFields[i];
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
        registry.Register<PersistentEntityId>("Persistent Entity Id");
        registry.Register<AssetReference>("Asset Reference");
        registry.Register<MetaData>("Meta Data");
        registry.Register<Renderable>("Renderable");
        registry.Register<Collideable>("Collideable");
        registry.Register<Light>("Light");
        registry.Register<Spawner>("Spawner");
        registry.Register<Animation>("Animation", /*removable=*/true);
        registry.Register<MoveableActor>("Moveable Actor", /*removable=*/true);
        registry.Register<ScriptComponent>("Script", /*removable=*/true);
    }
} // namespace sage::editor
