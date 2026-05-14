#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "../../core/DeviceInfo.hpp"

namespace magda {

namespace te = tracktion;

ParameterInfo makeInfoFromTeParam(int index, te::AutomatableParameter* param);

}  // namespace magda
