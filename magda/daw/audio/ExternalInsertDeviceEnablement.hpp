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
 * user enabled in Audio Settings (or that was enabled at startup) is left
 * alone. Message thread only.
 */
class ExternalInsertDeviceEnablement {
  public:
    explicit ExternalInsertDeviceEnablement(tracktion::engine::Edit& edit);

    /** Derive pass. Returns true when any device's enablement changed (the
        caller should reallocate the playback graph). */
    bool refresh();

  private:
    tracktion::engine::Edit& edit_;
    std::set<juce::String> autoEnabledInputs_;
    std::set<juce::String> autoEnabledOutputs_;
};

}  // namespace magda
