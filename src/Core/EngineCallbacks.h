//==================================================================================
//
// エンジン全体のイベント通知のヘッダーファイル [EngineCallbacks.h]
//
// エンジンからの通知は、次のどちらの方法でも受け取れる（中身は同じ）。
//
//  1) デリゲート（イベントごとに登録。UE 風）:
//       auto handle = dxs::EngineEvents::Get().onSceneLoaded.AddLambda(
//           [](dxs::Scene& scene) { ... });
//
//  2) インターフェースクラス（まとめて登録。Unity の「I...Handler」風）:
//     下のインターフェースを 1 つ以上継承し、必要な関数だけオーバーライドして、
//     RegisterCallbacks() の戻り値を持ち続ける（破棄すると自動で解除される）。
//       class CMyTool final : public dxs::IPlayModeCallbacks { ... };
//       CMyTool tool;
//       dxs::CallbackRegistration registration = dxs::RegisterCallbacks(tool);
//
//==================================================================================
#pragma once

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "Delegate.h"
#include "../Graphics/GraphicsApi.h"

#include <concepts>
#include <cstddef>
#include <string>
#include <vector>

namespace dxs {

//**********************************************************************************
// *** 前方宣言 ***
//**********************************************************************************
struct Scene;
struct Entity;
class Component;

//**********************************************************************************
// *** 通知を受け取るインターフェース ***
//**********************************************************************************
// シーンの読み込み・保存
class ISceneCallbacks {
public:
    virtual ~ISceneCallbacks() = default;

    virtual void OnSceneLoaded(Scene& /*scene*/) {} // 読み込み後

    virtual void OnSceneSaved(const Scene& /*scene*/) {} // 保存後
};

// フレームの前後
class IFrameCallbacks {
public:
    virtual ~IFrameCallbacks() = default;

    // そのフレームのどの Component::Update よりも前
    virtual void OnPreUpdate(Scene& /*scene*/, float /*fDeltaSeconds*/) {}

    // そのフレームのすべての Component::Update とコルーチンの後
    virtual void OnPostUpdate(Scene& /*scene*/, float /*fDeltaSeconds*/) {}
};

// コンポーネントの開始・追加・削除
class IComponentCallbacks {
public:
    virtual ~IComponentCallbacks() = default;

    virtual void OnComponentStarted(Entity& /*entity*/, Component& /*component*/, const std::string& /*sName*/) {} // Start() の後

    virtual void OnComponentAttached(Entity& /*entity*/, const std::string& /*sName*/) {} // 追加された

    virtual void OnComponentDetached(Entity& /*entity*/, const std::string& /*sName*/) {} // 外された
};

// 再生モード（Play / Stop / Pause）
class IPlayModeCallbacks {
public:
    virtual ~IPlayModeCallbacks() = default;

    virtual void OnEnterPlayMode(Scene& /*scene*/) {} // 再生開始

    virtual void OnExitPlayMode(Scene& /*scene*/) {} // 再生停止

    virtual void OnPauseChanged(bool /*bPaused*/) {} // 一時停止の切り替え
};

// エディターの選択
class ISelectionCallbacks {
public:
    virtual ~ISelectionCallbacks() = default;

    virtual void OnSelectionChanged(const std::vector<int>& /*vSelection*/, int /*nActive*/) {
    } // 選択中の番号一覧と、主に選択中の番号
};

// グラフィックス API の切り替え
class IGraphicsCallbacks {
public:
    virtual ~IGraphicsCallbacks() = default;

    virtual void OnGraphicsApiChanged(GraphicsApi /*api*/) {}
};

//**********************************************************************************
// *** イベント通知のまとめ（デリゲート版） ***
//**********************************************************************************
class EngineEvents {
public:
    static EngineEvents& Get(); // プログラム全体で 1 つ

