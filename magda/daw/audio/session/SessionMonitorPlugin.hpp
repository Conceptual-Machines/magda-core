#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "plugins/DeviceServices.hpp"

namespace magda {

namespace te = tracktion;

/**
 * @brief Minimal te::Plugin that advances the injected session context on the
 * audio thread.
 *
 * Inserted once per Edit (on the master track or a utility track).
 * Passes audio and MIDI through unchanged — exists solely to provide
 * an audio-thread callback for session clip state monitoring.
 *
 * Registered via MagdaEngineBehaviour::createCustomPlugin().
 */
class SessionMonitorPlugin : public te::Plugin {
  public:
    SessionMonitorPlugin(const te::PluginCreationInfo& info);
    ~SessionMonitorPlugin() override;

    static const char* getPluginName() {
        return "Session Monitor";
    }
    static const char* xmlTypeName;

    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return xmlTypeName;
    }
    juce::String getShortName(int) override {
        return "SessMon";
    }
    juce::String getSelectableDescription() override {
        return getName();
    }

    void initialise(const te::PluginInitialisationInfo&) override;
    void deinitialise() override;
    void reset() override;

    void applyToBuffer(const te::PluginRenderContext&) override;

    bool takesMidiInput() override {
        return true;
    }
    bool takesAudioInput() override {
        return true;
    }
    bool isSynth() override {
        return false;
    }
    bool producesAudioWhenNoAudioInput() override {
        return false;
    }
    double getTailLength() const override {
        return 0.0;
    }

    void restorePluginStateFromValueTree(const juce::ValueTree&) override;

    void setSessionContext(daw::audio::DeviceSessionContext* context) {
        sessionContext_ = context;
    }

  private:
    daw::audio::DeviceSessionContext* sessionContext_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SessionMonitorPlugin)
};

}  // namespace magda
