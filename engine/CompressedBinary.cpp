#include "Serializer.hpp"

#include <zstd.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <memory>
#include <thread>

namespace sage::serializer
{
    namespace
    {
        constexpr std::array<char, 4> LEGACY_ZSTD_ASSET_BIN_MAGIC = {'L', 'Q', 'B', '3'};
        constexpr std::array<char, 4> LEGACY_ASSET_BIN_MAGIC = {'L', 'Q', 'B', '2'};
        constexpr std::uint64_t MAX_BINARY_BYTES = 512ull * 1024 * 1024;
        constexpr std::size_t MIN_THREADED_COMPRESSION_BYTES = 8ull * 1024 * 1024;
        constexpr unsigned int MAX_COMPRESSION_WORKERS = 8;
        constexpr int ASSET_COMPRESSION_LEVEL = 3;

        void CheckZstd(const std::size_t result)
        {
            if (ZSTD_isError(result))
                throw std::runtime_error(std::string("Zstandard: ") + ZSTD_getErrorName(result));
        }
    } // namespace

    bool WriteCompressedBinaryPayload(
        const char* path, const std::array<char, 4>& magic, const std::string_view payload)
    {
        if (payload.size() > MAX_BINARY_BYTES)
        {
            std::cerr << "ERROR: Binary payload exceeds size limit: " << path << '\n';
            return false;
        }

        const auto start = std::chrono::steady_clock::now();
        std::vector<char> compressed;
        unsigned int workers = 0;
        try
        {
            if (magic == ASSET_BIN_MAGIC || magic == LEGACY_ZSTD_ASSET_BIN_MAGIC)
            {
                std::unique_ptr<ZSTD_CCtx, decltype(&ZSTD_freeCCtx)> context(ZSTD_createCCtx(), &ZSTD_freeCCtx);
                if (!context) throw std::runtime_error("Cannot allocate Zstandard compression context");
                if (payload.size() >= MIN_THREADED_COMPRESSION_BYTES)
                    workers = std::clamp(std::thread::hardware_concurrency(), 1u, MAX_COMPRESSION_WORKERS);
                CheckZstd(ZSTD_CCtx_setParameter(context.get(), ZSTD_c_compressionLevel, ASSET_COMPRESSION_LEVEL));
                CheckZstd(ZSTD_CCtx_setParameter(context.get(), ZSTD_c_nbWorkers, static_cast<int>(workers)));
                CheckZstd(ZSTD_CCtx_setParameter(context.get(), ZSTD_c_checksumFlag, 1));
                compressed.resize(ZSTD_compressBound(payload.size()));
                const auto size = ZSTD_compress2(
                    context.get(), compressed.data(), compressed.size(), payload.data(), payload.size());
                CheckZstd(size);
                compressed.resize(size);
            }
            else
            {
                int size = 0;
                std::unique_ptr<unsigned char, decltype(&MemFree)> data(
                    CompressData(
                        // raylib's DEFLATE API accepts unsigned bytes.
                        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                        reinterpret_cast<const unsigned char*>(payload.data()),
                        static_cast<int>(payload.size()),
                        &size),
                    &MemFree);
                if (!data || size <= 0) throw std::runtime_error("DEFLATE compression failed");
                compressed.resize(static_cast<std::size_t>(size));
                std::memcpy(compressed.data(), data.get(), compressed.size());
            }
        }
        catch (const std::exception& error)
        {
            std::cerr << "ERROR: Cannot compress '" << path << "': " << error.what() << '\n';
            return false;
        }
        const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

        std::ofstream storage(path, std::ios::binary);
        const auto uncompressedSize = static_cast<std::uint64_t>(payload.size());
        const auto compressedSize = static_cast<std::uint64_t>(compressed.size());
        const auto rawSizeBytes = std::bit_cast<std::array<char, sizeof(uncompressedSize)>>(uncompressedSize);
        const auto compressedSizeBytes = std::bit_cast<std::array<char, sizeof(compressedSize)>>(compressedSize);
        storage.write(magic.data(), static_cast<std::streamsize>(magic.size()));
        storage.write(rawSizeBytes.data(), static_cast<std::streamsize>(rawSizeBytes.size()));
        storage.write(compressedSizeBytes.data(), static_cast<std::streamsize>(compressedSizeBytes.size()));
        storage.write(compressed.data(), static_cast<std::streamsize>(compressed.size()));
        storage.close();
        if (!storage)
        {
            std::cerr << "ERROR: Cannot write compressed binary: " << path << '\n';
            return false;
        }
        std::cout << "  (codec="
                  << ((magic == ASSET_BIN_MAGIC || magic == LEGACY_ZSTD_ASSET_BIN_MAGIC) ? "zstd" : "deflate")
                  << " workers=" << workers << " compression=" << seconds << "s raw=" << uncompressedSize
                  << "B compressed=" << compressedSize << "B ratio="
                  << (payload.empty() ? 0.0
                                      : static_cast<double>(compressedSize) / static_cast<double>(payload.size()))
                  << ")\n";
        return true;
    }

