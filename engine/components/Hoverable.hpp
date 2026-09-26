#pragma once

#include "cereal/cereal.hpp"

#include "cereal/types/string.hpp"

#include <string>
#include <utility>

namespace sage
{
    struct Hoverable
    {
        std::string label;

        Hoverable() = default;
        explicit Hoverable(std::string _label) : label(std::move(_label))
        {
        }

        template <class Archive>
        void serialize(Archive& archive)
        {
            archive(cereal::make_nvp("label", label));
        }

        template <class Inspector>
        void define_editor_options(Inspector& inspector)
        {
            inspector.field("label", "Label", label);
        }

        template <class Persistence>
        static void define_persistence(Persistence& persistence)
        {
            persistence.key("sage.Hoverable");
        }
    };
} // namespace sage
