#pragma once
#include <tracktion_engine/tracktion_engine.h>

#include "../audio/DrumGridPlugin.hpp"
#include "../audio/MagdaSamplerPlugin.hpp"
#include "../audio/SidechainMonitorPlugin.hpp"

namespace magda {

class MagdaEngineBehaviour : public tracktion::EngineBehaviour {
  public:
    // Disable JUCE driver timestamps for MIDI — works around a JUCE 8.0.10 bug
    // where CoreMIDI timestamps are incorrectly scaled (1e6 instead of 1e-6).
    // When false, TE uses getMillisecondCounterHiRes() which is accurate and correct.
    // TODO: Re-evaluate when upgrading to JUCE >= 8.0.11 (fix: 8b0ae502ff)
    bool isMidiDriverUsedForIncommingMessageTiming() override {
        return false;
    }

    tracktion::Plugin::Ptr createCustomPlugin(tracktion::PluginCreationInfo info) override {
        auto type = info.state[tracktion::IDs::type].toString();
        if (type == daw::audio::MagdaSamplerPlugin::xmlTypeName) {
            DBG("MagdaEngineBehaviour::createCustomPlugin - creating MagdaSamplerPlugin");
            return new daw::audio::MagdaSamplerPlugin(info);
        }
        if (type == daw::audio::DrumGridPlugin::xmlTypeName) {
            DBG("MagdaEngineBehaviour::createCustomPlugin - creating DrumGridPlugin");
            return new daw::audio::DrumGridPlugin(info);
        }
        if (type == SidechainMonitorPlugin::xmlTypeName) {
            DBG("MagdaEngineBehaviour::createCustomPlugin - creating SidechainMonitorPlugin");
            return new SidechainMonitorPlugin(info);
        }
        DBG("MagdaEngineBehaviour::createCustomPlugin - unknown type: " << type);
        return {};
    }
};

}  // namespace magda
