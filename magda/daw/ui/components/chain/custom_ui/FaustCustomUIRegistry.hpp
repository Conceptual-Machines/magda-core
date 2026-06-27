#pragma once

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <unordered_map>

#include "audio/plugins/FaustCustomViewKind.hpp"

namespace magda::daw::audio {
class IFaustEditorModel;
}

namespace magda::daw::ui {

using magda::daw::audio::FaustCustomViewKind;

/**
 * @brief Base class for per-DSP custom views inside FaustUI.
 *
 * A custom view sits between FaustUI's header strip and the standard
 * device parameter grid, bound to one Faust device (IFaustEditorModel,
 * i.e. either the Faust effect or instrument). It reads
 * live parameter values via the plugin's FaustParamPool zones (or the
 * matching AutomatableParameter for thread-safe message-side reads)
 * and renders whatever JUCE the host wants — a transfer-curve plot, an
 * XY pad, a meter cluster, a waveform display, etc.
 *
 * The view does NOT own the plugin and must not outlive it. FaustUI
 * destroys the view when the plugin is unset or replaced.
 */
class FaustCustomView : public juce::Component {
  public:
    FaustCustomView() = default;
    ~FaustCustomView() override = default;

    /// Vertical space the view wants under FaustUI's header strip.
    /// FaustUI carves exactly this many pixels for the view; the
    /// device's standard parameter grid fills whatever remains. The
    /// default is reasonable for a small inline plot — override for
    /// taller layouts.
    virtual int getPreferredHeight() const {
        return 88;
    }
};

/**
 * @brief Registry mapping a `FaustCustomViewKind` to a factory that
 *        produces the matching custom view.
 *
 * In-house DSPs register a factory at process startup; FaustUI looks
 * up by the plugin's current `FaustCustomViewKind` (set when the DSP
 * was loaded). `Kind::None` is reserved for user-loaded .dsp files
 * with no associated view — FaustUI falls back to the standard
 * grid-only layout in that case.
 *
 * Thread-safety: register on the main thread before any FaustUI
 * lookup. `create()` must be called on the message thread.
 */
class FaustCustomUIRegistry {
  public:
    using Factory =
        std::function<std::unique_ptr<FaustCustomView>(magda::daw::audio::IFaustEditorModel&)>;

    static FaustCustomUIRegistry& getInstance();

    /// Register a factory for the given kind. Replacing an existing
    /// registration is allowed (last write wins) — useful in tests; in
    /// production the registrations are one-shot at startup.
    void registerView(FaustCustomViewKind kind, Factory factory);

    /// Returns nullptr if `kind` is `None` or no factory is registered
    /// for it.
    std::unique_ptr<FaustCustomView> create(FaustCustomViewKind kind,
                                            magda::daw::audio::IFaustEditorModel& plugin) const;

  private:
    FaustCustomUIRegistry() = default;
    std::unordered_map<FaustCustomViewKind, Factory> factories_;
};

}  // namespace magda::daw::ui
