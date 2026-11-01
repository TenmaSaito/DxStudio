#include "InspectorShowcase.h"

#include <cmath>

void InspectorShowcase::Start(dxs::Entity& entity) {
    dxs::Debug::Log(entity.sName + ": " + std::to_string(m_vWaypoints.size()) + " waypoints, label '" + m_sLabel + "'");
}

// Walks along the waypoints (offset by Offset) at Speed while Mode is not Idle.
void InspectorShowcase::Update(dxs::Entity& entity, float fDeltaSeconds) {
    m_fElapsed += fDeltaSeconds;
    entity.draw.bVisible = m_bVisible;
    if (m_mode == MODE::IDLE || m_vWaypoints.empty()) return;
    m_nWaypoint %= m_vWaypoints.size();
    const auto& target = m_vWaypoints[m_nWaypoint];
    auto& position = entity.draw.position;
    const float fDx = target.x + m_offset.x - position.x, fDy = target.y + m_offset.y - position.y,
                fDz = target.z + m_offset.z - position.z;
    const float fDistance = std::sqrt(fDx * fDx + fDy * fDy + fDz * fDz);
    const float fStep = m_fSpeed * (m_mode == MODE::RUN ? 2.0f : 1.0f) * fDeltaSeconds;
    if (fDistance <= fStep || fDistance < 1e-5f) {
        position = {target.x + m_offset.x, target.y + m_offset.y, target.z + m_offset.z};
        m_nWaypoint = (m_nWaypoint + 1) % m_vWaypoints.size();
        return;
    }

    position.x += fDx / fDistance * fStep;
    position.y += fDy / fDistance * fStep;
    position.z += fDz / fDistance * fStep;
}

DXS_REGISTER_COMPONENT(InspectorShowcase)
