#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "../core/aliases/TargetResolver.hpp"

namespace magda {

namespace te = tracktion;

// ============================================================================
// ControllerParamWriter (abstract)
// ============================================================================

/**
 * @brief Abstract base for writing a normalized value to a resolved parameter target.
 *
 * The default implementation (DefaultControllerParamWriter) looks up the
 * plugin via AudioBridge::getPlugin(), then calls setParameter() on the
 * AutomatableParameter. Called on the message thread.
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
    virtual void write(const ResolvedTarget& resolved, float value) = 0;
};

// ============================================================================
// DefaultControllerParamWriter
// ============================================================================

class AudioBridge;

/**
 * @brief Production param writer backed by AudioBridge.
 *
 * Resolves devicePath -> te::Plugin via AudioBridge::getPlugin(), then
 * writes value via AutomatableParameter::setParameter().
 */
class DefaultControllerParamWriter : public ControllerParamWriter {
  public:
    explicit DefaultControllerParamWriter(AudioBridge& bridge) : bridge_(bridge) {}

    void write(const ResolvedTarget& resolved, float value) override;

  private:
    void writePluginParam(const ResolvedTarget& resolved, float clamped);
    void writeMacro(const ResolvedTarget& resolved, float clamped);
    void writeModParam(const ResolvedTarget& resolved, float clamped);

    AudioBridge& bridge_;
};

}  // namespace magda
