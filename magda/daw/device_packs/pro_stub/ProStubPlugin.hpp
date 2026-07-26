#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace magda::pro_stub {

namespace te = tracktion::engine;

/**
 * Transparent proof device for the optional private-pack build path.
 *
 * It intentionally uses only Tracktion's Plugin contract and the public MAGDA
 * registration API. A real private pack can replace this directory without
 * changing the host or base-device targets.
 */
class ProStubPlugin final : public te::Plugin {
  public:
    explicit ProStubPlugin(const te::PluginCreationInfo& info) : te::Plugin(info) {}
    ~ProStubPlugin() override {
        notifyListenersOfDeletion();
    }

    static constexpr const char* xmlTypeName = "magda-pro-stub";

    juce::String getName() const override {
        return "Pro Pack Stub";
    }
    juce::String getPluginType() override {
        return xmlTypeName;
    }
    juce::String getShortName(int) override {
        return "Pro Stub";
    }
    juce::String getSelectableDescription() override {
        return getName();
    }

    void initialise(const te::PluginInitialisationInfo&) override {}
    void deinitialise() override {}
    void reset() override {}
    void applyToBuffer(const te::PluginRenderContext&) override {}

    bool takesMidiInput() override {
        return false;
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
    void restorePluginStateFromValueTree(const juce::ValueTree&) override {}
};

}  // namespace magda::pro_stub
