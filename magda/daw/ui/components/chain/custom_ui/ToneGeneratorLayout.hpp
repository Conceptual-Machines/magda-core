// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>

#include "audio/plugins/ToneGeneratorPlugin.hpp"
#include "layout/DeviceParamLayout.hpp"

namespace magda::daw::ui {

/**
 * @brief The Test Tone's body: the waveform row, the noise colour and Transport Only, then
 *        Frequency beside Level (#3164, #3201).
 *
 * Rows are sized for the control style. The faceplate and the param grid both place from this,
 * over the same bounds.
 */
struct ToneGeneratorGeometry {
    juce::Rectangle<int> waveform;
    juce::Rectangle<int> noiseColor;
    juce::Rectangle<int> transportOnly;
    std::array<juce::Rectangle<int>, 2> cells;

    static constexpr int kSpace = 8;
    static constexpr int kWaveformHeight = 30;
    static constexpr int kOptionsHeight = 24;

    static int cellHeight(ParamControlStyle style) {
        switch (style) {
            case ParamControlStyle::Knobs:
                return 72;
            case ParamControlStyle::Sliders:
                return 44;
            case ParamControlStyle::Text:
                break;
        }
        return 48;
    }

    static ToneGeneratorGeometry of(juce::Rectangle<int> bounds, ParamControlStyle style) {
        ToneGeneratorGeometry g;
        auto area = bounds.reduced(kSpace, 0).withTrimmedTop(kSpace / 2).withTrimmedBottom(4);
        g.waveform = area.removeFromTop(kWaveformHeight);
        area.removeFromTop(kSpace);
        auto options = area.removeFromTop(kOptionsHeight);
        g.noiseColor = options.removeFromLeft((options.getWidth() - kSpace) * 3 / 5);
        options.removeFromLeft(kSpace);
        g.transportOnly = options;
        area.removeFromTop(kSpace);

        auto row = area.removeFromTop(juce::jmin(cellHeight(style), area.getHeight()));
        g.cells[0] = row.removeFromLeft((row.getWidth() - kSpace) / 2);
        row.removeFromLeft(kSpace);
        g.cells[1] = row;
        return g;
    }
};

/// Frequency and Level, placed by ToneGeneratorGeometry. Frequency dims for noise, which has none.
class ToneGeneratorLayout final : public DeviceParamLayout {
  public:
    using Tone = devices::ToneGenerator;

    int cellCount() const override {
        return 2;
    }
    int cellsPerRow() const override {
        return 2;
    }
    bool wantsPagination() const override {
        return false;
    }
    int totalPages(const magda::DeviceInfo&) const override {
        return 1;
    }
    bool placesOwnCells() const override {
        return true;
    }
    juce::Rectangle<int> cellBounds(int cell, juce::Rectangle<int> area, bool) const override {
        return ToneGeneratorGeometry::of(area, style_).cells[static_cast<size_t>(cell)];
    }
    void setControlStyle(ParamControlStyle style) override {
        style_ = style;
    }

    ParamCell cellFor(const magda::DeviceInfo& device, int cellIndex, int) const override {
        static constexpr int kSlots[] = {Tone::kFrequencyParamIndex, Tone::kLevelParamIndex};
        ParamCell cell;
        if (cellIndex < 0 || cellIndex >= cellCount())
            return cell;

        const int slot = kSlots[cellIndex];
        for (int k = 0; k < static_cast<int>(device.parameters.size()); ++k) {
            if (device.parameters[static_cast<size_t>(k)].paramIndex != slot)
                continue;
            cell.mode = ParamCell::Mode::Filled;
            cell.paramArrayIndex = k;
            cell.targetParamIndex = slot;
            if (slot == Tone::kFrequencyParamIndex) {
                const auto* waveform = device.findParameterByIndex(Tone::kWaveformParamIndex);
                cell.enabled = waveform == nullptr || juce::roundToInt(waveform->currentValue) !=
                                                          static_cast<int>(Tone::Waveform::Noise);
            }
            return cell;
        }
        return cell;
    }

  private:
    ParamControlStyle style_ = ParamControlStyle::Text;
};

}  // namespace magda::daw::ui
