#pragma once
#include <array>
#include <cstddef>
#include <functional>
namespace sage
{
    enum class RenderPass : std::size_t
    {
        Shadows,
        Scene,
        Bloom,
        Occlusion,
        PostProcess,
        Ui,
        Present,
        Count
    };
    // Enabled with SAGE_RENDER_PROFILE=1. GPU results are read asynchronously.
    class RenderProfiler
    {
        static constexpr std::size_t PASS_COUNT = static_cast<std::size_t>(RenderPass::Count);
        static constexpr std::size_t QUERY_BUFFER_FRAMES = 8;
        struct Query
        {
            unsigned int id = 0;
            bool pending = false;
        };
        struct PassTimings
        {
            std::array<Query, QUERY_BUFFER_FRAMES> queries{};
            double cpuMilliseconds = 0.0;
            double gpuMilliseconds = 0.0;
            std::size_t gpuSamples = 0;
        };
        std::array<PassTimings, PASS_COUNT> timings{};
        std::size_t frame = 0;
        bool enabled;
        bool gpuAvailable;
        double waitMilliseconds = 0.0;

      public:
        explicit RenderProfiler(bool forceEnabled = false);
        ~RenderProfiler();
        RenderProfiler(const RenderProfiler&) = delete;
        RenderProfiler(RenderProfiler&&) = delete;
        RenderProfiler& operator=(RenderProfiler&&) = delete;
        RenderProfiler& operator=(const RenderProfiler&) = delete;
        void Measure(RenderPass pass, const std::function<void()>& draw);
        void FinishFrame(int sceneWidth, int sceneHeight);
        [[nodiscard]] bool HasGpuTimings() const
        {
            return gpuAvailable;
        }
    };
    // Offscreen scene targets use FXAA; window MSAA can be enabled for comparison.
    unsigned int WindowMsaaFlags();
} // namespace sage
