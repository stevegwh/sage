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

    // Per-file-type magic prefixes for compressed binaries. Bumped if on-disk layout changes.
    inline constexpr std::array<char, 4> ASSET_BIN_MAGIC = {'L', 'Q', 'B', '2'};
    inline constexpr std::array<char, 4> MAP_BIN_MAGIC = {'L', 'Q', 'M', '2'};

    // Writes a 20-byte header (magic + uncompressed size + compressed size) followed by a
    // DEFLATE-compressed cereal binary payload. The lambda receives a BinaryOutputArchive and
    // is free to call output(...) any number of times.
    template <typename ArchiveFn>
    bool WriteCompressedBinary(const char* path, const std::array<char, 4>& magic, ArchiveFn&& archiveFn)
    {
        std::ostringstream buf(std::ios::binary);
        {
            cereal::BinaryOutputArchive output{buf};
            std::forward<ArchiveFn>(archiveFn)(output);
        }
        const std::string raw = buf.str();
        const int rawSize = static_cast<int>(raw.size());

        int compSize = 0;
        unsigned char* compData =
            CompressData(reinterpret_cast<const unsigned char*>(raw.data()), rawSize, &compSize);
        if (compData == nullptr || compSize <= 0)
        {
            std::cerr << "ERROR: CompressData failed; aborting save of '" << path << "'." << std::endl;
            if (compData) MemFree(compData);
            return false;
        }

        std::ofstream storage(path, std::ios::binary);
        if (!storage.is_open())
        {
            std::cerr << "ERROR: Unable to open '" << path << "' for writing." << std::endl;
            MemFree(compData);
            return false;
        }

        const auto uncompressedSize = static_cast<uint64_t>(rawSize);
        const auto compressedSize = static_cast<uint64_t>(compSize);
        storage.write(magic.data(), static_cast<std::streamsize>(magic.size()));
        storage.write(reinterpret_cast<const char*>(&uncompressedSize), sizeof(uncompressedSize));
        storage.write(reinterpret_cast<const char*>(&compressedSize), sizeof(compressedSize));
        storage.write(reinterpret_cast<const char*>(compData), compSize);

        MemFree(compData);
        storage.close();
        std::cout << "  (raw=" << rawSize << "B compressed=" << compSize
                  << "B ratio=" << (rawSize > 0 ? (static_cast<double>(compSize) / rawSize) : 0.0) << ")"
                  << std::endl;
        return true;
    }

    // Reads a header-prefixed DEFLATE-compressed cereal payload, then invokes the lambda with
    // both a BinaryInputArchive and the underlying istream (so callers that use peek-until-EOF
    // loops still work).
    inline std::string ReadCompressedBinaryPayload(const char* path, const std::array<char, 4>& magic)
    {
        std::ifstream storage(path, std::ios::binary);
        if (!storage.is_open())
        {
            std::cerr << "ERROR: Unable to open file for reading." << std::endl;
            throw std::runtime_error(std::string("Cannot read compressed asset: ") + path);
        }

        std::array<char, 4> fileMagic{};
        uint64_t uncompressedSize = 0;
        uint64_t compressedSize = 0;
        storage.read(fileMagic.data(), static_cast<std::streamsize>(fileMagic.size()));
        storage.read(reinterpret_cast<char*>(&uncompressedSize), sizeof(uncompressedSize));
        storage.read(reinterpret_cast<char*>(&compressedSize), sizeof(compressedSize));

        if (fileMagic != magic)
        {
            std::cerr << "ERROR: file magic mismatch at " << path << " (got '"
                      << std::string(fileMagic.data(), fileMagic.size()) << "', expected '"
                      << std::string(magic.data(), magic.size()) << "')." << std::endl;
            throw std::runtime_error(std::string("Cannot read compressed asset: ") + path);
        }

        constexpr std::uint64_t MAX_COMPRESSED_ASSET_BYTES = 512ull * 1024 * 1024;
        if (!storage || compressedSize == 0 || compressedSize > MAX_COMPRESSED_ASSET_BYTES ||
            uncompressedSize > MAX_COMPRESSED_ASSET_BYTES)
            throw std::runtime_error(std::string("Invalid compressed asset size: ") + path);
        const auto payloadStart = storage.tellg();
        storage.seekg(0, std::ios::end);
        if (std::cmp_not_equal(storage.tellg() - payloadStart, compressedSize))
            throw std::runtime_error(std::string("Truncated compressed asset: ") + path);
        storage.seekg(payloadStart);
        std::vector<unsigned char> compBuf(compressedSize);
        storage.read(reinterpret_cast<char*>(compBuf.data()), static_cast<std::streamsize>(compressedSize));
        storage.close();

        int decompSize = 0;
        unsigned char* decompData = DecompressData(compBuf.data(), static_cast<int>(compressedSize), &decompSize);
        if (decompData == nullptr || std::cmp_not_equal(decompSize, uncompressedSize))
        {
            std::cerr << "ERROR: DecompressData failed (got " << decompSize << ", expected " << uncompressedSize
                      << ")." << std::endl;
            if (decompData) MemFree(decompData);
            throw std::runtime_error(std::string("Cannot read compressed asset: ") + path);
        }

        std::string decompStr(reinterpret_cast<const char*>(decompData), uncompressedSize);
        MemFree(decompData);
        return decompStr;
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
