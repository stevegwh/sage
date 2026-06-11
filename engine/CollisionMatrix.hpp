//
// Created by Steve Wheeler on 12/06/2026.
//

#pragma once

#include "CollisionLayers.hpp"

#include "cereal/cereal.hpp"
#include "cereal/types/array.hpp"
#include "cereal/types/string.hpp"
#include "cereal/types/vector.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sage
{
    // Unity-style layer collision matrix: a single symmetric grid decides which
    // layers interact. Collideables only carry a layer; CollisionSystem resolves
    // what that layer collides with (trigger overlaps, default query masks) here.
    //
    // Persisted as JSON (DEFAULT_PATH) together with any user-created layers, so
    // the editor's matrix window and the game share one source of truth. Layers
    // declared in code (engine + project/CustomCollisionLayers.hpp) are identified
    // by bit and never stored; user layers store name + bit index and are
    // registered into the layer registry on Load().
    class CollisionMatrix
    {
        // rows[i] = mask of layer bits that the layer with bit index i interacts
        // with. SetPair keeps the matrix symmetric.
        std::array<std::uint64_t, MAX_COLLISION_LAYERS> rows{};

        struct UserLayerRecord
        {
            std::string name;
            std::uint8_t index{};

            template <class Archive>
            void serialize(Archive& archive)
            {
                archive(cereal::make_nvp("name", name), cereal::make_nvp("index", index));
            }
        };
        std::vector<UserLayerRecord> userLayers;

      public:
        static constexpr const char* DEFAULT_PATH = "resources/collision_matrix.json";

        [[nodiscard]] CollisionMask GetMask(CollisionLayer layer) const;
        [[nodiscard]] bool GetPair(CollisionLayer a, CollisionLayer b) const;
        void SetPair(CollisionLayer a, CollisionLayer b, bool collides);

        // Creates a user layer named `layerName` on the next free bit and registers
        // it. Returns an invalid layer when the name is empty or already in use, or
        // when all 64 bits are taken.
        CollisionLayer AddUserLayer(const std::string& layerName);

        void ResetToDefaults();
        // Reads the matrix and user layers from `path` (registering the user
        // layers); a missing or unreadable file falls back to ResetToDefaults().
        void Load(const char* path = DEFAULT_PATH);
        void Save(const char* path = DEFAULT_PATH) const;

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(cereal::make_nvp("userLayers", userLayers), cereal::make_nvp("rows", rows));
        }
    };
} // namespace sage
