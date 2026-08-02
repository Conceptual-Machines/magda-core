#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "FaustPatchInfo.hpp"

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

    /// Which runtime Faust device this is. The editor header keys the patch
    /// library off this — bundled starters, the user's saved patches and the
    /// save destination are all split by kind, so an instrument never lists
    /// FX patches and vice versa.
    virtual FaustPatchKind getPatchKind() const = 0;

    /// Lifetime-stable parameter pool the device harvested from its live DSP.
    virtual const FaustParamPool& getPool() const = 0;

    /// Name of the bespoke custom view the loaded DSP asked for via
    /// `declare magda_view`, or empty when the generic param grid is used.
    virtual juce::String getCustomViewName() const = 0;

    /// Diagnostics from the most recent pool rebind (overflow / duplicate idx
    /// / out-of-range), surfaced in the FaustUI error label.
    virtual const std::vector<juce::String>& getLastRebindDiagnostics() const = 0;

    /// Compile + swap in `source`, persisting it to plugin state. Returns true
    /// on success; on failure `err` carries the libfaust message and the
    /// previously-loaded DSP is left in place. Message thread only.
    /// The custom view is read from `source` itself, so callers never pass one.
    virtual bool loadDspSource(const juce::String& name, const juce::String& source,
                               juce::String& err) = 0;

    /// Put source into the editor/persisted state without compiling or
    /// swapping the active DSP. Used for deliberately unverified generation.
    virtual void stageSourceForEditing(const juce::String& name, const juce::String& source) = 0;

    /// Display name of the currently loaded DSP.
    virtual juce::String getDspName() const = 0;

    /// The currently loaded .dsp source (what the code editor reads/edits).
    virtual juce::String getDspSource() const = 0;

    /// Authorship metadata the loaded patch declares about itself. Empty
    /// fields where the patch omits the declare; `isEmpty()` when it
    /// declares none of them.
    virtual FaustPatchInfo getPatchInfo() const = 0;
};

}  // namespace magda::daw::audio
