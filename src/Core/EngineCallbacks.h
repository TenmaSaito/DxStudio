#pragma once

// ---------------------------------------------------------------------------
// Engine-wide callbacks.
//
// Two equivalent ways to receive engine notifications:
//
//  1) Delegates (fine-grained, UE style):
//       auto handle = dxs::EngineEvents::Get().onSceneLoaded.AddLambda(
//           [](dxs::Scene& scene) { ... });
//
//  2) Interface classes (grouped, Unity "I...Handler" style): derive from one
//     or more of the interfaces below, override what you need and keep the
//     returned registration alive. It unsubscribes automatically.
//       class CMyTool final : public dxs::IPlayModeCallbacks { ... };
//       CMyTool tool;
//       dxs::CallbackRegistration registration = dxs::RegisterCallbacks(tool);
// ---------------------------------------------------------------------------

#include "Delegate.h"
#include "../Graphics/GraphicsApi.h"

#include <concepts>
#include <cstddef>
#include <string>
#include <vector>

namespace dxs {

struct Scene;
struct Entity;
class Component;

// ---- Callback interfaces ------------------------------------------------------
class ISceneCallbacks {
public:
    virtual ~ISceneCallbacks() = default;
    virtual void OnSceneLoaded(Scene& /*scene*/) {}
    virtual void OnSceneSaved(const Scene& /*scene*/) {}
};

class IFrameCallbacks {
public:
    virtual ~IFrameCallbacks() = default;
    // Before any Component::Update of the frame.
    virtual void OnPreUpdate(Scene& /*scene*/, float /*fDeltaSeconds*/) {}
    // After every Component::Update and coroutine of the frame.
    virtual void OnPostUpdate(Scene& /*scene*/, float /*fDeltaSeconds*/) {}
};

class IComponentCallbacks {
public:
    virtual ~IComponentCallbacks() = default;
    virtual void OnComponentStarted(Entity& /*entity*/, Component& /*component*/, const std::string& /*sName*/) {}
    virtual void OnComponentAttached(Entity& /*entity*/, const std::string& /*sName*/) {}
    virtual void OnComponentDetached(Entity& /*entity*/, const std::string& /*sName*/) {}
};

class IPlayModeCallbacks {
public:
    virtual ~IPlayModeCallbacks() = default;
    virtual void OnEnterPlayMode(Scene& /*scene*/) {}
    virtual void OnExitPlayMode(Scene& /*scene*/) {}
    virtual void OnPauseChanged(bool /*bPaused*/) {}
};

class ISelectionCallbacks {
public:
    virtual ~ISelectionCallbacks() = default;
    virtual void OnSelectionChanged(const std::vector<int>& /*vSelection*/, int /*nActive*/) {}
};

class IGraphicsCallbacks {
public:
    virtual ~IGraphicsCallbacks() = default;
    virtual void OnGraphicsApiChanged(GraphicsApi /*api*/) {}
};

// ---- Event hub ------------------------------------------------------------------
class EngineEvents {
public:
    static EngineEvents& Get();

    MulticastDelegate<Scene&> onSceneLoaded;
    MulticastDelegate<const Scene&> onSceneSaved;
    MulticastDelegate<Scene&, float> onPreUpdate;
    MulticastDelegate<Scene&, float> onPostUpdate;
    MulticastDelegate<Entity&, Component&, const std::string&> onComponentStarted;
    MulticastDelegate<Entity&, const std::string&> onComponentAttached;
    MulticastDelegate<Entity&, const std::string&> onComponentDetached;
    MulticastDelegate<Scene&> onEnterPlayMode;
    MulticastDelegate<Scene&> onExitPlayMode;
    MulticastDelegate<bool> onPauseChanged;
    MulticastDelegate<const std::vector<int>&, int> onSelectionChanged;
    MulticastDelegate<GraphicsApi> onGraphicsApiChanged;

private:
    EngineEvents() = default;
};

// RAII group of subscriptions created by RegisterCallbacks().
using CallbackRegistration = DelegateConnections;

// Subscribes every interface implemented by `listener`. The listener must
// outlive the returned registration (store both in the same owner).
template<class T>
[[nodiscard]] CallbackRegistration RegisterCallbacks(T& listener) {
    CallbackRegistration registration;
    auto& events = EngineEvents::Get();
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
