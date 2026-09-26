#pragma once
#include "cereal/archives/json.hpp"
#include "cereal/external/rapidjson/document.h"
#include "cereal/external/rapidjson/error/en.h"
#include "cereal/external/rapidjson/pointer.h"
#include "cereal/external/rapidjson/prettywriter.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace sage::json
{
    using Value = rapidjson::Value;
    using Document = rapidjson::Document;
    using Allocator = Document::AllocatorType;
    inline Document Parse(const std::string& text)
    {
        Document value;
        value.Parse(text.c_str());
        if (value.HasParseError())
            throw std::runtime_error(
                "Invalid JSON at byte " + std::to_string(value.GetErrorOffset()) + ": " +
                rapidjson::GetParseError_En(value.GetParseError()));
        return value;
    }
    inline void Sort(Value& value, Allocator& allocator)
    {
        if (value.IsArray())
            for (auto& item : value.GetArray())
                Sort(item, allocator);
        if (!value.IsObject()) return;
        std::vector<std::string> names;
        for (auto& member : value.GetObject())
        {
            Sort(member.value, allocator);
            names.emplace_back(member.name.GetString());
        }
        std::ranges::sort(names);
        Value ordered(rapidjson::kObjectType);
        for (const auto& name : names)
            ordered.AddMember(
                Value(name.c_str(), allocator), Value().CopyFrom(value[name.c_str()], allocator), allocator);
        value.Swap(ordered);
    }
    inline std::string Stringify(const Value& value)
    {
        Document copy;
        copy.CopyFrom(value, copy.GetAllocator());
        Sort(copy, copy.GetAllocator());
        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        writer.SetIndent(' ', 2);
        copy.Accept(writer);
        return std::string(buffer.GetString()) + "\n";
    }
    inline std::string Read(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open " + path.string());
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }
    inline void Append(Value& array, const Value& value, Allocator& allocator)
    {
        Value copy;
        copy.CopyFrom(value, allocator);
        array.PushBack(copy, allocator);
    }
    inline void Put(Value& object, const char* key, const Value& value, Allocator& allocator)
    {
        Value copy;
        copy.CopyFrom(value, allocator);
        if (object.HasMember(key))
            object[key].Swap(copy);
        else
            object.AddMember(Value(key, allocator), copy, allocator);
    }
    inline void Put(Value& object, const char* key, const std::string& value, Allocator& allocator)
    {
        Put(object,
            key,
            Value(value.c_str(), static_cast<rapidjson::SizeType>(value.size()), allocator),
            allocator);
    }
    inline void Put(Value& object, const char* key, const char* value, Allocator& allocator)
    {
        Put(object, key, std::string(value), allocator);
    }
    inline void Put(Value& object, const char* key, std::uint64_t value, Allocator& allocator)
    {
        Put(object, key, Value(value), allocator);
    }
    inline void Put(Value& object, const char* key, bool value, Allocator& allocator)
    {
        Put(object, key, Value(value), allocator);
    }
    template <class T>
    Document Encode(const T& value)
    {
        std::ostringstream stream;
        {
            cereal::JSONOutputArchive output(stream);
            output(cereal::make_nvp("data", value));
        }
        auto wrapper = Parse(stream.str());
        Document result;
        result.CopyFrom(wrapper["data"], result.GetAllocator());
        return result;
    }
    template <class T>
    void Decode(const Value& value, T& result)
    {
        Document wrapper(rapidjson::kObjectType);
        Put(wrapper, "data", value, wrapper.GetAllocator());
        std::istringstream stream(Stringify(wrapper));
        cereal::JSONInputArchive input(stream);
        input(cereal::make_nvp("data", result));
    }
    inline const Value& Require(const Value& object, const char* key)
    {
        if (!object.IsObject() || !object.HasMember(key))
            throw std::runtime_error(std::string("Missing field: ") + key);
        return object[key];
    }
    inline std::string String(const Value& object, const char* key)
    {
        const auto& value = Require(object, key);
        if (!value.IsString()) throw std::runtime_error(std::string("Expected string: ") + key);
        return value.GetString();
    }
    inline std::uint32_t Id(const Value& object, const char* key)
    {
        const auto& value = Require(object, key);
        if (!value.IsUint()) throw std::runtime_error(std::string("Expected entity ID: ") + key);
        return value.GetUint();
    }
} // namespace sage::json
