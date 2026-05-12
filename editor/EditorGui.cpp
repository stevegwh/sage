#include "EditorGui.hpp"

#include "engine/GameUiEngine.hpp"
#include "engine/ResourceManager.hpp"
#include "engine/Settings.hpp"
#include "engine/ui/UIElements.hpp"
#include "engine/ui/UILayout.hpp"
#include "engine/ui/UIWindow.hpp"

#include <memory>

namespace sage::editor
{
    void EditorGui::SetPlacementStatus(
        const std::string& selectedAsset, const std::string& hoveredGrid, const std::string& lastPlaced) const
    {
        if (selectedAssetText) selectedAssetText->SetContent("Asset: " + selectedAsset);
        if (gridText) gridText->SetContent("Grid: " + hoveredGrid);
        if (lastPlacedText) lastPlacedText->SetContent("Last: " + lastPlaced);
    }

    EditorGui::EditorGui(GameUIEngine* ui, Settings* settings)
    {
        const auto frame = ResourceManager::GetInstance().TextureLoad("resources/textures/ui/frame.png");
        auto window = std::make_unique<Window>(
            settings,
            frame,
            TextureStretchMode::STRETCH,
            24.0f,
            24.0f,
            360.0f,
            184.0f,
            Padding{20, 16, 14, 14});

        overlayWindow = ui->CreateWindow(std::move(window));
        auto* mainTable = overlayWindow->CreateTable({0, 0, 4, 0});

        {
            auto* titleRow = mainTable->CreateTableRow(18);
            auto* titleCell = titleRow->CreateTableCell();
            auto title = std::make_unique<TitleBar>(ui, titleCell, TextBox::FontInfo{});
            titleCell->CreateTitleBar(std::move(title), "Editor");
        }

        {
            auto* contentRow = mainTable->CreateTableRow({10, 0, 0, 0});
            auto* contentCell = contentRow->CreateTableCell({8, 8, 8, 8});
            auto* table = contentCell->CreateTable();

            auto addLine = [ui, table](const char* text) {
                auto* row = table->CreateTableRow();
                auto* cell = row->CreateTableCell();
                auto label = std::make_unique<TextBox>(ui, cell, TextBox::FontInfo{});
                return cell->CreateTextbox(std::move(label), text);
            };

            addLine("Scene: Grid workspace");
            selectedAssetText = addLine("Asset: None");
            gridText = addLine("Grid: None");
            lastPlacedText = addLine("Last: None");
        }

        overlayWindow->FinalizeLayout();
    }
} // namespace sage::editor
