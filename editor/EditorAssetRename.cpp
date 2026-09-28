#include "EditorAssetRename.hpp"

#include "EditorAssetCatalog.hpp"
#include "EditorComponents.hpp"
#include "engine/components/Renderable.hpp"
#include "engine/components/Animation.hpp"
#include "engine/components/UberShaderComponent.hpp"
#include "engine/content/Json.hpp"
#include "engine/ResourceManager.hpp"
#include "engine/AssetKey.hpp"
#include "engine/slib.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <format>
#include <optional>
#include <regex>
#include <system_error>
#include <utility>
#include <vector>

namespace sage::editor
{
    namespace
    {
        std::string ToUpperAscii(std::string value)
        {
            for (auto& ch : value)
            {
                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            }
            return value;
        }

        bool EqualsIgnoreAsciiCase(std::string lhs, std::string rhs)
        {
            return ToUpperAscii(std::move(lhs)) == ToUpperAscii(std::move(rhs));
        }

        bool IsReservedWindowsDeviceName(const std::string& fileName)
        {
            const auto dot = fileName.find('.');
            const auto base = ToUpperAscii(fileName.substr(0, dot));
            if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL")
            {
                return true;
            }

            if (base.size() == 4)
            {
                const auto prefix = base.substr(0, 3);
                const auto suffix = base[3];
                return (prefix == "COM" || prefix == "LPT") && suffix >= '1' && suffix <= '9';
            }

            return false;
        }

        std::optional<std::string> ValidatePortableFileName(const std::string& fileName)
        {
            if (fileName.empty()) return "File name cannot be empty.";
            if (fileName == "." || fileName == "..") return "File name cannot be '.' or '..'.";
            if (fileName.size() > 255) return "File name must be 255 bytes or fewer.";
            if (fileName.back() == ' ' || fileName.back() == '.')
            {
                return "File name cannot end with a space or a dot.";
            }
            if (IsReservedWindowsDeviceName(fileName))
            {
                return "File name uses a reserved Windows device name.";
            }

            for (const unsigned char ch : fileName)
            {
                if (ch == '\0') return "File name cannot contain NUL.";
                if (ch < 32) return "File name cannot contain control characters.";
                switch (ch)
                {
                case '<':
                case '>':
                case ':':
                case '"':
                case '/':
                case '\\':
                case '|':
                case '?':
                case '*':
                    return "File name contains a character that is illegal on Windows, macOS, or Linux.";
                default:
                    break;
                }
            }

            return std::nullopt;
        }

        bool EquivalentPath(const std::filesystem::path& lhs, const std::filesystem::path& rhs)
        {
            std::error_code ec;
            if (!std::filesystem::exists(lhs, ec) || ec) return false;
            if (!std::filesystem::exists(rhs, ec) || ec) return false;
            const bool equivalent = std::filesystem::equivalent(lhs, rhs, ec);
            return !ec && equivalent;
        }

        std::optional<std::string> CheckTargetCollision(
            const std::filesystem::path& source,
            const std::filesystem::path& target)
        {
            std::error_code ec;
            if (std::filesystem::exists(target, ec) && !EquivalentPath(source, target))
            {
                return std::format("A file named {} already exists.", target.filename().string());
            }
            if (ec)
            {
                return std::format("Could not check target path: {}", ec.message());
            }
            return std::nullopt;
        }

        std::filesystem::path BuildRenameTargetPath(
            const std::filesystem::path& sourcePath,
            const std::string& requestedFileName,
            std::string& error)
        {
            if (const auto validationError = ValidatePortableFileName(requestedFileName);
                validationError.has_value())
            {
                error = *validationError;
                return {};
            }

            std::filesystem::path requestedPath{requestedFileName};
            auto finalFileName = requestedFileName;
            const auto requiredExtension = sourcePath.extension().string();
            if (requestedPath.extension().empty() && !requiredExtension.empty())
            {
                finalFileName += requiredExtension;
            }
            else if (!requiredExtension.empty() &&
                     !EqualsIgnoreAsciiCase(requestedPath.extension().string(), requiredExtension))
            {
                error = std::format("File extension must stay {}.", requiredExtension);
                return {};
            }

            if (const auto validationError = ValidatePortableFileName(finalFileName);
                validationError.has_value())
            {
                error = *validationError;
                return {};
            }

            return sourcePath.parent_path() / finalFileName;
        }

