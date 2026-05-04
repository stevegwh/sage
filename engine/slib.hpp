//
// Created by Steve Wheeler on 06/07/2024.
//

#pragma once

#include "raylib-cereal.hpp"
#include "raylib.h"

#include "entt/entt.hpp"
#include <string>

#include <optional>

namespace sage
{
    struct UberShaderComponent;

    class ImageSafe
    {
        Image image{};
        // If Image is a deepCopy, this class will use RAII.
        bool deepCopy = true;

      public:
        [[nodiscard]] const Image& GetImage() const;
        void SetImage(Image& _image);
        [[nodiscard]] Color GetColor(int x, int y) const;
        [[nodiscard]] bool HasLoaded() const;
        [[nodiscard]] int GetWidth() const;
        [[nodiscard]] int GetHeight() const;

        ImageSafe(const ImageSafe&) = delete;
        ImageSafe& operator=(const ImageSafe&) = delete;
        ImageSafe(ImageSafe&& other) noexcept;
        ImageSafe& operator=(ImageSafe&& other) noexcept;

        ~ImageSafe();
        explicit ImageSafe(const Image& _image, bool _deepCopy = true);
        explicit ImageSafe(const std::string& path, bool _deepCopy = true);
        explicit ImageSafe(bool _deepCopy = true);

        template <typename Archive>
        void serialize(Archive& archive)
        {
            archive(image);
        };
    };

    class Renderable;
    class ResourceManager;
    class TextureTerrainOverlay;
    class LightManager;
    class UberShaderSystem;
    class RenderSystem;
    class ResourcePacker;

    /**
     * Non-owning view of a Model whose storage lives in ResourceManager.
     * Read-only public API. SetShader is private (struct-copy-local on shared
     * material entries, but gated behind friend access so random callers cannot
     * reach for it). SetTexture / SetMaterial do not exist here at all — they
     * write through the shared materials[i].maps pointer and only make sense on
     * a unique entry; see ModelSafeOwned and ModelSafeManaged.
     */
    class ModelSafe
    {
      protected:
        Model rlmodel{};
        std::string modelKey{};

        void SetShader(Shader shader, int materialIdx) const;
        void SetShader(Shader shader) const;

      public:
        [[nodiscard]] const Model& GetRlModel() const;
        [[nodiscard]] const Mesh& GetMesh(int num) const;
        [[nodiscard]] BoundingBox CalcLocalMeshBoundingBox(const Mesh& mesh, bool& success) const;
        [[nodiscard]] BoundingBox CalcLocalBoundingBox() const;
        [[nodiscard]] RayCollision GetRayMeshCollision(Ray ray, int meshNum, Matrix transform) const;
        void UpdateAnimation(ModelAnimation anim, unsigned int frame) const;
        void Draw(Vector3 position, float scale, Color tint) const;
        void Draw(Vector3 position, Vector3 rotationAxis, float rotationAngle, Vector3 scale, Color tint) const;
        void DrawUber(
            UberShaderComponent* uber,
            Vector3 position,
            Vector3 rotationAxis,
            float rotationAngle,
            Vector3 scale,
            Color tint) const;
        [[nodiscard]] int GetMeshCount() const;
        [[nodiscard]] int GetMaterialCount() const;
        [[nodiscard]] Matrix GetTransform() const;
        void SetTransform(Matrix trans);
        [[nodiscard]] Shader GetShader(int materialIdx) const;
        void SetKey(const std::string& newKey);
        [[nodiscard]] const std::string& GetKey() const;

        ModelSafe(const ModelSafe&) = delete;
        ModelSafe& operator=(const ModelSafe&) = delete;
        ModelSafe(ModelSafe&& other) noexcept;
        ModelSafe& operator=(ModelSafe&& other) noexcept;
        ModelSafe() = default;
        virtual ~ModelSafe() = default;

        friend class Renderable;
        friend class ResourceManager;
        friend class TextureTerrainOverlay;
        friend class LightManager;
        friend class UberShaderSystem;
        friend class RenderSystem;
        friend class ResourcePacker;
        friend struct UberShaderComponent;
    };

    /**
     * A procedurally-created Model whose storage (meshes + per-material maps allocations)
     * is owned by this object directly. The destructor frees those allocations locally
     * but does NOT UnloadMaterial — textures and shaders inside the maps are typically
     * RM-cached and shared. Public mutators are sound because the Model is not shared.
     * Move-only.
     */
    class ModelSafeOwned : public ModelSafe
    {
      public:
        using ModelSafe::SetShader;

        void SetTexture(Texture texture, int materialIdx, MaterialMapIndex mapIdx) const;
        void SetMaterial(unsigned int idx, Material mat) const;

        ModelSafeOwned(const ModelSafeOwned&) = delete;
        ModelSafeOwned& operator=(const ModelSafeOwned&) = delete;
        ModelSafeOwned(ModelSafeOwned&& other) noexcept;
        ModelSafeOwned& operator=(ModelSafeOwned&& other) noexcept;

        explicit ModelSafeOwned(Model& rawModel);
        ModelSafeOwned() = default;
        ~ModelSafeOwned() override;
    };

    /**
     * Handle to a ResourceManager-managed deep-copy entry. ResourceManager owns the
     * storage (registered under modelKey); the destructor releases the entry via
     * ResourceManager::ReleaseDeepCopy. Public mutators are sound because the entry
     * is unique to this handle. Move-only.
     */
    class ModelSafeManaged : public ModelSafe
    {
      public:
        using ModelSafe::SetShader;

        void SetTexture(Texture texture, int materialIdx, MaterialMapIndex mapIdx) const;
        void SetMaterial(unsigned int idx, Material mat) const;

        ModelSafeManaged(const ModelSafeManaged&) = delete;
        ModelSafeManaged& operator=(const ModelSafeManaged&) = delete;
        ModelSafeManaged(ModelSafeManaged&& other) noexcept;
        ModelSafeManaged& operator=(ModelSafeManaged&& other) noexcept;

        ModelSafeManaged() = default;
        ~ModelSafeManaged() override;

        friend class ResourceManager;
    };

    // Frees model meshes/bones/bindPose without unloading individual materials.
    // Use when materials are shared (e.g. owned by ResourceManager::materialMap).
    void sgUnloadModel(Model model);

    std::string TitleCase(const std::string& A);
    bool AlmostEquals(Vector3 a, Vector3 b);
    bool PointInsideRect(Rectangle rec, Vector2 point);
    Vector2 Vec3ToVec2(const Vector3& vec3);
    Vector3 NegateVector(const Vector3& vec3);
    Vector3 Vector3MultiplyByValue(const Vector3& vec3, float value);
    Vector2 Vector2MultiplyByValue(const Vector2& vec3, float value);
    Matrix ComposeMatrix(Vector3 translation, Quaternion rotation, Vector3 scale);
    int GetBoneIdByName(const BoneInfo* bones, int numBones, const char* boneName);
    Image GenImageGradientRadialTrans(int width, int height, float density, Color inner, Color outer);
    std::string StripPath(const std::string& fullPath);
} // namespace sage
