#pragma once

#include "EditorAssetCatalog.hpp"
#include "EditorGui.hpp"

#include <functional>
#include <string>

namespace sage::editor
{
    struct ModelDefaultsStatus
    {
        std::string assetName;
        float height = 0.0f;
        float rotation = 0.0f;
        float scale = 1.0f;
    };

    class EditorModelDefaultsController
    {
        EditorAssetCatalog& assets;
        std::function<bool()> isActive;
        std::function<void()> onChanged;

        void notifyChanged() const;

      public:
        EditorModelDefaultsController(
            EditorAssetCatalog& assets, std::function<bool()> isActive, std::function<void()> onChanged);

        void AdjustHeight(float amount);
        void AdjustRotation(float amount);
        void AdjustScale(float amount);
        void SetHeight(float value);
        void SetRotation(float value);
        void SetScale(float value);
        void Apply();
        void Reset();

        [[nodiscard]] ModelDefaultsStatus Status(const std::string& inactiveAssetName) const;
        [[nodiscard]] EditorGui::ModelDefaultCallbacks Callbacks();
    };
} // namespace sage::editor
