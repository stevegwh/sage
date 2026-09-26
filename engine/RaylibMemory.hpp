#pragma once

#include "raylib.h"

#include <cstddef>
#include <cstring>
#include <limits>
#include <new>

namespace sage
{
    // Raylib frees these buffers itself, so they must use its allocator.
    inline void* AllocateZeroedMemory(std::size_t count, std::size_t elementSize)
    {
        if (elementSize != 0 && count > std::numeric_limits<unsigned int>::max() / elementSize)
            throw std::bad_alloc();
        const auto bytes = static_cast<unsigned int>(count * elementSize);
        auto* memory = MemAlloc(bytes);
        if (memory != nullptr) std::memset(memory, 0, bytes);
        return memory;
    }
} // namespace sage
