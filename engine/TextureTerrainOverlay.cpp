#include "TextureTerrainOverlay.hpp"
#include "components/DynamicRenderable.hpp"
#include "components/Renderable.hpp"
#include "components/sgTransform.hpp"
#include "EngineSystems.hpp"
#include "ResourceManager.hpp"
#include "systems/NavigationGridSystem.hpp"
#include "systems/TransformSystem.hpp"

namespace sage
{
    void TextureTerrainOverlay::updateMeshData(
        Mesh& mesh, const GridSquare& minRange, const GridSquare& maxRange) const
    {
        int maxRow = maxRange.row - minRange.row;
        int maxCol = maxRange.col - minRange.col;

        for (int row = 0; row < maxRow; ++row)
        {
            for (int col = 0; col < maxCol; ++col)
            {
                int vertexIndex = row * maxCol + col;
                int gridRow = row + minRange.row;
                int gridCol = col + minRange.col;

                updateVertexData(mesh, vertexIndex, gridRow, gridCol);
                updateNormalData(mesh, vertexIndex, gridRow, gridCol);
                updateTexCoordData(mesh, vertexIndex, row, col, maxRow, maxCol);
            }
        }
    }

    Mesh TextureTerrainOverlay::createInitialMesh(const GridSquare& minRange, const GridSquare& maxRange) const
    {
        int maxRow = maxRange.row - minRange.row;
        int maxCol = maxRange.col - minRange.col;
        int vertexCount = maxRow * maxCol;

        Mesh mesh = {.vertexCount = 0};
        mesh.vertexCount = vertexCount;
        mesh.triangleCount = (maxRow - 1) * (maxCol - 1) * 2;
        mesh.vertices = static_cast<float*>(MemAlloc(vertexCount * 3 * sizeof(float)));
        mesh.normals = static_cast<float*>(MemAlloc(vertexCount * 3 * sizeof(float)));
        mesh.texcoords = static_cast<float*>(MemAlloc(vertexCount * 2 * sizeof(float)));
        mesh.indices = static_cast<unsigned short*>(MemAlloc(mesh.triangleCount * 3 * sizeof(unsigned short)));

        updateMeshData(mesh, minRange, maxRange);
        generateIndices(mesh, maxRow, maxCol);

        return mesh;
    }

    void TextureTerrainOverlay::updateVertexData(Mesh& mesh, int vertexIndex, int gridRow, int gridCol) const
    {
        const auto& gridSquares = sys->navigationGridSystem->GetGridSquares();
        mesh.vertices[vertexIndex * 3] = gridSquares.at(gridRow).at(gridCol).worldPosMin.x;
        mesh.vertices[vertexIndex * 3 + 1] = gridSquares.at(gridRow).at(gridCol).heightMap.GetHeight() +
                                             0.3f; // Little buffer so the overlay doesn't blend into terrain
        mesh.vertices[vertexIndex * 3 + 2] = gridSquares.at(gridRow).at(gridCol).worldPosMin.z;
    }

    void TextureTerrainOverlay::updateNormalData(Mesh& mesh, int vertexIndex, int gridRow, int gridCol) const
    {
        const auto& gridSquares = sys->navigationGridSystem->GetGridSquares();
        mesh.normals[vertexIndex * 3] = gridSquares.at(gridRow).at(gridCol).heightMap.GetNormal().x;
        mesh.normals[vertexIndex * 3 + 1] = gridSquares.at(gridRow).at(gridCol).heightMap.GetNormal().y;
        mesh.normals[vertexIndex * 3 + 2] = gridSquares.at(gridRow).at(gridCol).heightMap.GetNormal().z;
    }

    void TextureTerrainOverlay::updateTexCoordData(
        Mesh& mesh, int vertexIndex, int row, int col, int maxRow, int maxCol)
    {
        mesh.texcoords[vertexIndex * 2] = static_cast<float>(col) / static_cast<float>(maxCol - 1);
        mesh.texcoords[vertexIndex * 2 + 1] = static_cast<float>(row) / static_cast<float>(maxRow - 1);
    }

