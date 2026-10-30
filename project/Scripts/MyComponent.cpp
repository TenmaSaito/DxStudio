#include "MyComponent.h"

void Spin::Update(dxs::Entity& entity, float fDeltaSeconds) {
    entity.draw.rotation.y += fDeltaSeconds * 0.7f;
}

namespace dxs {
void RegisterProjectComponents() {
    RegisterComponentClass("Spin", []() -> std::unique_ptr<Component> { return std::make_unique<Spin>(); });
}
} // namespace dxs
