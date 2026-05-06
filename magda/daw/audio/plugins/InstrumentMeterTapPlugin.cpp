#include "plugins/InstrumentMeterTapPlugin.hpp"

#include "DeviceMeteringManager.hpp"

namespace magda::daw::audio {

const char* InstrumentMeterTapPlugin::xmlTypeName = "instrumentmetertap";
namespace {
const juce::Identifier deviceIdProperty{"magdaDeviceId"};

void storeMax(std::atomic<float>* target, float value) {
    if (!target)
        return;

    auto current = target->load(std::memory_order_relaxed);
    while (value > current &&
           !target->compare_exchange_weak(current, value, std::memory_order_relaxed,
                                          std::memory_order_relaxed)) {
    }
}
}  // namespace

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
    peakL_.store(nullptr, std::memory_order_release);
    peakR_.store(nullptr, std::memory_order_release);
    gainLinear_.store(nullptr, std::memory_order_release);
    if (tapStorage_)
        retiredTapStorage_.push_back(std::move(tapStorage_));

    const auto deviceId = deviceId_.load(std::memory_order_relaxed);
    if (deviceId == INVALID_DEVICE_ID)
        return;

    if (auto* manager = DeviceMeteringManager::getInstanceForEdit(edit)) {
        auto tap = manager->getRealtimeTap(deviceId);
        if (tap.isValid()) {
            tapStorage_ = tap.storage;
            peakL_.store(tap.peakL, std::memory_order_release);
            peakR_.store(tap.peakR, std::memory_order_release);
            gainLinear_.store(tap.gainLinear, std::memory_order_release);
        }
    }
}

void InstrumentMeterTapPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!fc.destBuffer || fc.bufferNumSamples <= 0)
        return;

    auto* peakL = peakL_.load(std::memory_order_acquire);
    auto* peakR = peakR_.load(std::memory_order_acquire);
    auto* gainLinear = gainLinear_.load(std::memory_order_acquire);
    if (!peakL || !peakR || !gainLinear)
        return;

    const float gain = gainLinear->load(std::memory_order_relaxed);
    if (gain != 1.0f)
        fc.destBuffer->applyGain(fc.bufferStartSample, fc.bufferNumSamples, gain);

    const int numChannels = fc.destBuffer->getNumChannels();
    const float left =
        numChannels > 0 ? fc.destBuffer->getMagnitude(0, fc.bufferStartSample, fc.bufferNumSamples)
                        : 0.0f;
    const float right =
        numChannels > 1 ? fc.destBuffer->getMagnitude(1, fc.bufferStartSample, fc.bufferNumSamples)
                        : left;

    storeMax(peakL, left);
    storeMax(peakR, right);
}

}  // namespace magda::daw::audio