    void TextureTerrainOverlay::generateIndices(Mesh& mesh, int maxRow, int maxCol)
    {
        int indexCount = 0;
        for (int row = 0; row < maxRow - 1; ++row)
        {
            for (int col = 0; col < maxCol - 1; ++col)
            {
                int topLeft = row * maxCol + col;
                int topRight = topLeft + 1;
                int bottomLeft = (row + 1) * maxCol + col;
                int bottomRight = bottomLeft + 1;

                mesh.indices[indexCount++] = topLeft;
                mesh.indices[indexCount++] = bottomLeft;
                mesh.indices[indexCount++] = topRight;

                mesh.indices[indexCount++] = topRight;
                mesh.indices[indexCount++] = bottomLeft;
                mesh.indices[indexCount++] = bottomRight;
            }
        }
    }

    void TextureTerrainOverlay::updateTerrainPolygon(const GridSquare& minRange, const GridSquare& maxRange) const
    {
        auto& renderable = registry->get<DynamicRenderable>(entity);
        auto meshReference = renderable.GetMesh();
        assert(meshReference && "TextureTerrainOverlay: dynamic renderable must hold a model");
        auto& mesh = meshReference->get();
        updateMeshData(mesh, minRange, maxRange);

        int vertexCount = mesh.vertexCount;
        UpdateMeshBuffer(mesh, 0, mesh.vertices, static_cast<int>(vertexCount * 3 * sizeof(float)), 0);
        UpdateMeshBuffer(mesh, 1, mesh.normals, static_cast<int>(vertexCount * 3 * sizeof(float)), 0);
        UpdateMeshBuffer(mesh, 2, mesh.texcoords, static_cast<int>(vertexCount * 2 * sizeof(float)), 0);
    }

    Model TextureTerrainOverlay::generateTerrainPolygon(
        const GridSquare& minRange, const GridSquare& maxRange) const
    {
        Mesh mesh = createInitialMesh(minRange, maxRange);

        UploadMesh(&mesh, false);
        Model model = LoadModelFromMesh(mesh);
        model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = texture;

        return model;
    }

    void TextureTerrainOverlay::SetHint(Color col) const
    {
        auto& renderable = registry->get<DynamicRenderable>(entity);
        renderable.hint = col;
    }

    void TextureTerrainOverlay::SetShader(Shader _shader)
    {
        shader = _shader;
        auto& renderable = registry->get<DynamicRenderable>(entity);
        renderable.SetShader(_shader);
    }

    void TextureTerrainOverlay::Enable(bool enable)
    {
        assert(initialised);
        m_active = enable;
        auto& renderable = registry->get<DynamicRenderable>(entity);
        renderable.active = enable;
    }

    void TextureTerrainOverlay::Init(const Vector3 startPos, const float _radius)
    {
        if (initialised)
        {
            Update(startPos);
            return;
        };
        initialised = true;
        radius = _radius; // Should be in constructor

        sys->navigationGridSystem->WorldToGridSpace(startPos, lastHit);
        GridSquare minRange{}, maxRange{};
        sys->navigationGridSystem->GetGridRange(startPos, static_cast<int>(radius), minRange, maxRange);
        auto& renderable = registry->get<DynamicRenderable>(entity);
        renderable.SetModel(generateTerrainPolygon(minRange, maxRange));
        renderable.SetShader(shader);

        // Calculate the center of the mesh in world space
        const auto& gridSquares = sys->navigationGridSystem->GetGridSquares();
        const Vector3 meshMin = {
            .x = gridSquares.at(minRange.row).at(minRange.col).worldPosMin.x,
            .y = gridSquares.at(minRange.row).at(minRange.col).heightMap.GetHeight(),
            .z = gridSquares.at(minRange.row).at(minRange.col).worldPosMin.z};
        const Vector3 meshMax = {
            .x = gridSquares.at(maxRange.row - 1).at(maxRange.col - 1).worldPosMax.x,
            .y = gridSquares.at(maxRange.row - 1).at(maxRange.col - 1).heightMap.GetHeight(),
            .z = gridSquares.at(maxRange.row - 1).at(maxRange.col - 1).worldPosMax.z};
        const Vector3 meshCenter = {
            .x = (meshMin.x + meshMax.x) * 0.5f, .y = 0, .z = (meshMin.z + meshMax.z) * 0.5f};

        // Calculate the offset to center the mesh on the mouse position
        meshOffset = {.x = startPos.x - meshCenter.x, .y = 0, .z = startPos.z - meshCenter.z};
        registry->get<sgTransform>(entity).position.world = meshOffset;
        renderable.SetTransform(MatrixIdentity());
    }