        struct DocumentEdit
        {
            std::filesystem::path path;
            std::filesystem::path stagedPath;
            std::string original;
            std::string updated;
        };

        std::vector<DocumentEdit> FindDocumentReferences(const std::string& oldKey, const std::string& newKey)
        {
            static const std::regex reference{R"asset(("(?:key|modelKey|assetKey)"\s*:\s*")([^"\\]+)("))asset"};
            std::vector<DocumentEdit> edits;
            for (const auto& entry : std::filesystem::recursive_directory_iterator("resources"))
            {
                if (!entry.is_regular_file()) continue;
                const auto extension = entry.path().extension();
                if (extension != ".map" && extension != ".flatpack") continue;

                std::ifstream input(entry.path(), std::ios::binary);
                const std::string original{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
                if (original.find(oldKey) == std::string::npos) continue;

                std::string updated;
                auto last = original.cbegin();
                for (std::sregex_iterator match(original.begin(), original.end(), reference), end; match != end; ++match)
                {
                    updated.append(last, match->prefix().second);
                    updated += (*match)[1].str();
                    updated += (*match)[2].str() == oldKey ? newKey : (*match)[2].str();
                    updated += (*match)[3].str();
                    last = match->suffix().first;
                }
                updated.append(last, original.cend());
                if (updated == original) continue;
                json::Parse(updated);
                edits.push_back({entry.path(), entry.path().string() + ".asset-rename-tmp", original, updated});
            }
            return edits;
        }

        void RemoveStagedEdits(const std::vector<DocumentEdit>& edits)
        {
            for (const auto& edit : edits)
            {
                std::error_code ignored;
                std::filesystem::remove(edit.stagedPath, ignored);
            }
        }
    } // namespace

