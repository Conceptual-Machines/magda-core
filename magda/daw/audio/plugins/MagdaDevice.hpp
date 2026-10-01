#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

#include <algorithm>
#include <magda/sdk/device/Device.hpp>
#include <ranges>

#include "core/ParameterInfo.hpp"
#include "core/SidechainPort.hpp"
#include "plugins/DeviceJuceInterop.hpp"

/**
 * @file MagdaDevice.hpp
 * @brief MAGDA's device base: the JUCE-free SDK contract, plus what the app describes it with.
 *
 * Everything a host needs to run a device is sdk::Device (docs/device-interface.md in the
 * SDK). What is added here is how the app presents the device's parameters, which stays
 * with the model until the parameter manifest (#2939) replaces it.
 */

namespace magda::daw::audio {

using DeviceProperties = sdk::DeviceProperties;
using DevicePrepareContext = sdk::PrepareContext;
using DeviceProcessContext = sdk::ProcessContext;
using sdk::DeviceTelemetry;
using sdk::midiEventPosition;
using sdk::MidiEventPosition;

class MagdaDevice : public sdk::Device {
  public:
    void setHost(sdk::DeviceHost* host) override {
        host_ = host;
    }

    /// How @p slot is described to the model and the UI. Slots are those of parameterCount().
    virtual ParameterInfo parameterInfo(int) const {
        return {};
    }

    /// Every parameter this device describes, in slot order. The view yields values,
    /// because parameterInfo() builds one per call.
    auto parameters() const {
        return std::views::iota(0, std::max(0, parameterCount())) |
               std::views::transform([this](int slot) { return parameterInfo(slot); });
    }

  protected:
    /// The host this device reports to, or null when it runs detached. Control thread.
    sdk::DeviceHost* host() const {
        return host_;
    }

  private:
    sdk::DeviceHost* host_ = nullptr;
};

}  // namespace magda::daw::audio
