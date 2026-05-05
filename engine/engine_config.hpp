//
// Created by Steve Wheeler on 04/01/2026.
//

#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

using CollisionMatrix = std::vector<std::vector<bool>>;

namespace sage
{
    // Collision config
    enum class CollisionLayer
    {
        // Reserved start
        DEFAULT,
        GEOMETRY_SIMPLE,  // Uses bounding box as foundation for collision
        GEOMETRY_COMPLEX, // Uses mesh as basis for collision
        BUILDING,
        PLAYER,
        BACKGROUND, // Collides with nothing
        STAIRS,
        // Reserved end
        // User defined start
        NPC,
        ENEMY,
        ITEM,
        INTERACTABLE,
        CHEST,
        // User defined end
        COUNT // Must always be last
    };

    static const std::unordered_map<CollisionLayer, std::string> cursorTextureMap = {
        {CollisionLayer::DEFAULT, "cursor_regular"},
        {CollisionLayer::NPC, "cursor_talk"},
        {CollisionLayer::GEOMETRY_SIMPLE, "cursor_move"},
        {CollisionLayer::GEOMETRY_COMPLEX, "cursor_move"},
        {CollisionLayer::ENEMY, "cursor_attack"},
        {CollisionLayer::ITEM, "cursor_pickup"},
        {CollisionLayer::CHEST, "cursor_pickup"},
        {CollisionLayer::INTERACTABLE, "cursor_interact"},
        {CollisionLayer::BUILDING, "cursor_denied"}}; // namespace sage

    constexpr std::array cursorHoverLayers = {
        CollisionLayer::NPC,
        CollisionLayer::ENEMY,
        CollisionLayer::ITEM,
        CollisionLayer::INTERACTABLE,
        CollisionLayer::CHEST};

    CollisionMatrix static CreateCollisionMatrix()
    {
        // TODO: Add touhou-raylib changes for collision matrix
        int numLayers = static_cast<int>(CollisionLayer::COUNT);
        std::vector matrix(numLayers, std::vector<bool>(numLayers, false));

        matrix[static_cast<int>(CollisionLayer::DEFAULT)][static_cast<int>(CollisionLayer::PLAYER)] = true;
        matrix[static_cast<int>(CollisionLayer::DEFAULT)][static_cast<int>(CollisionLayer::ENEMY)] = true;
        matrix[static_cast<int>(CollisionLayer::DEFAULT)][static_cast<int>(CollisionLayer::NPC)] = true;
        matrix[static_cast<int>(CollisionLayer::DEFAULT)][static_cast<int>(CollisionLayer::ITEM)] = true;
        matrix[static_cast<int>(CollisionLayer::DEFAULT)][static_cast<int>(CollisionLayer::INTERACTABLE)] = true;
        matrix[static_cast<int>(CollisionLayer::DEFAULT)][static_cast<int>(CollisionLayer::CHEST)] = true;
        matrix[static_cast<int>(CollisionLayer::DEFAULT)][static_cast<int>(CollisionLayer::BUILDING)] = true;
        matrix[static_cast<int>(CollisionLayer::DEFAULT)][static_cast<int>(CollisionLayer::GEOMETRY_SIMPLE)] =
            true;
        matrix[static_cast<int>(CollisionLayer::DEFAULT)][static_cast<int>(CollisionLayer::GEOMETRY_COMPLEX)] =
            true;
        matrix[static_cast<int>(CollisionLayer::DEFAULT)][static_cast<int>(CollisionLayer::STAIRS)] = true;

        matrix[static_cast<int>(CollisionLayer::PLAYER)][static_cast<int>(CollisionLayer::ENEMY)] = true;
        matrix[static_cast<int>(CollisionLayer::PLAYER)][static_cast<int>(CollisionLayer::BUILDING)] = true;
        matrix[static_cast<int>(CollisionLayer::PLAYER)][static_cast<int>(CollisionLayer::INTERACTABLE)] = true;
        matrix[static_cast<int>(CollisionLayer::PLAYER)][static_cast<int>(CollisionLayer::CHEST)] = true;

        matrix[static_cast<int>(CollisionLayer::ENEMY)][static_cast<int>(CollisionLayer::PLAYER)] = true;
        matrix[static_cast<int>(CollisionLayer::ENEMY)][static_cast<int>(CollisionLayer::BUILDING)] = true;

        return matrix;
    }
    // Animation config
    enum class AnimationEnum
    {
        IDLE,
        IDLE2,
        DEATH,
        AUTOATTACK,
        WALK,
        TALK,
        SPIN,
        SLASH,
        RUN,
        SPELLCAST_FWD,
        SPELLCAST_UP,
        ROLL
    };
} // namespace sage
