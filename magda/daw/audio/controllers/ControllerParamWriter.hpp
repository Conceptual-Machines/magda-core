#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "../../core/aliases/TargetResolver.hpp"

namespace magda {

namespace te = tracktion;

// ============================================================================
// ControllerParamWriter (abstract)
// ============================================================================

/**
 * @brief Abstract base for writing a normalized value to a resolved parameter target.
 *
 * The default implementation writes through TrackManager, using the live
 * parameter description supplied by the active engine. Called on the message
 * thread.
 */
class ControllerParamWriter {
  public:
    virtual ~ControllerParamWriter() = default;

    /**
     * @brief Write a normalized value to the resolved target.
     *
     * @param resolved  Fully resolved device + param index.
     * @param value     Normalized float in [0, 1].
     *
     * Must be called on the message thread.
     */
    virtual void write(const ResolveResult& resolved, float value) = 0;
};

// ============================================================================
// DefaultControllerParamWriter
// ============================================================================

class AudioBridge;

/**
 * @brief Production param writer shared by the incumbent and native engines.
 */
class DefaultControllerParamWriter : public ControllerParamWriter {
  public:
    DefaultControllerParamWriter() = default;
    // Source compatibility for callers that still have an incumbent bridge.
    explicit DefaultControllerParamWriter(AudioBridge&) {}

    void write(const ResolveResult& resolved, float value) override;

  private:
    void writePluginParam(const ControlTarget& target, float clamped);
    static void writeMacro(const ControlTarget& target, float clamped);
    static void writeModParam(const ControlTarget& target, float clamped);
    static void writeTrackLevel(const ControlTarget& target, float clamped);
    static void writeSendLevel(const ControlTarget& target, float clamped);
};

}  // namespace magda
