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

// Matches the Mixer's MIN / MAX_CHANNEL_WIDTH, as the Preferences sliders do.
constexpr double kMinWidth = 80.0;
constexpr double kMaxWidth = 180.0;

class TrackSizePresetsPanel final : public juce::Component {
  public:
    using Store = std::function<void(Config&, int)>;

    struct Preset {
        juce::String label;
        int value;
        Store store;
    };

    TrackSizePresetsPanel(double minValue, double maxValue, std::array<Preset, 3> presets) {
        for (size_t i = 0; i < presets.size(); ++i)
            add(i, presets[i], minValue, maxValue);
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

    void add(size_t index, const Preset& preset, double minValue, double maxValue) {
        auto& row = rows_[index];
        row.label.setText(preset.label, juce::dontSendNotification);
        row.label.setFont(FontManager::getInstance().getUIFont(12.0f));
        row.label.setColour(juce::Label::textColourId,
                            ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
        addAndMakeVisible(row.label);

        row.slider.setRange(minValue, maxValue, 1.0);
        row.slider.setShowFillIndicator(false);
        row.slider.setValueFormatter([](double value) { return juce::String(value, 0); });
        row.slider.setValue(preset.value, juce::dontSendNotification);
        row.slider.onValueChanged = [store = preset.store](double value) {
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
    const auto& config = Config::getInstance();
    juce::CallOutBox::launchAsynchronously(
        std::make_unique<TrackSizePresetsPanel>(
            kMinHeight, kMaxHeight,
            std::array<TrackSizePresetsPanel::Preset, 3>{
                {{tr("preferences.slider.compact_track_height"), config.getCompactTrackHeight(),
                  [](Config& c, int h) { c.setCompactTrackHeight(h); }},
                 {tr("preferences.slider.medium_track_height"), config.getMediumTrackHeight(),
                  [](Config& c, int h) { c.setMediumTrackHeight(h); }},
                 {tr("preferences.slider.large_track_height"), config.getLargeTrackHeight(),
                  [](Config& c, int h) { c.setLargeTrackHeight(h); }}}}),
        screenArea, nullptr);
}

void showTrackWidthPresets(juce::Rectangle<int> screenArea) {
    const auto& config = Config::getInstance();
    juce::CallOutBox::launchAsynchronously(
        std::make_unique<TrackSizePresetsPanel>(
            kMinWidth, kMaxWidth,
            std::array<TrackSizePresetsPanel::Preset, 3>{
                {{tr("preferences.slider.narrow_track_width"), config.getNarrowTrackWidth(),
                  [](Config& c, int w) { c.setNarrowTrackWidth(w); }},
                 {tr("preferences.slider.medium_track_width"), config.getMediumTrackWidth(),
                  [](Config& c, int w) { c.setMediumTrackWidth(w); }},
                 {tr("preferences.slider.wide_track_width"), config.getWideTrackWidth(),
                  [](Config& c, int w) { c.setWideTrackWidth(w); }}}}),
        screenArea, nullptr);
}

}  // namespace magda
