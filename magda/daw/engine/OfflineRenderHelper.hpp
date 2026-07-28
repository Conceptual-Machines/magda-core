#pragma once

// clang-format off
#include <tracktion_engine/tracktion_engine.h>
// clang-format on

namespace magda {

/** Engine-internal preparation hook retained for focused render-state tests. */
void prepareEditForOfflineRender(tracktion::Edit& edit);

}  // namespace magda
