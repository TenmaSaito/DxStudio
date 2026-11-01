//==================================================================================
//
// フレーム時間クラスのヘッダーファイル [Time.h]
//
// コンポーネントとコルーチンが共通で使う時間（Unity の UnityEngine.Time 相当）。
// dxs::UpdateUserComponents が毎フレーム 1 回 Advance() で進める。
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include <cstdint>

namespace dxs {

//**********************************************************************************
// *** フレーム時間クラス ***
//**********************************************************************************
class Time {
public:
    // 今フレームの経過秒（TimeScale を掛けた値）
    static float DeltaTime() noexcept { return s_fDeltaTime * s_fTimeScale; }

    // 今フレームの経過秒（TimeScale の影響を受けない実時間）
    static float UnscaledDeltaTime() noexcept { return s_fDeltaTime; }

    // 再生開始（ゲーム起動）からの経過秒（TimeScale を掛けた値）
    static double TimeSinceStart() noexcept { return s_dTime; }

    // 経過フレーム数
    static std::uint64_t FrameCount() noexcept { return s_nFrameCount; }

    // 時間の進む速さ（1 = 等速、0 = 停止、0.5 = スロー）
    static float TimeScale() noexcept { return s_fTimeScale; }

    static void SetTimeScale(float fScale) noexcept { s_fTimeScale = fScale < 0.0f ? 0.0f : fScale; } // 負の値は 0 にする

    // ---- エンジン内部用 ----------------------------------------------------
    // 1 フレーム分の実時間を渡して時間を進める
    static void Advance(float fUnscaledDeltaSeconds) noexcept {
        s_fDeltaTime = fUnscaledDeltaSeconds < 0.0f ? 0.0f : fUnscaledDeltaSeconds; // 負の経過時間は 0 扱い
        s_dTime += static_cast<double>(s_fDeltaTime) * s_fTimeScale;
        ++s_nFrameCount;
    }

    // 再生開始時などに初期状態へ戻す
    static void Reset() noexcept {
        s_fDeltaTime = 0.0f;
        s_dTime = 0.0;
        s_nFrameCount = 0;
        s_fTimeScale = 1.0f;
    }

private:
    static inline float s_fDeltaTime = 0.0f;       // 今フレームの実経過秒
    static inline double s_dTime = 0.0;            // 開始からの経過秒（スケール済み）
    static inline std::uint64_t s_nFrameCount = 0; // 経過フレーム数
    static inline float s_fTimeScale = 1.0f;       // 時間の進む速さ
};

} // namespace dxs
