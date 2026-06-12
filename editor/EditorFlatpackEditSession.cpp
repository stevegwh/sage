#include "EditorFlatpackEditSession.hpp"

#include "EditorFlatpack.hpp"
#include "EditorHistory.hpp"
#include "EditorMapLoader.hpp"
#include "engine/Camera.hpp"
#include "engine/EngineSystems.hpp"

#include "imgui.h"
#include "raylib.h"

#include <format>
#include <iostream>
#include <utility>

namespace sage::editor
{
    namespace
    {
        constexpr float SAVE_FEEDBACK_SECONDS = 2.5f;
        constexpr const char* CLOSE_POPUP_ID = "Close Flatpack";

        std::filesystem::path mapStashPath()
        {
            return std::filesystem::temp_directory_path() / "hero-herder-editor-map-stash.map";
        }
    } // namespace

    EditorFlatpackEditSession::EditorFlatpackEditSession(
        EngineSystems* _sys, EditorHistory* _history, Callbacks _callbacks)
        : sys(_sys), history(_history), callbacks(std::move(_callbacks))
    {
    }

    void EditorFlatpackEditSession::Update()
    {
        if (saveFeedbackRemaining > 0.0f)
        {
            saveFeedbackRemaining = std::max(0.0f, saveFeedbackRemaining - GetFrameTime());
        }
    }

    void EditorFlatpackEditSession::Open(std::filesystem::path path)
    {
        if (active)
        {
            if (path == flatpackPath) return;
            pendingOpenPath = std::move(path);
            RequestClose();
            return;
        }
        open(path);
    }

    void EditorFlatpackEditSession::open(const std::filesystem::path& path)
    {
        if (!IsFlatpackFile(path.string().c_str()))
        {
            std::cerr << "ERROR: Not a flatpack file: " << path << std::endl;
            return;
        }

        // Stash the map session (scene, camera, dirty flag) so closing the
        // flatpack restores it exactly, unsaved changes included.
        std::vector<entt::entity> hierarchyOrder;
        if (callbacks.prepareMapStash) hierarchyOrder = callbacks.prepareMapStash();
        stashPath = mapStashPath();
        SaveMap(*sys->registry, stashPath.string().c_str(), hierarchyOrder);
        stashedMapDirty = history && history->HasUnsavedChanges();
        stashedCamera = *sys->camera->getRaylibCam();

        if (callbacks.clearScene) callbacks.clearScene();

        root = callbacks.loadFlatpack ? callbacks.loadFlatpack(path) : entt::null;
        if (root == entt::null)
        {
            std::cerr << "ERROR: Failed to open flatpack for editing: " << path << std::endl;
            restoreStashedMap();
            return;
        }

        flatpackPath = path;
        active = true;
        saveFeedbackRemaining = 0.0f;
        saveFeedbackStatus.clear();
        if (history) history->MarkSaved();
        if (callbacks.setSceneName) callbacks.setSceneName(std::format("Flatpack: {}", FlatpackName()));
        if (callbacks.finishOpen) callbacks.finishOpen();
        sys->camera->FocusEntity(root);
    }

    void EditorFlatpackEditSession::Save()
    {
        if (!active) return;

        if (!SaveFlatpack(*sys->registry, root, flatpackPath.string().c_str()))
        {
            std::cerr << "ERROR: Failed to save flatpack: " << flatpackPath << std::endl;
            return;
        }
        if (history) history->MarkSaved();
        saveFeedbackStatus = std::format("Saved {}", flatpackPath.filename().string());
        saveFeedbackRemaining = SAVE_FEEDBACK_SECONDS;
        if (callbacks.catalogChanged) callbacks.catalogChanged();
    }

    void EditorFlatpackEditSession::RequestClose()
    {
        if (!active) return;
        if (HasUnsavedChanges())
        {
            closePromptRequested = true;
            return;
        }
        close();
    }

    void EditorFlatpackEditSession::close()
    {
        if (callbacks.clearScene) callbacks.clearScene();
        active = false;
        root = entt::null;
        flatpackPath.clear();
        saveFeedbackRemaining = 0.0f;
        saveFeedbackStatus.clear();

        restoreStashedMap();

        if (!pendingOpenPath.empty())
        {
            const auto next = std::exchange(pendingOpenPath, {});
            open(next);
        }
    }

    void EditorFlatpackEditSession::restoreStashedMap()
    {
        if (!stashPath.empty() && std::filesystem::is_regular_file(stashPath))
        {
            LoadMap(sys->registry, stashPath.string().c_str());
            std::filesystem::remove(stashPath);
        }
        stashPath.clear();

        if (callbacks.finishMapRestore) callbacks.finishMapRestore();
        sys->camera->SetCamera(stashedCamera.position, stashedCamera.target);
        // LoadMap leaves the history marked saved; the stash round-trip must not
        // silently launder edits the user made before opening the flatpack.
        if (stashedMapDirty && history) history->MarkDirty();
        stashedMapDirty = false;
    }

    void EditorFlatpackEditSession::DrawCloseConfirmationModal()
    {
        if (closePromptRequested && !ImGui::IsPopupOpen(CLOSE_POPUP_ID))
        {
            ImGui::OpenPopup(CLOSE_POPUP_ID);
        }
        closePromptRequested = false;

        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2{0.5f, 0.5f});
        ImGui::SetNextWindowSize(ImVec2{460.0f, 0.0f}, ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal(
                CLOSE_POPUP_ID, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
        {
            return;
        }

        ImGui::TextWrapped("'%s' has unsaved changes.", FlatpackName().c_str());
        ImGui::Spacing();
        if (ImGui::Button("Save & Close"))
        {
            Save();
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            close();
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard Changes"))
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            close();
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            pendingOpenPath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    bool EditorFlatpackEditSession::IsActive() const
    {
        return active;
    }

    entt::entity EditorFlatpackEditSession::Root() const
    {
        return root;
    }

    const std::filesystem::path& EditorFlatpackEditSession::Path() const
    {
        return flatpackPath;
    }

    std::string EditorFlatpackEditSession::FlatpackName() const
    {
        return flatpackPath.stem().string();
    }

    bool EditorFlatpackEditSession::HasUnsavedChanges() const
    {
        return active && history && history->HasUnsavedChanges();
    }

    bool EditorFlatpackEditSession::StashedMapHadUnsavedChanges() const
    {
        return active && stashedMapDirty;
    }

    std::string EditorFlatpackEditSession::CurrentSaveStatus() const
    {
        if (HasUnsavedChanges()) return "Unsaved changes";
        if (saveFeedbackRemaining > 0.0f) return saveFeedbackStatus;
        return {};
    }
} // namespace sage::editor
