#pragma once
#include <tracktion_engine/tracktion_engine.h>

#include "../audio/plugins/ArpeggiatorPlugin.hpp"
#include "../audio/plugins/AudioSidechainMonitorPlugin.hpp"
#include "../audio/plugins/DrumGridPlugin.hpp"
#include "../audio/plugins/FaustInstrumentPlugin.hpp"
#include "../audio/plugins/FaustPlugin.hpp"
#include "../audio/plugins/FollowerSourceTapPlugin.hpp"
#include "../audio/plugins/InstrumentMeterTapPlugin.hpp"
#include "../audio/plugins/LevelsPlugin.hpp"
#include "../audio/plugins/MagdaSamplerPlugin.hpp"
#include "../audio/plugins/MidiChordEnginePlugin.hpp"
#include "../audio/plugins/MidiReceivePlugin.hpp"
#include "../audio/plugins/MidiStrumPlugin.hpp"
#include "../audio/plugins/OscilloscopePlugin.hpp"
#include "../audio/plugins/PolyStepSequencerPlugin.hpp"
#include "../audio/plugins/SidechainMonitorPlugin.hpp"
#include "../audio/plugins/SpectrumAnalyzerPlugin.hpp"
#include "../audio/plugins/StepSequencerPlugin.hpp"
#include "../audio/plugins/TrackMeasurementPlugin.hpp"
#include "../audio/plugins/compiled/CompiledPluginRegistry.hpp"
#include "../audio/plugins/mutable/MutableCloudsPlugin.hpp"
#include "../audio/plugins/mutable/MutableElementsPlugin.hpp"
#include "../audio/plugins/mutable/MutableRingsPlugin.hpp"
#include "../audio/session/SessionMonitorPlugin.hpp"
#include "../project/ProjectManager.hpp"

namespace magda {

class MagdaEngineBehaviour : public tracktion::EngineBehaviour {
  public:
    // Prevent TE from auto-initialising the device manager during Engine construction.
    // We do it ourselves in initializeDeviceManager() after validating saved state,
    // to avoid CoreAudio hangs from broken Settings.xml entries.
    bool autoInitialiseDeviceManager() override {
        return false;
    }

    // Disable JUCE driver timestamps for MIDI — works around a JUCE 8.0.10 bug
    // where CoreMIDI timestamps are incorrectly scaled (1e6 instead of 1e-6).
    // When false, TE uses getMillisecondCounterHiRes() which is accurate and correct.
    // TODO: Re-evaluate when upgrading to JUCE >= 8.0.11 (fix: 8b0ae502ff)
    bool isMidiDriverUsedForIncommingMessageTiming() override {
        return false;
    }

    // Process muted tracks so LevelMeterPlugin still receives audio and meters
    // stay active. Track output is still silenced by TrackMutingNode.
    bool shouldProcessMutedTracks() override {
        return true;
    }

    tracktion::EditLimits getEditLimits() override {
        auto limits = tracktion::EngineBehaviour::getEditLimits();
        limits.maxNumMasterPlugins = 64;
        return limits;
    }

    juce::File getDefaultFolderForAudioRecordings(tracktion::Edit&) override {
        auto recDir = ProjectManager::getInstance().getRecordingsDirectory();
        if (recDir != juce::File()) {
            recDir.createDirectory();
            return recDir;
        }
        return {};
    }

    // Return a full file path for new recordings — this has higher priority than
    // the %projectdir% pattern expansion, which can be overridden by editFileRetriever.
    juce::File getFileForNewAudioRecording(tracktion::Track& track,
                                           const juce::String& fileExtension) override {
        auto recDir = ProjectManager::getInstance().getRecordingsDirectory();
        if (recDir == juce::File())
            return {};

        recDir.createDirectory();
        auto now = juce::Time::getCurrentTime();
        auto date = juce::String(now.getDayOfMonth()) +
                    juce::Time::getMonthName(now.getMonth(), true) + juce::String(now.getYear());
        auto time = juce::String::formatted("%d%02d%02d", now.getHours(), now.getMinutes(),
                                            now.getSeconds());

        for (int take = 1;; ++take) {
            auto name = track.getName() + "_" + date + "_" + time + "_" + juce::String(take);
            auto file = recDir.getChildFile(name + fileExtension);
            if (!file.exists())
                return file;
        }
    }