    MulticastDelegate<Scene&> onSceneLoaded;                                       // シーン読み込み後
    MulticastDelegate<const Scene&> onSceneSaved;                                  // シーン保存後
    MulticastDelegate<Scene&, float> onPreUpdate;                                  // 更新の前（経過秒）
    MulticastDelegate<Scene&, float> onPostUpdate;                                 // 更新の後（経過秒）
    MulticastDelegate<Entity&, Component&, const std::string&> onComponentStarted; // コンポーネント開始
    MulticastDelegate<Entity&, const std::string&> onComponentAttached;            // コンポーネント追加
    MulticastDelegate<Entity&, const std::string&> onComponentDetached;            // コンポーネント削除
    MulticastDelegate<Scene&> onEnterPlayMode;                                     // 再生開始
    MulticastDelegate<Scene&> onExitPlayMode;                                      // 再生停止
    MulticastDelegate<bool> onPauseChanged;                                        // 一時停止の切り替え
    MulticastDelegate<const std::vector<int>&, int> onSelectionChanged;            // 選択の変更
    MulticastDelegate<GraphicsApi> onGraphicsApiChanged;                           // グラフィックス API の変更

private:
    EngineEvents() = default; // Get() 以外からの生成を禁止
};

// RegisterCallbacks() が返す、自動解除される登録のまとめ
using CallbackRegistration = DelegateConnections;

//==================================================================================
// --- インターフェースの一括登録処理 ---
//==================================================================================
// listener が継承しているインターフェースをすべて登録する。
// listener は戻り値より長く生きている必要がある（同じ持ち主に両方を持たせる）。
template<class T>
[[nodiscard]] CallbackRegistration RegisterCallbacks(T& listener) {
    CallbackRegistration registration;
    auto& events = EngineEvents::Get();

    // 継承しているインターフェースだけをコンパイル時に選んで登録する
    if constexpr (std::derived_from<T, ISceneCallbacks>) {
        ISceneCallbacks* pListener = &listener;
        registration.Add(events.onSceneLoaded, events.onSceneLoaded.AddRaw(pListener, &ISceneCallbacks::OnSceneLoaded));
        registration.Add(events.onSceneSaved, events.onSceneSaved.AddRaw(pListener, &ISceneCallbacks::OnSceneSaved));
    }

    if constexpr (std::derived_from<T, IFrameCallbacks>) {
        IFrameCallbacks* pListener = &listener;
        registration.Add(events.onPreUpdate, events.onPreUpdate.AddRaw(pListener, &IFrameCallbacks::OnPreUpdate));
        registration.Add(events.onPostUpdate, events.onPostUpdate.AddRaw(pListener, &IFrameCallbacks::OnPostUpdate));
    }

    if constexpr (std::derived_from<T, IComponentCallbacks>) {
        IComponentCallbacks* pListener = &listener;
        registration.Add(events.onComponentStarted,
                         events.onComponentStarted.AddRaw(pListener, &IComponentCallbacks::OnComponentStarted));
        registration.Add(events.onComponentAttached,
                         events.onComponentAttached.AddRaw(pListener, &IComponentCallbacks::OnComponentAttached));
        registration.Add(events.onComponentDetached,
                         events.onComponentDetached.AddRaw(pListener, &IComponentCallbacks::OnComponentDetached));
    }

    if constexpr (std::derived_from<T, IPlayModeCallbacks>) {
        IPlayModeCallbacks* pListener = &listener;
        registration.Add(events.onEnterPlayMode, events.onEnterPlayMode.AddRaw(pListener, &IPlayModeCallbacks::OnEnterPlayMode));
        registration.Add(events.onExitPlayMode, events.onExitPlayMode.AddRaw(pListener, &IPlayModeCallbacks::OnExitPlayMode));
        registration.Add(events.onPauseChanged, events.onPauseChanged.AddRaw(pListener, &IPlayModeCallbacks::OnPauseChanged));
    }

    if constexpr (std::derived_from<T, ISelectionCallbacks>) {
        ISelectionCallbacks* pListener = &listener;
        registration.Add(events.onSelectionChanged,
                         events.onSelectionChanged.AddRaw(pListener, &ISelectionCallbacks::OnSelectionChanged));
    }

    if constexpr (std::derived_from<T, IGraphicsCallbacks>) {
        IGraphicsCallbacks* pListener = &listener;
        registration.Add(events.onGraphicsApiChanged,
                         events.onGraphicsApiChanged.AddRaw(pListener, &IGraphicsCallbacks::OnGraphicsApiChanged));
    }

    return registration;
}

} // namespace dxs
