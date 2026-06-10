#include "ControllerActivation.hpp"

#include "../../audio/midi/MidiDeviceMatch.hpp"
#include "BindingRegistry.hpp"
#include "ControllerRegistry.hpp"

namespace magda::controllers {

namespace {
// Injected by the app layer; null until wired (defaults to "profile active").
std::function<bool()> g_profileSurfaceActiveProvider;
}  // namespace

bool isControllerConnected(const Controller& c,
                           const juce::Array<juce::MidiDeviceInfo>& liveInputs) {
    if (c.inputPort.isEmpty())
        return false;
    for (const auto& dev : liveInputs) {
        if (magda::midi::matches(c.inputPort, dev.identifier, dev.name))
            return true;
    }
    return false;
}

bool isControllerConnected(const ControllerId& id) {
    auto c = ControllerRegistry::getInstance().find(id);
    if (!c)
        return false;
    return isControllerConnected(*c, juce::MidiInput::getAvailableDevices());
}

void setProfileSurfaceActiveProvider(std::function<bool()> provider) {
    g_profileSurfaceActiveProvider = std::move(provider);
}

bool isProfileSurfaceActive() {
    return g_profileSurfaceActiveProvider ? g_profileSurfaceActiveProvider() : true;
}

bool isDeviceAutomapLive(const ChainNodePath& devicePath) {
    if (!isProfileSurfaceActive())
        return false;
    auto owner = BindingRegistry::getInstance().resolverControllerForDevice(devicePath);
    return owner.has_value() && isControllerConnected(*owner);
}

bool isDeviceUserMapLive(const ChainNodePath& devicePath) {
    if (!isProfileSurfaceActive())
        return false;
    auto owner = BindingRegistry::getInstance().userMappingControllerForDevice(devicePath);
    return owner.has_value() && isControllerConnected(*owner);
}

}  // namespace magda::controllers
