// The devices MAGDA exposes through the SDK's C ABI: the JUCE plugin, the wasm module and the
// parity renderer are built from this list (#2940).

#include <magda/sdk/abi/DeviceModule.hpp>

#include "devices/tone/ToneGenerator.hpp"

namespace {

constexpr magda::sdk::abi::DeviceFactory kDevices[] = {
    {magda::devices::ToneGenerator::kDeviceType,
     []() -> std::unique_ptr<magda::sdk::Device> {
         return std::make_unique<magda::devices::ToneGenerator>();
     }},
};

}  // namespace

std::span<const magda::sdk::abi::DeviceFactory> magda::sdk::abi::moduleDevices() {
    return kDevices;
}
