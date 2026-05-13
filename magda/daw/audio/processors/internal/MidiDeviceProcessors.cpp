#include "processors/internal/MidiDeviceProcessors.hpp"

#include <utility>

#include "plugins/ArpeggiatorPlugin.hpp"
#include "plugins/DrumGridPlugin.hpp"
#include "plugins/StepSequencerPlugin.hpp"
#include "processors/ParameterInfoBuilder.hpp"

namespace magda {

// =============================================================================
// ArpeggiatorProcessor
// =============================================================================

ArpeggiatorProcessor::ArpeggiatorProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : DeviceProcessor(deviceId, std::move(plugin)) {}

daw::audio::ArpeggiatorPlugin* ArpeggiatorProcessor::getArpPlugin() const {
    return dynamic_cast<daw::audio::ArpeggiatorPlugin*>(plugin_.get());
}

int ArpeggiatorProcessor::getParameterCount() const {
    if (plugin_)
        return static_cast<int>(plugin_->getAutomatableParameters().size());
    return 0;
}

ParameterInfo ArpeggiatorProcessor::getParameterInfo(int index) const {
    if (!plugin_)
        return {};
    auto params = plugin_->getAutomatableParameters();
    if (index < 0 || index >= static_cast<int>(params.size()))
        return {};
    ParameterInfo info = makeInfoFromTeParam(index, params[index]);
    // Depth (5) and skew (6) default to bipolar; all others unipolar
    info.bipolarModulation = (index == 5 || index == 6);
    return info;
}

void ArpeggiatorProcessor::populateParameters(DeviceInfo& info) const {
    info.parameters.clear();
    for (int i = 0; i < getParameterCount(); ++i) {
        info.parameters.push_back(getParameterInfo(i));
    }
}

void ArpeggiatorProcessor::setParameterByIndex(int paramIndex, float value) {
    if (!plugin_)
        return;

    auto params = plugin_->getAutomatableParameters();
    int autoCount = static_cast<int>(params.size());

    if (paramIndex >= 0 && paramIndex < autoCount) {
        params[paramIndex]->setParameterFromHost(value, juce::sendNotificationSync);
    }
}

float ArpeggiatorProcessor::getParameterByIndex(int paramIndex) const {
    if (!plugin_)
        return 0.0f;

    auto params = plugin_->getAutomatableParameters();
    int autoCount = static_cast<int>(params.size());

    if (paramIndex >= 0 && paramIndex < autoCount)
        return params[paramIndex]->getCurrentValue();
    return 0.0f;
}

// =============================================================================
// StepSequencerProcessor
// =============================================================================

StepSequencerProcessor::StepSequencerProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : DeviceProcessor(deviceId, std::move(plugin)) {}

daw::audio::StepSequencerPlugin* StepSequencerProcessor::getSeqPlugin() const {
    return dynamic_cast<daw::audio::StepSequencerPlugin*>(plugin_.get());
}

int StepSequencerProcessor::getParameterCount() const {
    if (plugin_)
        return static_cast<int>(plugin_->getAutomatableParameters().size());
    return 0;
}

ParameterInfo StepSequencerProcessor::getParameterInfo(int index) const {
    if (!plugin_)
        return {};
    auto params = plugin_->getAutomatableParameters();
    if (index < 0 || index >= static_cast<int>(params.size()))
        return {};
    ParameterInfo info = makeInfoFromTeParam(index, params[index]);
    // Timing Depth (6) and Timing Skew (7) are bipolar
    info.bipolarModulation = (index == 6 || index == 7);
    return info;
}

void StepSequencerProcessor::populateParameters(DeviceInfo& info) const {
    info.parameters.clear();
    for (int i = 0; i < getParameterCount(); ++i) {
        info.parameters.push_back(getParameterInfo(i));
    }
}

void StepSequencerProcessor::setParameterByIndex(int paramIndex, float value) {
    if (!plugin_)
        return;

    auto params = plugin_->getAutomatableParameters();
    int autoCount = static_cast<int>(params.size());

    if (paramIndex >= 0 && paramIndex < autoCount) {
        params[paramIndex]->setParameterFromHost(value, juce::sendNotificationSync);
    }
}

float StepSequencerProcessor::getParameterByIndex(int paramIndex) const {
    if (!plugin_)
        return 0.0f;

    auto params = plugin_->getAutomatableParameters();
    int autoCount = static_cast<int>(params.size());

    if (paramIndex >= 0 && paramIndex < autoCount)
        return params[paramIndex]->getCurrentValue();
    return 0.0f;
}

// =============================================================================
// DrumGridProcessor
// =============================================================================

DrumGridProcessor::DrumGridProcessor(DeviceId deviceId, te::Plugin::Ptr plugin)
    : DeviceProcessor(deviceId, std::move(plugin)) {}

int DrumGridProcessor::getParameterCount() const {
    if (plugin_)
        return static_cast<int>(plugin_->getAutomatableParameters().size());
    return 0;
}

ParameterInfo DrumGridProcessor::getParameterInfo(int index) const {
    if (!plugin_)
        return {};
    auto params = plugin_->getAutomatableParameters();
    if (index < 0 || index >= static_cast<int>(params.size()))
        return {};
    ParameterInfo info = makeInfoFromTeParam(index, params[index]);
    // Pan params (odd indices) are bipolar
    info.bipolarModulation = (index % 2 == 1);
    return info;
}

void DrumGridProcessor::populateParameters(DeviceInfo& info) const {
    info.parameters.clear();
    for (int i = 0; i < getParameterCount(); ++i)
        info.parameters.push_back(getParameterInfo(i));
}

}  // namespace magda
