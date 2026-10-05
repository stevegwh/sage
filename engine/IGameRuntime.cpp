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

        StandaloneGameLauncher& launcherStorage()
        {
            static StandaloneGameLauncher launcher;
            return launcher;
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

    void SetStandaloneGameLauncher(StandaloneGameLauncher launcher)
    {
        launcherStorage() = std::move(launcher);
    }

    bool HasStandaloneGameLauncher()
    {
        return static_cast<bool>(launcherStorage());
    }

    bool LaunchStandaloneGame(const std::string& mapPath)
    {
        const auto& launcher = launcherStorage();
        if (!launcher) return false;
        try
        {
            return launcher(mapPath);
        }
        catch (const std::exception& error)
        {
            std::cerr << "ERROR: Failed to launch standalone game: " << error.what() << '\n';
            return false;
        }
    }
} // namespace sage
