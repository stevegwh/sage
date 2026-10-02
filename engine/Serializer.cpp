//
// Created by Steve Wheeler on 21/03/2024.
//

#include "Serializer.hpp"

#include "components/Collideable.hpp"
#include "components/Renderable.hpp"
#include "components/sgTransform.hpp"

#include <chrono>
#include <future>
#include <streambuf>
#include <unordered_map>

namespace sage::serializer
{
    namespace
    {
        class LoadingStreamBuffer final : public std::streambuf
        {
            std::reference_wrapper<const std::function<void()>> updateLoadingScreen;
            std::chrono::steady_clock::time_point lastUpdate = std::chrono::steady_clock::now();

          public:
            LoadingStreamBuffer(std::string& payload, const std::function<void()>& update)
                : updateLoadingScreen(update)
            {
                setg(payload.data(), payload.data(), payload.data() + payload.size());
            }

          protected:
            std::streamsize xsgetn(char* destination, const std::streamsize count) override
            {
                const auto read = std::streambuf::xsgetn(destination, count);
                if (updateLoadingScreen.get() &&
                    std::chrono::steady_clock::now() - lastUpdate >= std::chrono::milliseconds(50))
                {
                    updateLoadingScreen.get()();
                    lastUpdate = std::chrono::steady_clock::now();
                }
                return read;
            }
        };
    } // namespace

    // ----------------------------------------------

    void LoadAssetBinFile(
        entt::registry* destination, const char* path, const std::function<void()>& updateLoadingScreen)
    {
        assert(destination != nullptr);
        std::cout << "START: Loading asset bin." << std::endl;

        std::ifstream header(path, std::ios::binary);
        std::array<char, 4> magic{};
        header.read(magic.data(), magic.size());
        const bool cpuArchive = magic == ASSET_BIN_MAGIC;
        std::unordered_map<std::uint32_t, entt::entity> idMap;

        auto loadArchive = [&](cereal::BinaryInputArchive& input, std::istream& stream) {
            if (cpuArchive)
            {
                PackedAssets assets;
                input(assets);
                ResourceManager::GetInstance().LoadPackedAssets(assets, updateLoadingScreen);
                return;
            }
            input(ResourceManager::GetInstance());

            // Not necessary for asset bin?
            while (stream.peek() != EOF)
            {
                entity entityId{};
                auto entt = destination->create();
                auto& transform = destination->emplace<sgTransform>(entt);
                auto& collideable = destination->emplace<Collideable>(entt);
                auto& renderable = destination->emplace<Renderable>(entt);

                try
                {
                    input(entityId, transform, collideable, renderable);
                }
                catch (const cereal::Exception& e)
                {
                    std::cerr << "ERROR: Serialization error: " << e.what() << std::endl;
                    break;
                }
                idMap[entityId.id] = entt;
            }
        };

        if (updateLoadingScreen)
        {
            const std::string assetPath(path);
            auto payloadFuture = std::async(std::launch::async, [assetPath] {
                return ReadCompressedBinaryPayload(assetPath.c_str(), ASSET_BIN_MAGIC);
            });
            while (payloadFuture.wait_for(std::chrono::milliseconds(16)) != std::future_status::ready)
                updateLoadingScreen();
            std::string payload = payloadFuture.get();
            LoadingStreamBuffer buffer(payload, updateLoadingScreen);
            std::istream stream(&buffer);
            cereal::BinaryInputArchive input(stream);
            loadArchive(input, stream);
        }
        else
        {
            ReadCompressedBinary(path, ASSET_BIN_MAGIC, loadArchive);
        }

        for (auto [e, t] : destination->view<sgTransform>().each())
        {
            t.ResolveSerializedParent(idMap);
        }

        std::cout << "FINISH: Loading asset bin." << std::endl;
    }
} // namespace sage::serializer
