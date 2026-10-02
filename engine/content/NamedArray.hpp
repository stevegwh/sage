#pragma once
#include "cereal/archives/json.hpp"
#include "cereal/types/array.hpp"
#include <functional>
#include <stdexcept>

namespace sage::content
{
    // Cereal's fixed-array JSON uses value0/value1 object members by default.
    // A size tag makes persistent collections actual JSON arrays without changing binary bytes.
    template <class Array>
    struct ArrayValue
    {
        std::reference_wrapper<Array> values;
        template <class Archive>
        void serialize(Archive& archive)
        {
            if constexpr (
                std::is_same_v<Archive, cereal::JSONInputArchive> ||
                std::is_same_v<Archive, cereal::JSONOutputArchive>)
            {
                cereal::size_type size = values.get().size();
                archive(cereal::make_size_tag(size));
                if (size != values.get().size()) throw std::runtime_error("Invalid fixed array length");
                for (auto& value : values.get())
                    archive(value);
            }
            else
                archive(values.get());
        }
    };
    template <class Array>
    ArrayValue<Array> NamedArray(Array& values)
    {
        return {std::ref(values)};
    }
} // namespace sage::content
