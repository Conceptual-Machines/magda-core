#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "core/DeviceInfo.hpp"

namespace magda::daw::ui {

/**
 * @brief One value a device reports back, drawn as a bar, a number or a lamp.
 *
 * The read-only counterpart to a ParamSlotComponent, and deliberately not a
 * mode on it: a meter has no gesture, no context menu, no link target and no
 * drop target, because there is no parameter behind it to bind to.
 *
 * The reading is pulled from a supplier on a timer rather than pushed in with
 * the device snapshot, because a snapshot is refreshed when something changes
 * a parameter, and a meter changes when the signal does. Polling runs only
 * while the widget is actually showing.
 *
 * Ballistics deliberately live in the DSP (`ba.peakholder`,
 * `an.amp_follower_ud`, `dm.vumeter`). A ~30 Hz poll against ~1 ms audio
 * blocks samples about one block in thirty, so anything this widget smoothed
 * for itself would be smoothing a signal that had already dropped its
 * transients.
 */
class MeterWidget final : public juce::Component,
                          public juce::SettableTooltipClient,
                          private juce::Timer {
  public:
    MeterWidget();
    ~MeterWidget() override;

    void setMeterInfo(const magda::MeterInfo& info);

    /// Supplies the current reading in the meter's own units. Cleared with a
    /// null function when the widget stops showing a meter.
    void setSource(std::function<float()> source);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;

    /// Re-evaluates whether polling should run. Needed because JUCE only sends
    /// visibilityChanged() to the component whose own flag moved, so hiding an
    /// ancestor never reaches this widget on its own.
    void refreshActiveState();

  private:
    void timerCallback() override;
    void updateActiveState();
    /// 0..1 position of `value_` within the declared range.
    float normalized() const;
    juce::String formattedValue() const;

    static constexpr int kPollHz = 30;

    /// Vertical bars are drawn as a narrow centred column with the figure
    /// below, since a column wide enough to hold the text stops reading as a
    /// level. A horizontal bar keeps the text over the track.
    static constexpr int kVerticalBarWidth = 16;
    static constexpr int kVerticalValueHeight = 14;

    magda::MeterInfo info_;
    std::function<float()> source_;
    float value_ = 0.0f;

    juce::Label nameLabel_;
    juce::Font valueFont_{juce::FontOptions{11.0f}};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MeterWidget)
};

/**
 * @brief The strip of readouts a runtime Faust device declares via bargraphs.
 *
 * Given its own region of the device body, the way a compiled device's curve
 * view is, rather than cells in the parameter grid. A bargraph is not a
 * parameter and does not want a parameter's geometry: a level wants height,
 * which a grid cell sized for a knob and its label cannot give it.
 *
 * Meters are laid out as columns across the strip, weighted by the
 * `[width:N]` each declares. The panel is device-level, so every meter the
 * patch declares stays visible whichever parameter page is showing.
 */
class FaustMeterPanel final : public juce::Component {
  public:
    /// Vertical space the strip asks for under the parameter grid. Enough for
    /// a vertical bar to read as a level rather than as a thick line; the
    /// caller bounds it against the body height.
    static constexpr int kPreferredHeight = 84;

    FaustMeterPanel();
    ~FaustMeterPanel() override;

    /// Rebuilds the strip for `meters`. Returns true when the panel has
    /// anything to show, which is what the caller uses to decide whether to
    /// carve it a region at all.
    bool setMeters(const std::vector<magda::MeterInfo>& meters);

    /// Supplies live readings, keyed by `MeterInfo::meterIndex`. Safe to call
    /// before or after setMeters: whichever arrives second binds the widgets.
    void setMeterSource(std::function<float(int meterIndex)> source);

    bool isEmpty() const {
        return entries_.empty();
    }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;

  private:
    /// Pushes the showing state down to the widgets, which JUCE will not do.
    void refreshMeterActiveStates();
    void bindSources();

    struct Entry {
        magda::MeterInfo info;
        std::unique_ptr<MeterWidget> widget;
    };

    std::vector<Entry> entries_;
    std::function<float(int meterIndex)> meterSource_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FaustMeterPanel)
};

}  // namespace magda::daw::ui