    EditorGui::AssetRenameResult RenameAssetFile(
        entt::registry& registry,
        EditorAssetCatalog& catalog,
        const std::size_t index,
        const std::string& requestedFileName)
    {
        if (index >= catalog.Size())
        {
            return {.message = "Asset no longer exists."};
        }

        const auto entries = catalog.AssetEntries();
        if (index >= entries.size())
        {
            return {.message = "Asset no longer exists."};
        }

        const auto& entry = entries[index];
        if (entry.sourcePath.empty())
        {
            return {.message = "This asset has no source model file to rename."};
        }

        const auto oldSourcePath = entry.sourcePath;
        std::error_code ec;
        if (!std::filesystem::is_regular_file(oldSourcePath, ec))
        {
            return {.message = std::format("Source file is missing: {}", oldSourcePath.string())};
        }

        std::string error;
        const auto newSourcePath = BuildRenameTargetPath(oldSourcePath, requestedFileName, error);
        if (!error.empty())
        {
            return {.message = error};
        }
        if (EquivalentPath(oldSourcePath, newSourcePath))
        {
            return {.message = "File name is unchanged."};
        }
        if (const auto collision = CheckTargetCollision(oldSourcePath, newSourcePath); collision.has_value())
        {
            return {.message = *collision};
        }

        const auto oldKey = entry.modelKey;
        const auto newKey = AssetKeyForPath(newSourcePath);
        if (newKey.empty())
        {
            return {.message = "File name must have a non-empty stem."};
        }
        if (oldKey != newKey && ResourceManager::GetInstance().HasModelKey(newKey))
        {
            return {.message = std::format("An asset named {} is already loaded.", newKey)};
        }

        const auto oldDefaultsPath = entry.defaultsPath;
        const auto newDefaultsPath = EditorAssetCatalog::AssetDefaultsPathForModelKey(newKey);
        if (const auto collision = CheckTargetCollision(oldDefaultsPath, newDefaultsPath); collision.has_value())
        {
            return {.message = *collision};
        }

        std::vector<DocumentEdit> documentEdits;
        bool stagingStarted = false;
        try
        {
            documentEdits = FindDocumentReferences(oldKey, newKey);
            for (const auto& edit : documentEdits)
                if (std::filesystem::exists(edit.stagedPath))
                    throw std::runtime_error("Staged rename file already exists: " + edit.stagedPath.string());
            stagingStarted = true;
            for (const auto& edit : documentEdits)
            {
                std::ofstream output(edit.stagedPath, std::ios::binary);
                output << edit.updated;
                if (!output) throw std::runtime_error("Could not stage changes to " + edit.path.string());
            }
        }
        catch (const std::exception& exception)
        {
            if (stagingStarted) RemoveStagedEdits(documentEdits);
            return {.message = exception.what()};
        }

        std::filesystem::rename(oldSourcePath, newSourcePath, ec);
        if (ec)
        {
            RemoveStagedEdits(documentEdits);
            return {.message = std::format("Could not rename source file: {}", ec.message())};
        }

        bool movedDefaults = false;
        if (std::filesystem::exists(oldDefaultsPath, ec) && !EquivalentPath(oldDefaultsPath, newDefaultsPath))
        {
            std::filesystem::rename(oldDefaultsPath, newDefaultsPath, ec);
            if (ec)
            {
                std::error_code rollbackEc;
                std::filesystem::rename(newSourcePath, oldSourcePath, rollbackEc);
                RemoveStagedEdits(documentEdits);
                return {.message = std::format("Could not rename defaults file: {}", ec.message())};
            }
            movedDefaults = true;
        }

        if (!ResourceManager::GetInstance().RenameModelAsset(oldKey, newKey, newSourcePath.string()))
        {
            std::error_code rollbackEc;
            if (movedDefaults)
            {
                std::filesystem::rename(newDefaultsPath, oldDefaultsPath, rollbackEc);
            }
            std::filesystem::rename(newSourcePath, oldSourcePath, rollbackEc);
            RemoveStagedEdits(documentEdits);
            return {.message = "Could not update the loaded asset registry."};
        }

        std::size_t committed = 0;
        for (; committed < documentEdits.size(); ++committed)
        {
            std::filesystem::rename(documentEdits[committed].stagedPath, documentEdits[committed].path, ec);
            if (ec) break;
        }
        if (ec)
        {
            for (std::size_t i = 0; i < committed; ++i)
            {
                std::ofstream restore(documentEdits[i].path, std::ios::binary | std::ios::trunc);
                restore << documentEdits[i].original;
            }
            RemoveStagedEdits(documentEdits);
            ResourceManager::GetInstance().RenameModelAsset(newKey, oldKey, oldSourcePath.string());
            std::error_code rollbackEc;
            if (movedDefaults) std::filesystem::rename(newDefaultsPath, oldDefaultsPath, rollbackEc);
            std::filesystem::rename(newSourcePath, oldSourcePath, rollbackEc);
            return {.message = std::format("Could not update saved references: {}", ec.message())};
        }

        catalog.RenameAsset(index, newKey);

        for (const auto entity : registry.view<Animation>())
        {
            auto& animation = registry.get<Animation>(entity);
            if (animation.modelKey == oldKey) animation.modelKey = newKey;
        }

        for (const auto entity : registry.view<AssetReference>())
        {
            auto& assetReference = registry.get<AssetReference>(entity);
            if (assetReference.assetKey == oldKey)
            {
                assetReference.assetKey = newKey;
            }
        }

        for (const auto entity : registry.view<Renderable>())
        {
            auto& renderable = registry.get<Renderable>(entity);
            const auto* model = renderable.GetModel();
            if (model == nullptr || model->GetKey() != oldKey) continue;

            auto replacement = ResourceManager::GetInstance().GetModelView(newKey);
            replacement.SetTransform(renderable.initialTransform);
            renderable.SetModel(std::move(replacement));
            if (registry.any_of<UberShaderComponent>(entity))
            {
                auto& uber = registry.get<UberShaderComponent>(entity);
                if (auto* replacementModel = renderable.GetModel(); replacementModel != nullptr)
                {
                    replacementModel->SetShader(uber.shader);
                }
            }
        }

        const auto updatedEntries = catalog.AssetEntries();
        return {
            .renamed = true,
            .message = std::format("Renamed to {}.", newSourcePath.filename().string()),
            .updatedEntry = updatedEntries.at(index)};
    }
} // namespace sage::editor
