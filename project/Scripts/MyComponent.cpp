#include "MyComponent.h"

void Spin::Update(dxs::Entity& entity, float deltaSeconds) {
    entity.draw.rotation.y += deltaSeconds * 0.7f;
}

namespace dxs {
void RegisterProjectComponents() {
    RegisterComponentClass("Spin", []() -> std::unique_ptr<Component> { return std::make_unique<Spin>(); });
}
} // namespace dxs
