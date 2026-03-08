#pragma once
#include <tracktion_engine/tracktion_engine.h>

#include "../audio/DrumGridPlugin.hpp"
#include "../audio/MagdaSamplerPlugin.hpp"
#include "../audio/MidiReceivePlugin.hpp"
#include "../audio/SidechainMonitorPlugin.hpp"
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
        if (type == daw::audio::DrumGridPlugin::xmlTypeName) {
            DBG("MagdaEngineBehaviour::createCustomPlugin - creating DrumGridPlugin");
            return new daw::audio::DrumGridPlugin(info);
        }
        if (type == SidechainMonitorPlugin::xmlTypeName) {
            DBG("MagdaEngineBehaviour::createCustomPlugin - creating SidechainMonitorPlugin");
            return new SidechainMonitorPlugin(info);
        }
        if (type == MidiReceivePlugin::xmlTypeName) {
            DBG("MagdaEngineBehaviour::createCustomPlugin - creating MidiReceivePlugin");
            return new MidiReceivePlugin(info);
        }
        DBG("MagdaEngineBehaviour::createCustomPlugin - unknown type: " << type);
        return {};
    }
};

}  // namespace magda
