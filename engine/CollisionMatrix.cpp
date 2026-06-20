//
// Created by Steve Wheeler on 12/06/2026.
//

#include "CollisionMatrix.hpp"

#include "cereal/archives/json.hpp"
#include "raylib.h"

#include <bit>
#include <cassert>
#include <exception>
#include <fstream>

namespace sage
{
    namespace
    {
        int bitIndex(const CollisionLayer layer)
        {
            assert(layer.IsValid() && "bitIndex requires a non-zero collision layer");
            return std::countr_zero(layer.bit);
        }
    } // namespace

    CollisionMask CollisionMatrix::GetMask(const CollisionLayer layer) const
    {
        if (!layer.IsValid()) return CollisionMask{};
        return CollisionMask{rows[bitIndex(layer)]};
    }

    bool CollisionMatrix::GetPair(const CollisionLayer a, const CollisionLayer b) const
    {
        if (!a.IsValid() || !b.IsValid()) return false;
        return (rows[bitIndex(a)] & b.bit) != 0;
    }

    void CollisionMatrix::SetPair(const CollisionLayer a, const CollisionLayer b, const bool collides)
    {
        if (!a.IsValid() || !b.IsValid()) return;
        if (collides)
        {
            rows[bitIndex(a)] |= b.bit;
            rows[bitIndex(b)] |= a.bit;
        }
        else
        {
            rows[bitIndex(a)] &= ~b.bit;
            rows[bitIndex(b)] &= ~a.bit;
        }
    }

    CollisionLayer CollisionMatrix::AddUserLayer(const std::string& layerName)
    {
        if (layerName.empty()) return {};
        for (const auto& l : GetCollisionLayers())
        {
            if (l.layerName == layerName) return {};
        }
        const int index = FindFreeCollisionLayerIndex();
        if (index < 0) return {};
        userLayers.push_back({layerName, static_cast<std::uint8_t>(index)});
        return RegisterUserCollisionLayer(layerName, static_cast<std::uint8_t>(index));
    }

    void CollisionMatrix::ResetToDefaults()
    {
        rows = {};
        SetPair(collision_layers::Default, collision_layers::Default, true);
    }

    void CollisionMatrix::Load(const char* path)
    {
        ResetToDefaults();

        std::ifstream file(path);
        if (!file.is_open())
        {
            TraceLog(LOG_INFO, "CollisionMatrix: no settings file at '%s', using defaults", path);
            return;
        }

        try
        {
            cereal::JSONInputArchive archive(file);
            archive(*this);
        }
        catch (const std::exception& e)
        {
            TraceLog(LOG_ERROR, "CollisionMatrix: failed to read '%s' (%s), using defaults", path, e.what());
            userLayers.clear();
            ResetToDefaults();
            return;
        }

        for (const auto& record : userLayers)
        {
            RegisterUserCollisionLayer(record.name, record.index);
        }
    }

    void CollisionMatrix::Save(const char* path) const
    {
        std::ofstream file(path);
        if (!file.is_open())
        {
            TraceLog(LOG_ERROR, "CollisionMatrix: unable to write '%s'", path);
            return;
        }
        cereal::JSONOutputArchive archive(file);
        archive(cereal::make_nvp("collisionMatrix", *this));
    }
} // namespace sage
