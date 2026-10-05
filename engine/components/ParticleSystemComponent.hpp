#pragma once

#include "engine/components/sgTransform.hpp"

namespace sage
{
    // Identifies an effect root. Emitters remain ordinary entities in its transform subtree.
    struct ParticleSystemComponent
    {
        // EnTT must retain an addressable instance for the generic Inspector and content codecs.
        static constexpr std::size_t page_size = 1;

        template <class Archive>
        void serialize(Archive&)
        {
        }

        template <class Inspector>
        void define_editor_options(Inspector& i)
        {
            i.template requiresComponent<sgTransform>();
            i.note("Emitters", "Add child entities with a Particle Emitter component.");
        }
    };
} // namespace sage
