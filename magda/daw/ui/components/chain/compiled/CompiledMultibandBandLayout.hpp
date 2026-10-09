#pragma once

#include "audio/plugins/compiled/MagdaMultibandCompiledPlugin.hpp"
#include "layout/DeviceParamLayout.hpp"
#include "layout/MultibandEditorGeometry.hpp"

namespace magda::daw::ui {

/// The multiband dynamics' knobs for one band at a time: each page is a band's ten, and every
/// page ends with the whole-device five. The faceplate's tabs and band regions turn the pages.
class CompiledMultibandBandLayout final : public DeviceParamLayout {
  public:
    using MB = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
    static constexpr int kBandKnobs = 10;

    int cellCount() const override {
        return kBandKnobs + MultibandEditorGeometry::kGlobalCount;
    }
    int cellsPerRow() const override {
        return MultibandEditorGeometry::kKnobColumns;
    }
    bool wantsPagination() const override {
        return true;
    }
    int totalPages(const magda::DeviceInfo&) const override {
        return 3;
    }
    bool placesOwnCells() const override {
        return true;
    }
    juce::Rectangle<int> cellBounds(int cell, juce::Rectangle<int> area, bool) const override {
        return MultibandEditorGeometry::of(area).knob(cell);
    }

    ParamCell cellFor(const magda::DeviceInfo& device, int cellIndex,
                      int currentPage) const override {
        // Per band: above (upper threshold, ratio), below, range, then limit, timing and level.
        static constexpr int kBandSlots[3][kBandKnobs] = {
            {MB::kLowUpperThresholdSlot, MB::kLowAboveRatioSlot, MB::kLowLowerThresholdSlot,
             MB::kLowBelowRatioSlot, MB::kLowRangeSlot, MB::kLowLimitSlot, MB::kLowAttackSlot,
             MB::kLowReleaseSlot, MB::kLowInputSlot, MB::kLowGainSlot},
            {MB::kMidUpperThresholdSlot, MB::kMidAboveRatioSlot, MB::kMidLowerThresholdSlot,
             MB::kMidBelowRatioSlot, MB::kMidRangeSlot, MB::kMidLimitSlot, MB::kMidAttackSlot,
             MB::kMidReleaseSlot, MB::kMidInputSlot, MB::kMidGainSlot},
            {MB::kHighUpperThresholdSlot, MB::kHighAboveRatioSlot, MB::kHighLowerThresholdSlot,
             MB::kHighBelowRatioSlot, MB::kHighRangeSlot, MB::kHighLimitSlot, MB::kHighAttackSlot,
             MB::kHighReleaseSlot, MB::kHighInputSlot, MB::kHighGainSlot}};
        static constexpr int kGlobalSlots[] = {MB::kAmountSlot, MB::kAttackSlot, MB::kReleaseSlot,
                                               MB::kInputSlot, MB::kOutputSlot};
        // The editor names the band, so its knobs say only what they set.
        static constexpr const char* kBandLabels[kBandKnobs] = {
            "Above", "Ratio",  "Below",   "Ratio", "Range",
            "Limit", "Attack", "Release", "Input", "Gain"};

        const int band = juce::jlimit(0, 2, currentPage);
        const int slot = cellIndex < kBandKnobs ? kBandSlots[band][cellIndex]
                                                : kGlobalSlots[cellIndex - kBandKnobs];
        ParamCell cell;
        for (int k = 0; k < static_cast<int>(device.parameters.size()); ++k) {
            if (device.parameters[static_cast<size_t>(k)].paramIndex != slot)
                continue;
            cell.mode = ParamCell::Mode::Filled;
            cell.paramArrayIndex = k;
            cell.targetParamIndex = slot;
            if (cellIndex < kBandKnobs)
                cell.label = kBandLabels[cellIndex];
            return cell;
        }
        cell.mode = ParamCell::Mode::Hidden;
        return cell;
    }
};

}  // namespace magda::daw::ui
