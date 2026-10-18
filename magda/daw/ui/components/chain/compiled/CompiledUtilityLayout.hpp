#pragma once

#include "audio/plugins/compiled/MagdaUtilityCompiledPlugin.hpp"
#include "layout/DeviceParamLayout.hpp"
#include "layout/UtilityGeometry.hpp"

namespace magda::daw::ui {

/// The Utility's continuous controls, placed around its stereo field: Pan, Width and the Low
/// mono crossover in a row, Gain across the foot. The faceplate draws the field and the toggles.
class CompiledUtilityLayout final : public DeviceParamLayout {
  public:
    using Plugin = magda::daw::audio::compiled::MagdaUtilityCompiledPlugin;

    int cellCount() const override {
        return 4;
    }
    int cellsPerRow() const override {
        return 4;
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
        return UtilityGeometry::of(area, style_).cell(cell);
    }
    void setControlStyle(ParamControlStyle style) override {
        style_ = style;
    }

    ParamCell cellFor(const magda::DeviceInfo& device, int cellIndex, int) const override {
        static constexpr int kSlots[] = {Plugin::kPanSlot, Plugin::kWidthSlot,
                                         Plugin::kLowMonoFreqSlot, Plugin::kGainSlot};
        static constexpr const char* kLabels[] = {"Pan", "Width", "Low mono", "Gain"};
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
            cell.label = kLabels[cellIndex];
            // The crossover only acts with Low mono on, and Mono folds everything anyway.
            if (slot == Plugin::kLowMonoFreqSlot)
                cell.enabled =
                    isOn(device, Plugin::kLowMonoSlot) && !isOn(device, Plugin::kMonoSlot);
            return cell;
        }
        return cell;
    }

  private:
    static bool isOn(const magda::DeviceInfo& device, int slot) {
        const auto* param = device.findParameterByIndex(slot);
        return param != nullptr && param->currentValue >= 0.5f;
    }

    ParamControlStyle style_ = ParamControlStyle::Text;
};

}  // namespace magda::daw::ui
