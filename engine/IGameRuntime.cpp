//
// Created for play-in-editor support. Stores the single game-runtime factory
// registered by the game executable (see IGameRuntime.hpp).
//

#include "IGameRuntime.hpp"

#include <exception>
#include <iostream>

namespace sage
{
    namespace
    {
        GameRuntimeFactory& factoryStorage()
        {
            static GameRuntimeFactory factory;
            return factory;
        }
    } // namespace

    void SetGameRuntimeFactory(GameRuntimeFactory factory)
    {
        factoryStorage() = std::move(factory);
    }

    bool HasGameRuntimeFactory()
    {
        return static_cast<bool>(factoryStorage());
    }

    std::unique_ptr<IGameRuntime> CreateGameRuntime(const GameRuntimeContext& context)
    {
        auto& factory = factoryStorage();
        if (!factory) return nullptr;
        try
        {
            return factory(context);
        }
        catch (const std::exception& e)
        {
            std::cerr << "ERROR: Failed to create game runtime: " << e.what() << std::endl;
            return nullptr;
        }
    }
} // namespace sage
