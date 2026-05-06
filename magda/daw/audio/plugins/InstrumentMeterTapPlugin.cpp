#include "plugins/InstrumentMeterTapPlugin.hpp"

#include "DeviceMeteringManager.hpp"

namespace magda::daw::audio {

const char* InstrumentMeterTapPlugin::xmlTypeName = "instrumentmetertap";
namespace {
const juce::Identifier deviceIdProperty{"magdaDeviceId"};
}

InstrumentMeterTapPlugin::InstrumentMeterTapPlugin(const te::PluginCreationInfo& info)
    : te::Plugin(info) {
    deviceId_.store(
        static_cast<DeviceId>(int(state.getProperty(deviceIdProperty, INVALID_DEVICE_ID))),
        std::memory_order_relaxed);
    bindRealtimeTap();
}

InstrumentMeterTapPlugin::~InstrumentMeterTapPlugin() {
    notifyListenersOfDeletion();
}

void InstrumentMeterTapPlugin::getChannelNames(juce::StringArray* ins, juce::StringArray* outs) {
    if (ins)
        ins->addArray({"Left", "Right"});
    if (outs)
        outs->addArray({"Left", "Right"});
}

void InstrumentMeterTapPlugin::setDeviceId(DeviceId deviceId) {
    deviceId_.store(deviceId, std::memory_order_relaxed);
    state.setProperty(deviceIdProperty, deviceId, getUndoManager());
    bindRealtimeTap();
}

void InstrumentMeterTapPlugin::bindRealtimeTap() {
    const auto deviceId = deviceId_.load(std::memory_order_relaxed);
    if (deviceId == INVALID_DEVICE_ID)
        return;

    if (auto* manager = DeviceMeteringManager::getInstanceForEdit(edit)) {
        auto tap = manager->getRealtimeTap(deviceId);
        if (tap.isValid()) {
            peakL_ = tap.peakL;
            peakR_ = tap.peakR;
            gainLinear_ = tap.gainLinear;
        }
    }
}

void InstrumentMeterTapPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!fc.destBuffer || fc.bufferNumSamples <= 0)
        return;

    if (peakL_ == nullptr || peakR_ == nullptr || gainLinear_ == nullptr)
        bindRealtimeTap();

    const float gain = gainLinear_ ? gainLinear_->load(std::memory_order_relaxed) : 1.0f;
    if (gain != 1.0f)
        fc.destBuffer->applyGain(fc.bufferStartSample, fc.bufferNumSamples, gain);

    const int numChannels = fc.destBuffer->getNumChannels();
    const float left =
        numChannels > 0 ? fc.destBuffer->getMagnitude(0, fc.bufferStartSample, fc.bufferNumSamples)
                        : 0.0f;
    const float right =
        numChannels > 1 ? fc.destBuffer->getMagnitude(1, fc.bufferStartSample, fc.bufferNumSamples)
                        : left;

    if (peakL_)
        peakL_->store(left, std::memory_order_relaxed);
    if (peakR_)
        peakR_->store(right, std::memory_order_relaxed);
}

}  // namespace magda::daw::audio
