#pragma once

#include <functional>

namespace tracktion::inline engine {
class Edit;
}

namespace magda {

class TransportApiLive;

/**
 * Bind the incumbent engine to the engine-neutral transport facade.
 * The facade owns the edit observer through its callbacks and must be destroyed
 * (or have its engine state cleared) before the current Edit is destroyed.
 */
void wireTracktionTransportApi(TransportApiLive& api, std::function<tracktion::Edit*()> getEdit);

}  // namespace magda
