#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "FaustCustomViewKind.hpp"

namespace magda::daw::audio {

class FaustParamPool;

/**
 * @brief Editor-facing contract shared by the Faust devices.
 *
 * Both the interpreter Faust effect (FaustPlugin) and the interpreter Faust
 * instrument (FaustInstrumentPlugin) expose a runtime-recompilable .dsp plus a
 * lifetime-stable FaustParamPool. The chain UI (FaustUI header strip, the
 * FaustCustomUIRegistry custom views, MagdaDriveCurveView) only needs this
 * small surface — keying on the interface instead of a concrete plugin type
 * lets one code path drive the rich Faust UI (code editor, Load .dsp, custom
 * views, diagnostics) for either device.
 */
class IFaustEditorModel {
  public:
    virtual ~IFaustEditorModel() = default;

    /// Lifetime-stable parameter pool the device harvested from its live DSP.
    virtual const FaustParamPool& getPool() const = 0;

    /// Identifier for the bespoke custom view registered against the loaded
    /// DSP, or None when the generic param grid is used.
    virtual FaustCustomViewKind getCustomViewKind() const = 0;

    /// Diagnostics from the most recent pool rebind (overflow / duplicate idx
    /// / out-of-range), surfaced in the FaustUI error label.
    virtual const std::vector<juce::String>& getLastRebindDiagnostics() const = 0;

    /// Compile + swap in `source`, persisting it to plugin state. Returns true
    /// on success; on failure `err` carries the libfaust message and the
    /// previously-loaded DSP is left in place. Message thread only.
    virtual bool loadDspSource(const juce::String& name, const juce::String& source,
                               juce::String& err, FaustCustomViewKind viewKind) = 0;

    /// Display name of the currently loaded DSP.
    virtual juce::String getDspName() const = 0;

    /// The currently loaded .dsp source (what the code editor reads/edits).
    virtual juce::String getDspSource() const = 0;
};

}  // namespace magda::daw::audio
