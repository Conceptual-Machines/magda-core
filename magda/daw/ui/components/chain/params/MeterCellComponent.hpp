#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/DeviceInfo.hpp"

namespace magda::daw::ui {

/**
 * @brief One grid cell showing a value the device reports back.
 *
 * The read-only counterpart to ParamSlotComponent. It shares that component's
 * cell geometry (name label on top, readout below) and nothing else: a meter
 * has no gesture, no context menu, no link target and no drop target, because
 * there is no parameter behind it to bind to.
 *
 * The reading is pulled from a supplier on a timer rather than pushed in with
 * the rest of the device snapshot, because a snapshot is refreshed when
 * something changes a parameter, and a meter changes when the signal does.
 * Polling runs only while the cell is actually showing.
 *
 * Ballistics deliberately live in the DSP (`ba.peakholder`,
 * `an.amp_follower_ud`, `dm.vumeter`). A ~30 Hz poll against ~1 ms audio
 * blocks samples about one block in thirty, so anything this component
 * smoothed for itself would be smoothing a signal that had already dropped
 * its transients.
 */
class MeterCellComponent final : public juce::Component,
                                 public juce::SettableTooltipClient,
                                 private juce::Timer {
  public:
    MeterCellComponent();
    ~MeterCellComponent() override;

    void setMeterInfo(const magda::MeterInfo& info);

    /// Supplies the current reading in the meter's own units. Cleared with a
    /// null function when the cell stops showing a meter.
    void setSource(std::function<float()> source);

    void setFonts(const juce::Font& labelFont, const juce::Font& valueFont);

    void paint(juce::Graphics& g) override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;

  private:
    void timerCallback() override;
    void updateActiveState();
    /// 0..1 position of `value_` within the declared range.
    float normalized() const;
    juce::String formattedValue() const;

    static constexpr int kPollHz = 30;

    magda::MeterInfo info_;
    std::function<float()> source_;
    float value_ = 0.0f;

    juce::Label nameLabel_;
    juce::Font valueFont_{juce::FontOptions{11.0f}};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MeterCellComponent)
};

}  // namespace magda::daw::ui
