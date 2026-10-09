#pragma once

#include "audio/plugins/compiled/MagdaEqCompiledPlugin.hpp"
#include "layout/DeviceParamLayout.hpp"
#include "layout/EqBandEditorGeometry.hpp"

namespace magda::daw::ui {

/// The EQ's knobs for one band at a time: each page is a band, showing its frequency, gain
/// and Q, and every page ends with Output. The faceplate's chips and nodes turn the pages.
class CompiledEqBandLayout final : public DeviceParamLayout {
  public:
    using Plugin = magda::daw::audio::compiled::MagdaEqCompiledPlugin;

    int cellCount() const override {
        return 4;
    }
    int cellsPerRow() const override {
        return 4;
    }
    bool wantsPagination() const override {
        return true;
    }
    int totalPages(const magda::DeviceInfo&) const override {
        return Plugin::kBandCount;
    }
    bool placesOwnCells() const override {
        return true;
    }
    juce::Rectangle<int> cellBounds(int cell, juce::Rectangle<int> area, bool) const override {
        return EqBandEditorGeometry::of(area).knob(cell);
    }

    ParamCell cellFor(const magda::DeviceInfo& device, int cellIndex,
                      int currentPage) const override {
        static constexpr int kOffsets[] = {Plugin::kBandFreqOffset, Plugin::kBandGainOffset,
                                           Plugin::kBandQOffset};
        // The editor already names the band, so its knobs say only what they set.
        static constexpr const char* kLabels[] = {"Freq", "Gain", "Q", "Output"};
        const int band = juce::jlimit(0, Plugin::kBandCount - 1, currentPage);
        const int slot =
            cellIndex < 3 ? Plugin::bandSlot(band, kOffsets[cellIndex]) : Plugin::kOutputSlot;
        ParamCell cell;
        for (int k = 0; k < static_cast<int>(device.parameters.size()); ++k) {
            if (device.parameters[static_cast<size_t>(k)].paramIndex != slot)
                continue;
            cell.mode = ParamCell::Mode::Filled;
            cell.paramArrayIndex = k;
            cell.targetParamIndex = slot;
            cell.label = kLabels[cellIndex];
            return cell;
        }
        cell.mode = ParamCell::Mode::Hidden;
        return cell;
    }
};

}  // namespace magda::daw::ui