    std::string ReadCompressedBinaryPayload(const char* path, const std::array<char, 4>& magic)
    {
        std::ifstream storage(path, std::ios::binary);
        std::array<char, 4> fileMagic{};
        std::array<char, sizeof(std::uint64_t)> rawSizeBytes{};
        std::array<char, sizeof(std::uint64_t)> compressedSizeBytes{};
        storage.read(fileMagic.data(), static_cast<std::streamsize>(fileMagic.size()));
        storage.read(rawSizeBytes.data(), static_cast<std::streamsize>(rawSizeBytes.size()));
        storage.read(compressedSizeBytes.data(), static_cast<std::streamsize>(compressedSizeBytes.size()));
        const auto uncompressedSize = std::bit_cast<std::uint64_t>(rawSizeBytes);
        const auto compressedSize = std::bit_cast<std::uint64_t>(compressedSizeBytes);
        const bool legacyAsset = magic == ASSET_BIN_MAGIC &&
                                 (fileMagic == LEGACY_ASSET_BIN_MAGIC || fileMagic == LEGACY_ZSTD_ASSET_BIN_MAGIC);
        if (!storage || (fileMagic != magic && !legacyAsset))
            throw std::runtime_error(std::string("Invalid compressed binary header: ") + path);
        if (compressedSize == 0 || compressedSize > ZSTD_compressBound(MAX_BINARY_BYTES) ||
            uncompressedSize > MAX_BINARY_BYTES)
            throw std::runtime_error(std::string("Invalid compressed binary size: ") + path);
        const auto payloadStart = storage.tellg();
        storage.seekg(0, std::ios::end);
        if (std::cmp_not_equal(storage.tellg() - payloadStart, compressedSize))
            throw std::runtime_error(std::string("Truncated compressed binary: ") + path);
        storage.seekg(payloadStart);
        std::vector<char> compressed(compressedSize);
        storage.read(compressed.data(), static_cast<std::streamsize>(compressed.size()));
        if (!storage) throw std::runtime_error(std::string("Cannot read compressed binary: ") + path);

        if (fileMagic == ASSET_BIN_MAGIC || fileMagic == LEGACY_ZSTD_ASSET_BIN_MAGIC)
        {
            const auto frameSize = ZSTD_findFrameCompressedSize(compressed.data(), compressed.size());
            CheckZstd(frameSize);
            if (frameSize != compressed.size() ||
                ZSTD_getFrameContentSize(compressed.data(), compressed.size()) != uncompressedSize)
                throw std::runtime_error(std::string("Invalid Zstandard frame size: ") + path);
            std::string payload(uncompressedSize, '\0');
            const auto size =
                ZSTD_decompress(payload.data(), payload.size(), compressed.data(), compressed.size());
            CheckZstd(size);
            if (size != uncompressedSize)
                throw std::runtime_error(std::string("Invalid decompressed binary size: ") + path);
            return payload;
        }

        int size = 0;
        std::unique_ptr<unsigned char, decltype(&MemFree)> data(
            DecompressData(
                // raylib's DEFLATE API accepts unsigned bytes.
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                reinterpret_cast<const unsigned char*>(compressed.data()),
                static_cast<int>(compressed.size()),
                &size),
            &MemFree);
        if (!data || std::cmp_not_equal(size, uncompressedSize))
            throw std::runtime_error(std::string("Cannot decompress legacy binary: ") + path);
        std::string payload(static_cast<std::size_t>(size), '\0');
        std::memcpy(payload.data(), data.get(), payload.size());
        return payload;
    }
} // namespace sage::serializer
