#pragma once

#include "custom_ui/FaustCustomUIRegistry.hpp"

namespace magda::daw::audio {
class IFaustEditorModel;
struct FaustParamSlot;
}  // namespace magda::daw::audio

namespace magda::daw::ui {

/**
 * @brief Inline transfer-curve display for the bundled `magda_drive`
 *        Faust DSP.
 *
 * Reads the Drive (dB) and Gain (dB) slots out of the plugin's
 * FaustParamPool by label match and draws the resulting waveshaping
 * curve `y = tanh(drive_lin · x) · gain_lin` over `x ∈ [-1, 1]`. The
 * Cutoff slot is a lowpass and doesn't participate in the curve, so
 * we only re-render when Drive or Gain change.
 *
 * Registered with FaustCustomUIRegistry against the DSP name
 * "MagdaDrive" (the `declare name "MagdaDrive";` line in the source).
 *
 * Slot zones are read directly off the message thread for cheap
 * cosmetic display — torn reads are acceptable for a transfer-curve
 * preview. The view does NOT own the plugin and only stays alive for
 * as long as the parent FaustUI does.
 */
class MagdaDriveCurveView : public FaustCustomView, public juce::Timer {
  public:
    explicit MagdaDriveCurveView(magda::daw::audio::IFaustEditorModel& plugin);
    ~MagdaDriveCurveView() override;

    int getPreferredHeight() const override {
        return 96;
    }

    void paint(juce::Graphics& g) override;
    void timerCallback() override;

  private:
    /// Locate the active slot whose cleaned label exactly matches
    /// `label` (case-insensitive). Returns nullptr if not found.
    const magda::daw::audio::FaustParamSlot* findSlot(const juce::String& label) const;

    /// Read the live zone value off `slot` (the audio thread may be
    /// writing concurrently — torn reads are acceptable here). Falls
    /// back to the slot's default value if the zone pointer is null.
    static float readSlotValue(const magda::daw::audio::FaustParamSlot* slot);

    magda::daw::audio::IFaustEditorModel& plugin_;

    // Last sampled values — repaints fire only when one of these changes.
    float lastDrive_ = 0.0f;
    float lastGain_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaDriveCurveView)
};

/**
 * @brief Register every built-in Faust custom view with the global
 *        FaustCustomUIRegistry. Idempotent — repeat calls just
 *        re-install the same factories.
 *
 * Called by FaustUI on construction. Exposed as a free function so
 * the linker has a reason to keep the per-view translation units
 * alive when MAGDA links `libmagda_daw_app` as a static library — a
 * file-scope static registrar would be silently dropped because no
 * external symbol in this TU is referenced.
 */
void registerBuiltInFaustCustomViews();

}  // namespace magda::daw::ui
