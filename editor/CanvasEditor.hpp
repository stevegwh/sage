#pragma once
#include "CSharpScriptEditorConfig.hpp"
#include "engine/ui/CanvasDocument.hpp"
#include "entt/fwd.hpp"
#include <map>
#include <optional>

namespace sage
{
    struct CanvasLayout;
}

namespace sage::editor
{
    class CanvasEditor
    {
        // Document session and history.
        CanvasDocument document;
        std::filesystem::path path;
        std::string pathInput;
        std::string newScriptName;
        unsigned int selected = 1;
        bool active = false;
        bool workspaceVisible = false;
        bool sceneUIOpen = false;
        std::optional<std::filesystem::path> pendingOpen;
        bool closeRequested = false;
        std::string error;
        std::string saved;
        std::vector<std::string> undo;
        std::vector<std::string> redo;
        std::optional<std::string> transaction;
        bool historyChanged = false;
        CSharpScriptEditorConfig scripts;
        // Preview and pointer gestures.
        RenderTexture2D preview{};
        Vector2 dragStart{};
        Rectangle dragBounds{};
        bool moving = false;
        bool resizing = false;
        unsigned int dividerNeighbor = 0;
        float dividerSpace = 0;
        float dividerPercent = 0;
        float dividerNeighborPercent = 0;
        float previewWidth = 1920;
        float previewHeight = 1080;
        // Resources available to the inspector.
        std::vector<std::filesystem::path> textures;
        std::vector<std::filesystem::path> fonts;
        std::map<std::string, std::filesystem::path> scriptSources;
        std::string state() const;
        void restore(const std::string& value);
        void scanAssets();
        void beginEditing();
        void hierarchy(unsigned int id);
        void inspector();
        void drawCanvasProperties();
        void drawWindowProperties(CanvasNode& node);
        void drawTableProperties(CanvasNode& node);
        void drawRowProperties(CanvasNode& node);
        void drawCellProperties(CanvasNode& node);
        void drawBackgroundProperties(CanvasNode& node);
        void drawStructureActions(unsigned int id);
        void drawBehaviour(CanvasNode& node);
        void drawScriptIssues();
        static std::vector<std::string> exposedFields(const std::filesystem::path& path);
        static std::vector<std::filesystem::path> canvasFiles();
        void drawToolbar();
        void drawCloseDialog();
        void drawPreview();
        void beginPreviewDrag(const CanvasLayout& layout, Vector2 mouse);
        void updatePreviewDrag(Vector2 mouse, float scale);
        void save();
        void finishClose();
        void history(bool forward);

      public:
        explicit CanvasEditor(CSharpScriptEditorConfig config);
        ~CanvasEditor();
        bool IsActive() const
        {
            return active && workspaceVisible;
        }
        bool IsDirty() const
        {
            return active && state() != saved;
        }
        bool HasDocument() const
        {
            return active;
        }
        const std::filesystem::path& Path() const
        {
            return path;
        }
        void Resume()
        {
            workspaceVisible = active;
        }
        void ShowScene()
        {
            workspaceVisible = false;
        }
        void New();
        void Open(const std::filesystem::path& file);
        void Draw();
        json::Document Inspect() const;
        json::Document Command(const json::Value& request);
        void RequestClose();
        void DrawSceneMenu(bool enabled);
        void DrawSceneUI(entt::registry& registry, const std::function<void()>& sceneChanged);
    };
} // namespace sage::editor
