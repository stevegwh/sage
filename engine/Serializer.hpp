//
// Created by Steve Wheeler on 21/03/2024.
//

#pragma once
#include <array>
#include <stdexcept>

#include "ViewSerializer.hpp"

#include "cereal/archives/binary.hpp"
#include "cereal/archives/xml.hpp"
#include "cereal/cereal.hpp"
#include "cereal/types/string.hpp"
#include "entt/core/hashed_string.hpp"
#include "entt/core/type_traits.hpp"
#include "entt/entt.hpp"
#include "raylib-cereal.hpp"
#include <cereal/archives/json.hpp>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace sage::serializer
{
    // Archiving definitions
    struct entity
    {
        unsigned int id;
    };

    template <typename Archive>
    void serialize(Archive& archive, entity& entity)
    {
        archive(entity.id);
    }

    void LoadAssetBinFile(
        entt::registry* destination, const char* path, const std::function<void()>& updateLoadingScreen = {});

    template <typename T>
    bool SaveClassXML(const char* path, const T& toSave)
    {
        std::cout << "START: Saving class data to XML file." << std::endl;
        using namespace entt::literals;
        // std::stringstream storage;

        std::ofstream storage(path);
        if (!storage.is_open())
        {
            std::cerr << "ERROR: Unable to open '" << path << "' for writing." << std::endl;
            return false;
        }

        {
            // output finishes flushing its contents when it goes out of scope
            cereal::XMLOutputArchive output{storage};
            output(toSave);
        }

        storage.close();
        std::cout << "FINISH: Saving class data to XML file." << std::endl;
        return true;
    }

    template <typename T>
    void DeserializeXMLFile(const char* path, T& target)
    {
        std::cout << "START: Loading data from file." << std::endl;
        using namespace entt::literals;

        std::ifstream storage(path);
        if (storage.is_open())
        {
            cereal::XMLInputArchive input{storage};
            input(target);
            storage.close();
        }
        else
        {
            // File doesn't exist, create a new file with the default key mapping
            std::cout << "INFO: File not found. Creating a new file with defaults." << std::endl;
            SaveClassXML<T>(path, target);
        }
        std::cout << "FINISH: Loading data from file." << std::endl;
    }

    template <typename T>
    bool SaveClassJson(const std::string& path, const T& toSave)
    {
        std::cout << "START: Saving class data to json file." << std::endl;
        using namespace entt::literals;
        // std::stringstream storage;

        std::ofstream storage(path);
        if (!storage.is_open())
        {
            std::cerr << "ERROR: Unable to open '" << path << "' for writing." << std::endl;
            return false;
        }

        {
            // output finishes flushing its contents when it goes out of scope
            cereal::JSONOutputArchive output{storage};
            // output.setNextName(name);
            output(toSave);
        }

        storage.close();
        std::cout << "FINISH: Saving class data to json file." << std::endl;
        return true;
    }

    template <typename T>
    bool SaveViewJson(entt::registry& source, const char* path)
    {
        std::cout << "START: Saving view data to json file." << std::endl;
        using namespace entt::literals;
        // std::stringstream storage;

        std::ofstream storage(path);
        if (!storage.is_open())
        {
            std::cerr << "ERROR: Unable to open '" << path << "' for writing." << std::endl;
            return false;
        }

        {
            // output finishes flushing its contents when it goes out of scope
            cereal::JSONOutputArchive output{storage};
            ViewSerializer<T> allData(&source);
            output(allData);
        }

        storage.close();
        std::cout << "FINISH: Saving view data to json file." << std::endl;
        return true;
    }

    template <typename T>
    void DeserializeJsonFile(const char* path, T& target)
    {
        std::cout << "START: Loading data from file." << std::endl;
        using namespace entt::literals;

        std::ifstream storage(path);
        if (storage.is_open())
        {
            cereal::JSONInputArchive input{storage};
            input(target);
            storage.close();
        }
        else
        {
            // File doesn't exist, create a new file with the default key mapping
            std::cout << "INFO: File not found. Creating a new file with defaults." << std::endl;
            SaveClassJson<T>(path, target);
        }
        std::cout << "FINISH: Loading data from file." << std::endl;
    }

    // Assets use Zstandard; map formats retain their existing DEFLATE payloads.
    inline constexpr std::array<char, 4> ASSET_BIN_MAGIC = {'L', 'Q', 'B', '4'};
    inline constexpr std::array<char, 4> MAP_BIN_MAGIC = {'L', 'Q', 'M', '2'};

    // The 20-byte header contains the format magic, raw size, and compressed size.
    bool WriteCompressedBinaryPayload(
        const char* path, const std::array<char, 4>& magic, std::string_view payload);
    std::string ReadCompressedBinaryPayload(const char* path, const std::array<char, 4>& magic);

    template <typename ArchiveFn>
    bool WriteCompressedBinary(const char* path, const std::array<char, 4>& magic, ArchiveFn&& archiveFn)
    {
        std::ostringstream buf(std::ios::binary);
        {
            cereal::BinaryOutputArchive output{buf};
            std::forward<ArchiveFn>(archiveFn)(output);
        }
        return WriteCompressedBinaryPayload(path, magic, buf.str());
    }

    template <typename ArchiveFn>
    void ReadCompressedBinary(const char* path, const std::array<char, 4>& magic, ArchiveFn&& archiveFn)
    {
        std::istringstream inBuf(ReadCompressedBinaryPayload(path, magic), std::ios::binary);
        {
            cereal::BinaryInputArchive input(inBuf);
            std::forward<ArchiveFn>(archiveFn)(input, inBuf);
        }
    }

    template <typename T>
    bool SaveClassBinary(const char* path, const T& toSave)
    {
        std::cout << "START: Saving class data to binary file." << std::endl;
        const bool ok = WriteCompressedBinary(
            path, ASSET_BIN_MAGIC, [&](cereal::BinaryOutputArchive& output) { output(toSave); });
        std::cout << "FINISH: Saving class data to binary file." << std::endl;
        return ok;
    }
} // namespace sage::serializer
