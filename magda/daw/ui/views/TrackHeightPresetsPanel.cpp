// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TrackHeightPresetsPanel.hpp"

#include <array>
#include <functional>

#include "../components/common/TextSlider.hpp"
#include "../themes/ActiveTheme.hpp"
#include "../themes/FontManager.hpp"
#include "core/Config.hpp"
#include "core/StringTable.hpp"

namespace magda {

namespace {

// Matches TrackHeadersPanel's MIN / MAX_TRACK_HEIGHT, as the Preferences sliders do.
constexpr double kMinHeight = 32.0;
constexpr double kMaxHeight = 220.0;

class TrackHeightPresetsPanel final : public juce::Component {
  public:
    TrackHeightPresetsPanel() {
        auto& config = Config::getInstance();
        add(0, tr("preferences.slider.compact_track_height"), config.getCompactTrackHeight(),
            [](Config& c, int h) { c.setCompactTrackHeight(h); });
        add(1, tr("preferences.slider.medium_track_height"), config.getMediumTrackHeight(),
            [](Config& c, int h) { c.setMediumTrackHeight(h); });
        add(2, tr("preferences.slider.large_track_height"), config.getLargeTrackHeight(),
            [](Config& c, int h) { c.setLargeTrackHeight(h); });
        setSize(kLabelW + kGap + kSliderW + (2 * kPad), (kRowH * 3) + (2 * kPad));
    }

    void resized() override {
        auto area = getLocalBounds().reduced(kPad);
        for (auto& row : rows_) {
            auto line = area.removeFromTop(kRowH);
            row.slider.setBounds(line.removeFromRight(kSliderW).reduced(0, 3));
            line.removeFromRight(kGap);
            row.label.setBounds(line);
        }
    }

  private:
    static constexpr int kLabelW = 150;
    static constexpr int kSliderW = 64;
    static constexpr int kGap = 8;
    static constexpr int kRowH = 26;
    static constexpr int kPad = 8;

    struct Row {
        juce::Label label;
        daw::ui::TextSlider slider;
    };

    void add(size_t index, const juce::String& text, int height,
             std::function<void(Config&, int)> store) {
        auto& row = rows_[index];
        row.label.setText(text, juce::dontSendNotification);
        row.label.setFont(FontManager::getInstance().getUIFont(12.0f));
        row.label.setColour(juce::Label::textColourId,
                            ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
        addAndMakeVisible(row.label);

        row.slider.setRange(kMinHeight, kMaxHeight, 1.0);
        row.slider.setShowFillIndicator(false);
        row.slider.setValueFormatter([](double value) { return juce::String(value, 0); });
        row.slider.setValue(height, juce::dontSendNotification);
        row.slider.onValueChanged = [store = std::move(store)](double value) {
            auto& config = Config::getInstance();
            store(config, juce::roundToInt(value));
            config.save();
        };
        addAndMakeVisible(row.slider);
    }

    std::array<Row, 3> rows_;
};

}  // namespace

void showTrackHeightPresets(juce::Rectangle<int> screenArea) {
    juce::CallOutBox::launchAsynchronously(std::make_unique<TrackHeightPresetsPanel>(), screenArea,
                                           nullptr);
}

}  // namespace magda
