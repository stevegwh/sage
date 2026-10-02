#include "engine/Serializer.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <random>

namespace
{
    constexpr std::size_t BINARY_HEADER_BYTES = 20;
    constexpr unsigned int RANDOM_SEED = 42;
    constexpr std::size_t NOISE_BYTES = 1024 * 1024;
    constexpr unsigned int BYTE_MASK = 0xff;
    void Require(const bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    template <typename Function>
    void MustFail(Function&& function)
    {
        try
        {
            std::forward<Function>(function)();
        }
        catch (const std::runtime_error&)
        {
            return;
        }
        throw std::runtime_error("Malformed archive was accepted");
    }

    std::string ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    void WriteBytes(const std::filesystem::path& path, const std::string& bytes)
    {
        std::ofstream output(path, std::ios::binary);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        Require(static_cast<bool>(output), "Cannot write test fixture");
    }

    void TestArchives(const std::filesystem::path& folder)
    {
        using namespace sage::serializer;
        const auto file = (folder / "assets.bin").string();
        const std::string large(16ull * 1024 * 1024, 'x');
        Require(WriteCompressedBinaryPayload(file.c_str(), ASSET_BIN_MAGIC, large), "Asset write failed");
        Require(ReadCompressedBinaryPayload(file.c_str(), ASSET_BIN_MAGIC) == large, "Large round trip failed");
        const auto valid = ReadBytes(file);
        Require(valid.starts_with("LQB4"), "Asset format was not versioned");
        Require(
            valid.substr(BINARY_HEADER_BYTES, 4) == "\x28\xb5\x2f\xfd", "Asset payload is not a Zstandard frame");

        std::mt19937 random(RANDOM_SEED);
        std::string noise(NOISE_BYTES, '\0');
        for (auto& byte : noise)
            byte = static_cast<char>(random() & BYTE_MASK);
        for (const auto& payload : {std::string{}, std::string("a\0b", 3), noise})
        {
            Require(WriteCompressedBinaryPayload(file.c_str(), ASSET_BIN_MAGIC, payload), "Asset write failed");
            Require(
                ReadCompressedBinaryPayload(file.c_str(), ASSET_BIN_MAGIC) == payload, "Byte round trip failed");
        }

        // Exercise multiple compression jobs with data that cannot collapse to a tiny frame.
        std::string threadedNoise(large.size(), '\0');
        for (auto& byte : threadedNoise)
            byte = static_cast<char>(random() & BYTE_MASK);
        Require(
            WriteCompressedBinaryPayload(file.c_str(), ASSET_BIN_MAGIC, threadedNoise),
            "Threaded asset write failed");
        Require(
            ReadCompressedBinaryPayload(file.c_str(), ASSET_BIN_MAGIC) == threadedNoise,
            "Threaded incompressible round trip failed");

        const std::vector<std::string> values = {"texture", "model", std::string("a\0b", 3)};
        Require(SaveClassBinary(file.c_str(), values), "Cereal archive write failed");
        std::vector<std::string> loaded;
        ReadCompressedBinary(file.c_str(), ASSET_BIN_MAGIC, [&](auto& archive, auto&) { archive(loaded); });
        Require(loaded == values, "Cereal archive round trip failed");

        constexpr std::array<char, 4> LEGACY_ASSET_MAGIC = {'L', 'Q', 'B', '2'};
        Require(
            WriteCompressedBinaryPayload(file.c_str(), LEGACY_ASSET_MAGIC, noise), "Legacy fixture write failed");
        Require(ReadCompressedBinaryPayload(file.c_str(), ASSET_BIN_MAGIC) == noise, "Legacy asset read failed");
        constexpr std::array<char, 4> LEGACY_MAP_MAGIC = {'L', 'Q', 'E', '6'};
        for (const auto& magic : {MAP_BIN_MAGIC, LEGACY_MAP_MAGIC})
        {
            Require(WriteCompressedBinaryPayload(file.c_str(), magic, noise), "Map write failed");
            Require(ReadCompressedBinaryPayload(file.c_str(), magic) == noise, "Map round trip failed");
            MustFail([&] { ReadCompressedBinaryPayload(file.c_str(), ASSET_BIN_MAGIC); });
        }

        const auto invalid = (folder / "invalid.bin").string();
        for (const auto length : {std::size_t{0}, std::size_t{4}, BINARY_HEADER_BYTES - 1, valid.size() - 1})
        {
            WriteBytes(invalid, valid.substr(0, length));
            MustFail([&] { ReadCompressedBinaryPayload(invalid.c_str(), ASSET_BIN_MAGIC); });
        }
        WriteBytes(invalid, valid + "trailing data");
        MustFail([&] { ReadCompressedBinaryPayload(invalid.c_str(), ASSET_BIN_MAGIC); });
        auto corrupt = valid;
        corrupt.back() ^= 1; // Corrupt the frame checksum while retaining valid sizes.
        WriteBytes(invalid, corrupt);
        MustFail([&] { ReadCompressedBinaryPayload(invalid.c_str(), ASSET_BIN_MAGIC); });
        corrupt = valid;
        corrupt.at(4) ^= 1; // Header and frame disagree about the decompressed size.
        WriteBytes(invalid, corrupt);
        MustFail([&] { ReadCompressedBinaryPayload(invalid.c_str(), ASSET_BIN_MAGIC); });
        corrupt = valid;
        constexpr std::uint64_t EXCESSIVE_SIZE = 513ull * 1024 * 1024;
        std::memcpy(&corrupt.at(4), &EXCESSIVE_SIZE, sizeof(EXCESSIVE_SIZE));
        WriteBytes(invalid, corrupt);
        MustFail([&] { ReadCompressedBinaryPayload(invalid.c_str(), ASSET_BIN_MAGIC); });
        const auto missing = (folder / "missing" / "assets.bin").string();
        Require(
            !WriteCompressedBinaryPayload(missing.c_str(), ASSET_BIN_MAGIC, noise),
            "Failed write reported success");
        MustFail([&] { ReadCompressedBinaryPayload(missing.c_str(), ASSET_BIN_MAGIC); });
    }
} // namespace

int main()
{
    const auto folder =
        std::filesystem::temp_directory_path() /
        ("sage-compression-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try
    {
        std::filesystem::create_directory(folder);
        TestArchives(folder);
        std::filesystem::remove_all(folder);
        std::cout << "Compressed binary tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::filesystem::remove_all(folder);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
