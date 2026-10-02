#pragma once
#include "raylib.h"

#include <functional>
#include <optional>
namespace sage
{
    struct SimulationClock
    {
        double seconds = 0;
        float delta = 0;
    };
    inline thread_local std::optional<std::reference_wrapper<SimulationClock>> activeSimulationClock;
    inline float FrameTime()
    {
        return activeSimulationClock ? activeSimulationClock->get().delta : GetFrameTime();
    }
    inline double Time()
    {
        return activeSimulationClock ? activeSimulationClock->get().seconds : GetTime();
    }
    class SimulationClockScope
    {
        std::optional<std::reference_wrapper<SimulationClock>> previous;

      public:
        explicit SimulationClockScope(SimulationClock& clock) : previous(activeSimulationClock)
        {
            activeSimulationClock = std::ref(clock);
        }
        SimulationClockScope(const SimulationClockScope&) = delete;
        SimulationClockScope& operator=(const SimulationClockScope&) = delete;
        SimulationClockScope(SimulationClockScope&&) = delete;
        SimulationClockScope& operator=(SimulationClockScope&&) = delete;
        ~SimulationClockScope()
        {
            activeSimulationClock = previous;
        }
    };
} // namespace sage
