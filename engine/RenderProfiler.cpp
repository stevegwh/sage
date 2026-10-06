#include "RenderProfiler.hpp"
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_43)
#include "external/glad.h"
#endif
#include "raylib.h"
#include "rlgl.h"
#include <cstdlib>
#include <iostream>
#include <string_view>
namespace sage
{
    namespace
    {
        constexpr double NANOSECONDS_PER_MILLISECOND = 1000000.0;
        bool EnvironmentEnabled(const char* name)
        {
            const char* value = std::getenv(name);
            return value != nullptr && std::string_view(value) == "1";
        }
        bool GpuTimersAvailable()
        {
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_43)
            return (GLAD_GL_VERSION_3_3 != 0 || GLAD_GL_ARB_timer_query != 0) && glGenQueries && glBeginQuery &&
                   glEndQuery && glGetQueryObjectiv && glGetQueryObjectui64v && glDeleteQueries;
#else
            return false;
#endif
        }
    } // namespace
    unsigned int WindowMsaaFlags()
    {
        return EnvironmentEnabled("SAGE_WINDOW_MSAA") ? FLAG_MSAA_4X_HINT : 0U;
    }
    RenderProfiler::RenderProfiler(const bool forceEnabled)
        : enabled(forceEnabled || EnvironmentEnabled("SAGE_RENDER_PROFILE")),
          gpuAvailable(enabled && GpuTimersAvailable())
    {
        if (enabled)
            std::cout << "Render profiling: CPU submission ms; GPU "
                      << (gpuAvailable ? "elapsed ms" : "timers unavailable") << '\n';
    }
    RenderProfiler::~RenderProfiler()
    {
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_43)
        if (!gpuAvailable) return;
        for (auto& pass : queries)
            for (auto& query : pass)
                if (query.id != 0) glDeleteQueries(1, &query.id);
#endif
    }
    void RenderProfiler::Measure(const RenderPass pass, const std::function<void()>& draw)
    {
        if (!enabled)
        {
            draw();
            return;
        }
        const auto index = static_cast<std::size_t>(pass);
        rlDrawRenderBatchActive();
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_43)
        auto& query = queries.at(index).at(frame % queries.at(index).size());
        if (gpuAvailable && query.pending)
        {
            int available = 0;
            glGetQueryObjectiv(query.id, GL_QUERY_RESULT_AVAILABLE, &available);
            if (available != 0)
            {
                GLuint64 elapsed = 0;
                glGetQueryObjectui64v(query.id, GL_QUERY_RESULT, &elapsed);
                gpuMilliseconds.at(index) += static_cast<double>(elapsed) / NANOSECONDS_PER_MILLISECOND;
                ++gpuSamples.at(index);
                query.pending = false;
            }
        }
        const bool measureGpu = gpuAvailable && !query.pending;
        if (measureGpu)
        {
            if (query.id == 0) glGenQueries(1, &query.id);
            glBeginQuery(GL_TIME_ELAPSED, query.id);
        }
#endif
        const auto start = std::chrono::steady_clock::now();
        draw();
        rlDrawRenderBatchActive();
        cpuMilliseconds.at(index) +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_43)
        if (measureGpu)
        {
            glEndQuery(GL_TIME_ELAPSED);
            query.pending = true;
        }
#endif
    }
    void RenderProfiler::FinishFrame(const int sceneWidth, const int sceneHeight)
    {
        const auto start = std::chrono::steady_clock::now();
        EndDrawing();
        if (!enabled) return;
        waitMilliseconds +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        ++frame;
        constexpr std::size_t REPORT_FRAMES = 120;
        if (frame % REPORT_FRAMES != 0) return;
        constexpr std::array<const char*, PASS_COUNT> NAMES{
            "shadows", "scene", "bloom", "ao", "post", "ui", "present"};
        std::cout << "Render scene=" << sceneWidth << 'x' << sceneHeight << " display=" << GetScreenWidth() << 'x'
                  << GetScreenHeight() << " framebuffer=" << GetRenderWidth() << 'x' << GetRenderHeight()
                  << " (CPU/GPU ms):";
        for (std::size_t index = 0; index < PASS_COUNT; ++index)
        {
            std::cout << ' ' << NAMES.at(index) << '=' << cpuMilliseconds.at(index) / REPORT_FRAMES << '/';
            if (gpuSamples.at(index) != 0)
                std::cout << gpuMilliseconds.at(index) / static_cast<double>(gpuSamples.at(index));
            else
                std::cout << "n/a";
        }
        std::cout << " swap/wait=" << waitMilliseconds / REPORT_FRAMES << '\n' << std::flush;
        cpuMilliseconds.fill(0.0);
        gpuMilliseconds.fill(0.0);
        gpuSamples.fill(0);
        waitMilliseconds = 0.0;
    }
} // namespace sage
