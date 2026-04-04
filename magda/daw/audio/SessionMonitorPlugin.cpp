#include "SessionMonitorPlugin.hpp"

#include "SessionClipAudioMonitor.hpp"

namespace magda {

const char* SessionMonitorPlugin::xmlTypeName = "sessionmonitor";

SessionMonitorPlugin::SessionMonitorPlugin(const te::PluginCreationInfo& info) : te::Plugin(info) {}

SessionMonitorPlugin::~SessionMonitorPlugin() {
    notifyListenersOfDeletion();
}

void SessionMonitorPlugin::initialise(const te::PluginInitialisationInfo&) {}
void SessionMonitorPlugin::deinitialise() {}
void SessionMonitorPlugin::reset() {}

void SessionMonitorPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!audioMonitor_)
        return;

    double transportSeconds = fc.editTime.getStart().inSeconds();
    audioMonitor_->process(transportSeconds);
}

void SessionMonitorPlugin::restorePluginStateFromValueTree(const juce::ValueTree&) {}

}  // namespace magda