    bool TextureTerrainOverlay::IsActive() const
    {
        return m_active;
    }

    void TextureTerrainOverlay::Update(Vector3 pos)
    {
        if (!m_active) return;
        GridSquare gridPos{};
        sys->navigationGridSystem->WorldToGridSpace(pos, gridPos);
        if (lastHit == gridPos) return;

        lastHit = gridPos;
        GridSquare minRange{}, maxRange{};
        sys->navigationGridSystem->GetGridRange(pos, static_cast<int>(radius), minRange, maxRange);

        updateTerrainPolygon(minRange, maxRange);
        registry->get<sgTransform>(entity).position.world = meshOffset;
    }

    TextureTerrainOverlay::~TextureTerrainOverlay()
    {
        UnloadTexture(texture);
        // auto& renderable = registry->get<Renderable>(entity);
        // renderable.GetModel()->get().UnloadMaterials();
    }

    TextureTerrainOverlay::TextureTerrainOverlay(
        entt::registry* _registry, EngineSystems* _engineSystems, Texture tex, Color _hint, const char* shaderPath)
        : registry(_registry), sys(_engineSystems), texture(tex), entity(_registry->create())
    {
        assert(shaderPath != nullptr);
        shader = ResourceManager::GetInstance().ShaderLoad(std::nullopt, shaderPath);

        auto& r = registry->emplace<DynamicRenderable>(entity);
        r.active = false;
        r.SetName("TextureTerrainOverlay");
        r.hint = _hint;
        registry->emplace<sgTransform>(entity);
        registry->emplace<RenderableDeferred>(entity);
    }

    TextureTerrainOverlay::TextureTerrainOverlay(
        entt::registry* _registry,
        EngineSystems* _engineSystems,
        const std::string& assetId,
        Color _hint,
        const char* shaderPath)
        : registry(_registry),
          sys(_engineSystems),
          texture(ResourceManager::GetInstance().TextureLoad(assetId)),
          entity(_registry->create())
    {
        assert(shaderPath != nullptr);
        shader = ResourceManager::GetInstance().ShaderLoad(std::nullopt, shaderPath);

        auto& r = registry->emplace<DynamicRenderable>(entity);
        r.active = false;
        r.SetName("TextureTerrainOverlay");
        r.hint = _hint;
        registry->emplace<sgTransform>(entity);
        registry->emplace<RenderableDeferred>(entity);
    }

    TextureTerrainOverlay::TextureTerrainOverlay(
        entt::registry* _registry,
        EngineSystems* _engineSystems,
        const std::string& assetId,
        Color _hint,
        Shader _shader)
        : registry(_registry),
          sys(_engineSystems),
          texture(ResourceManager::GetInstance().TextureLoad(assetId)),
          shader(_shader),
          entity(_registry->create())
    {

        auto& r = registry->emplace<DynamicRenderable>(entity);
        r.active = false;
        r.SetName("TextureTerrainOverlay");
        r.hint = _hint;

        registry->emplace<sgTransform>(entity);
        registry->emplace<RenderableDeferred>(entity);
    }

} // namespace sage
