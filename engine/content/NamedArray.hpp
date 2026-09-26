#pragma once
#include "cereal/archives/json.hpp"
#include "cereal/types/array.hpp"
#include <stdexcept>

namespace sage::content
{
    // Cereal's fixed-array JSON uses value0/value1 object members by default.
    // A size tag makes persistent collections actual JSON arrays without changing binary bytes.
    template <class Array>
    struct ArrayValue
    {
        Array& values;
        template <class Archive>
        void serialize(Archive& archive)
        {
            if constexpr (
                std::is_same_v<Archive, cereal::JSONInputArchive> ||
                std::is_same_v<Archive, cereal::JSONOutputArchive>)
            {
                cereal::size_type size = values.size();
                archive(cereal::make_size_tag(size));
                if (size != values.size()) throw std::runtime_error("Invalid fixed array length");
                for (auto& value : values)
                    archive(value);
            }
            else
                archive(values);
        }
    };
    template <class Array>
    ArrayValue<Array> NamedArray(Array& values)
    {
        return {values};
    }
} // namespace sage::content
