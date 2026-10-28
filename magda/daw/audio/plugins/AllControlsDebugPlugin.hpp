// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <string>

#include "plugins/SdkDeviceAdapter.hpp"

namespace magda::daw::audio {

/**
 * @brief Debug builds only: a pass-through effect whose slot shows every header, sidebar and
 *        side-strip control, so the chrome can be checked without loading plug-ins (#3141).
 *
 * Its "debug-all-controls" tag forces the controls a device type would otherwise gate; the
 * sidechain port, the 40 parameters (two grid pages) and Voice Mode bring the rest naturally.
 */
class AllControlsDebugDevice : public sdk::Device {
  public:
    static constexpr const char* kDeviceType = "debugAllControls";
    static constexpr const char* kName = "All Controls (Debug)";
    static constexpr int kParamCount = 40;
    static constexpr int kVoiceModeParamIndex = kParamCount - 1;

    sdk::DeviceProperties properties() const override {
        return {.pluginId = kDeviceType,
                .name = kName,
                .shortName = "Debug",
                .sidechain = sdk::monoAudioSidechain};
    }

    void process(sdk::ProcessContext&) override {}

    int parameterCount() const override {
        return kParamCount;
    }

    sdk::ParameterDescriptor parameterDescriptor(int index) const override {
        sdk::ParameterDescriptor info;
        if (index < 0 || index >= kParamCount)
            return info;
        info.index = index;
        if (index == kVoiceModeParamIndex) {
            info.stableId = "voiceMode";
            info.name = "Voice Mode";
            info.scale = sdk::ParameterScale::Discrete;
            info.maxValue = 2.0f;
            info.defaultValue = 0.0f;
            info.choices = sdk::choicesFromLabels({"Poly", "Mono", "Legato"});
            return info;
        }
        info.stableId = "debug" + std::to_string(index + 1);
        info.name = "Param " + std::to_string(index + 1);
        return info;
    }

    float parameterValue(int index) const override {
        return index >= 0 && index < kParamCount ? values_[static_cast<size_t>(index)] : 0.0f;
    }

    void setParameterValue(int index, float value) override {
        if (index >= 0 && index < kParamCount)
            values_[static_cast<size_t>(index)] = value;
    }

  private:
    std::array<float, kParamCount> values_ = [] {
        std::array<float, kParamCount> values{};
        values.fill(0.5f);
        values[kVoiceModeParamIndex] = 0.0f;
        return values;
    }();
};

class AllControlsDebugPlugin : public SdkDeviceAdapter {
  public:
    AllControlsDebugPlugin() : SdkDeviceAdapter(std::make_unique<AllControlsDebugDevice>()) {}

    static constexpr const char* xmlTypeName = AllControlsDebugDevice::kDeviceType;
};

}  // namespace magda::daw::audio
