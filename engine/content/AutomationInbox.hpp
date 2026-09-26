#pragma once
#include "Json.hpp"
#include <chrono>
#include <cstdlib>
#include <functional>

namespace sage::content
{
    // Local file transport: requests are handled on the UI thread, never from a
    // network thread. Each request has its own atomic response and a revision check.
    class AutomationInbox
    {
        std::filesystem::path directory;
        static void Write(const std::filesystem::path& path, const json::Value& value)
        {
            const auto temporary = path.string() + ".writing";
            {
                std::ofstream output(temporary);
                output << json::Stringify(value);
                output.close();
                if (!output) throw std::runtime_error("Cannot write automation response");
            }
            std::filesystem::rename(temporary, path);
        }

      public:
        AutomationInbox()
        {
            if (const auto* configured = std::getenv("SAGE_AGENT_SESSION"))
                directory = configured;
            else
                directory = std::filesystem::path(".cache/herder-editor") /
                            std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
            std::filesystem::create_directories(directory);
            json::Document session(rapidjson::kObjectType);
            auto& a = session.GetAllocator();
            json::Put(session, "protocol", std::uint64_t(1), a);
            json::Put(session, "directory", std::filesystem::absolute(directory).string(), a);
            Write(directory / "session.json", session);
        }
        ~AutomationInbox()
        {
            std::error_code error;
            std::filesystem::remove(directory / "session.json", error);
        }
        const std::filesystem::path& Directory() const
        {
            return directory;
        }
        void Poll(const std::function<json::Document(const json::Value&)>& handler) const
        {
            std::vector<std::filesystem::path> pending;
            for (const auto& entry : std::filesystem::directory_iterator(directory))
                if (entry.is_regular_file() && entry.path().filename().string().ends_with(".request.json"))
                    pending.push_back(entry.path());
            std::ranges::sort(pending);
            for (const auto& path : pending)
            {
                json::Document response(rapidjson::kObjectType);
                auto& a = response.GetAllocator();
                try
                {
                    auto request = json::Parse(json::Read(path));
                    auto result = handler(request);
                    json::Put(response, "ok", true, a);
                    json::Put(response, "result", result, a);
                }
                catch (const std::exception& error)
                {
                    json::Put(response, "ok", false, a);
                    json::Put(response, "error", error.what(), a);
                }
                const auto name = path.filename().string();
                const auto output = directory / (name.substr(0, name.size() - 13) + ".response.json");
                Write(output, response);
                std::filesystem::remove(path);
            }
        }
    };
    inline std::string Revision(const json::Value& document)
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (unsigned char byte : json::Stringify(document))
        {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        return std::to_string(hash);
    }
} // namespace sage::content
