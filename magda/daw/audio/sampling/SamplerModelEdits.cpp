#include "sampling/SamplerModelEdits.hpp"

#include <functional>

#include "core/DeviceState.hpp"
#include "core/TrackManager.hpp"
#include "plugins/MagdaSamplerPlugin.hpp"
#include "project/ProjectManager.hpp"

namespace magda::sampler_edits {

namespace {

using Sampler = daw::audio::MagdaSamplerPlugin;

/// Patch the device's state document on the model; the projection pushes the
/// result into the live device. The model is what autosave writes and what both
/// engines build from, so a change also dirties the project.
bool authorState(const ChainNodePath& devicePath,
                 const std::function<void(device_state::Doc&)>& patch) {
    if (!TrackManager::getInstance().updateDeviceAuthoredState(devicePath, patch))
        return false;
    ProjectManager::getInstance().markDirty();
    return true;
}

}  // namespace

bool setLoopEnabled(const ChainNodePath& devicePath, bool enabled) {
    return authorState(devicePath, [enabled](device_state::Doc& doc) {
        doc.root.props.set(Sampler::StateIDs::loopEnabled, enabled);
    });
}

bool setRootNote(const ChainNodePath& devicePath, int note) {
    return authorState(devicePath, [note](device_state::Doc& doc) {
        doc.root.props.set(Sampler::StateIDs::rootNote, note);
    });
}

bool loadSample(const ChainNodePath& devicePath, const juce::File& file) {
    const auto choice = Sampler::readSampleChoice(file);
    if (!choice.valid)
        return false;

    const auto& path = file.getFullPathName();
    if (!authorState(devicePath, [&path, &choice](device_state::Doc& doc) {
            doc.root.props.set(Sampler::StateIDs::source, path);
            doc.root.props.set(Sampler::StateIDs::rootNote, choice.rootNote);
        }))
        return false;

    // After the state, because the projection seats the model's markers on the
    // device on its way in: written first, they would be the ones the new
    // sample inherits rather than the ones it replaces.
    auto& tracks = TrackManager::getInstance();
    tracks.setDeviceParameterValue(devicePath, Sampler::kSampleStart, 0.0f);
    tracks.setDeviceParameterValue(devicePath, Sampler::kSampleEnd, choice.markerSeconds);
    tracks.setDeviceParameterValue(devicePath, Sampler::kLoopStart, 0.0f);
    tracks.setDeviceParameterValue(devicePath, Sampler::kLoopEnd, choice.markerSeconds);
    return true;
}

}  // namespace magda::sampler_edits
