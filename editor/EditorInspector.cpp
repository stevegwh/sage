#include "EditorInspector.hpp"

#include "EditorComponents.hpp"
#include "engine/CollisionLayers.hpp"
#include "engine/components/Collideable.hpp"
#include "engine/components/Renderable.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/components/Spawner.hpp"
#include "engine/Light.hpp"
#include "engine/SceneTags.hpp"

#include <algorithm>
#include <optional>
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
                    if constexpr (std::is_same_v<T, EnumField>)
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

    std::vector<InspectedComponent> InspectorRegistry::Inspect(
        entt::registry& registry, const entt::entity entity) const
    {
        std::vector<InspectedComponent> result;
        for (const auto& entry : entries_)
        {
            if (!entry.has(registry, entity)) continue;
            result.push_back({entry.displayName, entry.describe(registry, entity)});
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

            InspectedComponent component{.displayName = entry.displayName};
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
    }
} // namespace sage::editor
