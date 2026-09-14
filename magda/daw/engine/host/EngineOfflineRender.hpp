#pragma once

#include <memory>
#include <optional>

#include "../../audio/plugins/engine/EngineDeviceFactory.hpp"
#include "../AudioEngine.hpp"
#include "exec/EngineDevice.hpp"
#include "exec/RenderContext.hpp"
#include "transport/TempoMap.hpp"

/**
 * @file EngineOfflineRender.hpp
 * @brief Export, bounce and analysis renders on magda::engine (#2555).
 *
 * A render compiles its own plan from the model and writes through
 * renderOffline into an AudioFileSink. The device instances are the live
 * session's wherever it has one, since a hosted plugin's state is only up to
 * date in the instance that has been playing; the live callback is off for as
 * long as a session is open, so the two never process one instance at once.
 */

namespace magda::daw::engine_host {

/** @brief What an offline render needs of the host whose devices it borrows. */
class OfflineRenderHost {
  public:
    virtual ~OfflineRenderHost() = default;

    /// Stop the transport, take the callback off the device and hold every
    /// publish until endOfflineRender(). Message thread.
    virtual void beginOfflineRender() = 0;

    /// Republish, put the callback back and play again if @p resumePlayback.
    virtual void endOfflineRender(bool resumePlayback) = 0;

    /// The instance the live session renders @p key with, or null.
    virtual std::shared_ptr<engine::EngineDevice> liveDevice(engine::DeviceKey key) const = 0;

    /// What the live devices are prepared for, or nothing without a session.
    virtual std::optional<engine::RenderContext> liveContext() const = 0;

    /// The map the render resolves its beat range and places clips through.
    virtual engine::TempoMap renderTempo() const = 0;

    /// Where a hosted plugin the live session does not hold is loaded from.
    virtual audio::engine_adapter::ExternalPluginServices pluginServices() const = 0;
};

/** @brief A render session over @p host's devices, open until destroyed. Message thread. */
std::unique_ptr<OfflineRenderSession> createEngineOfflineRenderSession(
    OfflineRenderHost& host, bool resumePlaybackWhenFinished);

}  // namespace magda::daw::engine_host
