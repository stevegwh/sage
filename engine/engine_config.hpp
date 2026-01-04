//
// Created by Steve Wheeler on 04/01/2026.
//

#pragma once
#include <array>
#include <string>
#include <unordered_map>

namespace sage
{
    enum class CollisionLayer
    {
        DEFAULT,
        GEOMETRY_SIMPLE, // Uses bounding box as foundation for collision
        BUILDING,
        NAVIGATION, // Unsure.
        PLAYER,
        NPC,
        ENEMY,
        BOYD,
        GEOMETRY_COMPLEX, // Uses mesh as basis for collision
        BACKGROUND,       // Collides with nothing
        STAIRS,
        ITEM,
        INTERACTABLE,
        CHEST,
        COUNT // Must always be last
    };

    static const std::unordered_map<CollisionLayer, std::string> cursorTextureMap = {
        {CollisionLayer::DEFAULT, "cursor_regular"},
        {CollisionLayer::NPC, "cursor_talk"},
        {CollisionLayer::GEOMETRY_SIMPLE, "cursor_move"},
        {CollisionLayer::GEOMETRY_COMPLEX, "cursor_move"},
        {CollisionLayer::NAVIGATION, "cursor_move"},
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
