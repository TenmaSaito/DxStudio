#pragma once

// Frame time shared by components and coroutines (Unity: UnityEngine.Time).
// Advanced once per frame by dxs::UpdateUserComponents.

#include <cstdint>

namespace dxs {

class Time {
public:
    // Scaled seconds of the current frame.
    static float DeltaTime() noexcept { return s_fDeltaTime * s_fTimeScale; }
    // Unscaled seconds of the current frame.
    static float UnscaledDeltaTime() noexcept { return s_fDeltaTime; }
    // Scaled seconds since Play started (or since the game launched).
    static double TimeSinceStart() noexcept { return s_dTime; }
    static std::uint64_t FrameCount() noexcept { return s_nFrameCount; }

    static float TimeScale() noexcept { return s_fTimeScale; }
    static void SetTimeScale(float fScale) noexcept { s_fTimeScale = fScale < 0.0f ? 0.0f : fScale; }

    // Engine internals.
    static void Advance(float fUnscaledDeltaSeconds) noexcept {
        s_fDeltaTime = fUnscaledDeltaSeconds < 0.0f ? 0.0f : fUnscaledDeltaSeconds;
        s_dTime += static_cast<double>(s_fDeltaTime) * s_fTimeScale;
        ++s_nFrameCount;
    }
    static void Reset() noexcept {
        s_fDeltaTime = 0.0f;
        s_dTime = 0.0;
        s_nFrameCount = 0;
        s_fTimeScale = 1.0f;
    }

private:
    static inline float s_fDeltaTime = 0.0f;
    static inline double s_dTime = 0.0;
    static inline std::uint64_t s_nFrameCount = 0;
    static inline float s_fTimeScale = 1.0f;
};

} // namespace dxs
