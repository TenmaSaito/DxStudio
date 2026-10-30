#include "EngineCallbacks.h"

namespace dxs {

EngineEvents& EngineEvents::Get() {
    static EngineEvents events;
    return events;
}

} // namespace dxs
