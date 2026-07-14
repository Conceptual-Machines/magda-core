#pragma once

#include <juce_core/juce_core.h>

#include <set>

namespace tracktion {
inline namespace engine {
class Edit;
}
}  // namespace tracktion

namespace magda {

/**
 * @brief Auto-enables hardware I/O referenced by External FX / External
 *        Instrument inserts (#1623, plan Phase 3).
 *
 * TE's insert picker and routing only see ENABLED devices — a saved send or
 * return on a disabled port silently resolves to noDevice. This component
 * derives enablement from the inserts: refresh() enables every port an enabled
 * insert references, and disables ports again once no insert uses them —
 * reference-counted by re-derivation, so removing one insert never turns off a
 * port another one still needs.
 *
 * Only ports this component itself enabled are ever disabled; anything the
 * user enabled in Audio Settings is left alone. The auto-enabled set is
 * persisted (Config) because TE persists device enablement globally: without
 * it, a port auto-enabled last session would come back after a restart
 * looking permanently user-enabled. A port that is enabled at startup AND in
 * the persisted set is therefore still treated as auto-enabled (eligible for
 * auto-disable). Message thread only.
 */
class ExternalInsertDeviceEnablement {
  public:
    explicit ExternalInsertDeviceEnablement(tracktion::engine::Edit& edit);

    /** Derive pass. Returns true when any device's enablement changed (the
        caller should reallocate the playback graph). */
    bool refresh();

    /** Per-port reconciliation rule, pure so it is testable without an
        engine. usedByInsert: an enabled insert references the port.
        portEnabled: the device's current state. trackedAsAuto: the port is
        in the auto-enabled set (from this session or restored from the
        persisted set). */
    struct PortAction {
        bool changeEnabled = false;  // apply `enabled` to the device
        bool enabled = false;
        bool trackAsAuto = false;  // keep/put the port in the auto-enabled set
    };
    static PortAction reconcilePort(bool usedByInsert, bool portEnabled, bool trackedAsAuto);

  private:
    void persistAutoEnabledSets() const;

    tracktion::engine::Edit& edit_;
    std::set<juce::String> autoEnabledInputs_;
    std::set<juce::String> autoEnabledOutputs_;
};

}  // namespace magda
