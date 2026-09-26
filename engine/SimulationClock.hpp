#pragma once
#include "raylib.h"
namespace sage
{
    struct SimulationClock
    {
        double seconds = 0;
        float delta = 0;
    };
    inline thread_local SimulationClock* activeSimulationClock = nullptr;
    inline float FrameTime()
    {
        return activeSimulationClock ? activeSimulationClock->delta : GetFrameTime();
    }
    inline double Time()
    {
        return activeSimulationClock ? activeSimulationClock->seconds : GetTime();
    }
    class SimulationClockScope
    {
        SimulationClock* previous;

      public:
        explicit SimulationClockScope(SimulationClock& clock) : previous(activeSimulationClock)
        {
            activeSimulationClock = &clock;
        }
        ~SimulationClockScope()
        {
            activeSimulationClock = previous;
        }
    };
} // namespace sage