    tracktion::Plugin::Ptr createCustomPlugin(tracktion::PluginCreationInfo info) override {
        auto type = info.state[tracktion::IDs::type].toString();
        if (type == daw::audio::MagdaSamplerPlugin::xmlTypeName) {
            return new daw::audio::MagdaSamplerPlugin(info);
        }
        if (type == daw::audio::MutableElementsPlugin::xmlTypeName) {
            return new daw::audio::MutableElementsPlugin(info);
        }
        if (type == daw::audio::MutableRingsPlugin::xmlTypeName) {
            return new daw::audio::MutableRingsPlugin(info);
        }
        if (type == daw::audio::MutableCloudsPlugin::xmlTypeName) {
            return new daw::audio::MutableCloudsPlugin(info);
        }
        if (type == daw::audio::DrumGridPlugin::xmlTypeName) {
            DBG("MagdaEngineBehaviour::createCustomPlugin - creating DrumGridPlugin");
            return new daw::audio::DrumGridPlugin(info);
        }
        if (type == SessionMonitorPlugin::xmlTypeName) {
            return new SessionMonitorPlugin(info);
        }
        if (type == SidechainMonitorPlugin::xmlTypeName) {
            DBG("MagdaEngineBehaviour::createCustomPlugin - creating SidechainMonitorPlugin");
            return new SidechainMonitorPlugin(info);
        }
        if (type == AudioSidechainMonitorPlugin::xmlTypeName) {
            DBG("MagdaEngineBehaviour::createCustomPlugin - creating AudioSidechainMonitorPlugin");
            return new AudioSidechainMonitorPlugin(info);
        }
        if (type == FollowerSourceTapPlugin::xmlTypeName) {
            DBG("MagdaEngineBehaviour::createCustomPlugin - creating FollowerSourceTapPlugin");
            return new FollowerSourceTapPlugin(info);
        }
        if (type == daw::audio::FaustPlugin::xmlTypeName) {
            DBG("MagdaEngineBehaviour::createCustomPlugin - creating FaustPlugin");
            return new daw::audio::FaustPlugin(info);
        }
        if (type == daw::audio::FaustInstrumentPlugin::xmlTypeName) {
            DBG("MagdaEngineBehaviour::createCustomPlugin - creating FaustInstrumentPlugin");
            return new daw::audio::FaustInstrumentPlugin(info);
        }
        // Compiled-Faust plugins go through the registry; one factory per
        // device lives in its own .cpp (see CompiledPluginRegistry.hpp).
        if (auto* spec = daw::audio::compiled::findCompiledPluginSpec(type)) {
            return spec->createPlugin(info);
        }
        if (type == MidiReceivePlugin::xmlTypeName) {
            DBG("MagdaEngineBehaviour::createCustomPlugin - creating MidiReceivePlugin");
            return new MidiReceivePlugin(info);
        }
        if (type == daw::audio::MidiChordEnginePlugin::xmlTypeName) {
            DBG("MagdaEngineBehaviour::createCustomPlugin - creating MidiChordEnginePlugin");
            return new daw::audio::MidiChordEnginePlugin(info);
        }
        if (type == daw::audio::ArpeggiatorPlugin::xmlTypeName) {
            return new daw::audio::ArpeggiatorPlugin(info);
        }
        if (type == daw::audio::MidiStrumPlugin::xmlTypeName) {
            return new daw::audio::MidiStrumPlugin(info);
        }
        if (type == daw::audio::StepSequencerPlugin::xmlTypeName) {
            return new daw::audio::StepSequencerPlugin(info);
        }
        if (type == daw::audio::PolyStepSequencerPlugin::xmlTypeName) {
            return new daw::audio::PolyStepSequencerPlugin(info);
        }
        if (type == daw::audio::OscilloscopePlugin::xmlTypeName) {
            return new daw::audio::OscilloscopePlugin(info);
        }
        if (type == daw::audio::SpectrumAnalyzerPlugin::xmlTypeName) {
            return new daw::audio::SpectrumAnalyzerPlugin(info);
        }
        if (type == daw::audio::LevelsPlugin::xmlTypeName) {
            return new daw::audio::LevelsPlugin(info);
        }
        if (type == daw::audio::InstrumentMeterTapPlugin::xmlTypeName) {
            return new daw::audio::InstrumentMeterTapPlugin(info);
        }
        if (type == daw::audio::TrackMeasurementPlugin::xmlTypeName) {
            return new daw::audio::TrackMeasurementPlugin(info);
        }
        if (type == tracktion::ImpulseResponsePlugin::xmlTypeName) {
            return new tracktion::ImpulseResponsePlugin(info);
        }
        DBG("MagdaEngineBehaviour::createCustomPlugin - unknown type: " << type);
        return {};
    }
};

}  // namespace magda
