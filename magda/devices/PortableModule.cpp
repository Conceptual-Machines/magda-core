// The devices MAGDA exposes through the SDK's C ABI: the JUCE plugins, the wasm module and the
// parity renderer are built from this list (#2940).

#include <magda/sdk/abi/DeviceModule.hpp>

#include "devices/faust/effects/Bitcrusher.hpp"
#include "devices/faust/effects/Chorus.hpp"
#include "devices/faust/effects/Clipper.hpp"
#include "devices/faust/effects/Compressor.hpp"
#include "devices/faust/effects/Delay.hpp"
#include "devices/faust/effects/Dimension.hpp"
#include "devices/faust/effects/Eq.hpp"
#include "devices/faust/effects/Filter.hpp"
#include "devices/faust/effects/Flanger.hpp"
#include "devices/faust/effects/FreqShift.hpp"
#include "devices/faust/effects/GateExpander.hpp"
#include "devices/faust/effects/GrainDelay.hpp"
#include "devices/faust/effects/Grit.hpp"
#include "devices/faust/effects/Limiter.hpp"
#include "devices/faust/effects/Mod.hpp"
#include "devices/faust/effects/Multiband.hpp"
#include "devices/faust/effects/Phaser.hpp"
#include "devices/faust/effects/Pitch.hpp"
#include "devices/faust/effects/Reverb.hpp"
#include "devices/faust/effects/RingMod.hpp"
#include "devices/faust/effects/Saturator.hpp"
#include "devices/faust/effects/Utility.hpp"
#include "devices/tone/ToneGenerator.hpp"

namespace {

using namespace magda::devices;

template <class Device> constexpr magda::sdk::abi::DeviceFactory device() {
    return {Device::xmlTypeName,
            []() -> std::unique_ptr<magda::sdk::Device> { return std::make_unique<Device>(); }};
}

constexpr magda::sdk::abi::DeviceFactory kDevices[] = {
    {ToneGenerator::kDeviceType,
     []() -> std::unique_ptr<magda::sdk::Device> { return std::make_unique<ToneGenerator>(); }},
    device<faust::Bitcrusher>(),
    device<faust::Chorus>(),
    device<faust::Clipper>(),
    device<faust::Compressor>(),
    device<faust::Delay>(),
    device<faust::Dimension>(),
    device<faust::Eq>(),
    device<faust::Filter>(),
    device<faust::Flanger>(),
    device<faust::FreqShift>(),
    device<faust::GateExpander>(),
    device<faust::GrainDelay>(),
    device<faust::Grit>(),
    device<faust::Limiter>(),
    device<faust::Mod>(),
    device<faust::Multiband>(),
    device<faust::Phaser>(),
    device<faust::Pitch>(),
    device<faust::Reverb>(),
    device<faust::RingMod>(),
    device<faust::Saturator>(),
    device<faust::Utility>(),
};

}  // namespace

std::span<const magda::sdk::abi::DeviceFactory> magda::sdk::abi::moduleDevices() {
    return kDevices;
}
