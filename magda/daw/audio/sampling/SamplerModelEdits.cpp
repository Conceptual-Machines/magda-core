#include "sampling/SamplerModelEdits.hpp"

#include <functional>

#include "core/DeviceState.hpp"
#include "core/TrackManager.hpp"
#include "plugins/MagdaSamplerPlugin.hpp"
#include "project/ProjectManager.hpp"

namespace magda::sampler_edits {

namespace {

using Sampler = daw::audio::MagdaSamplerPlugin;
using IDs = Sampler::StateIDs;
using Zone = daw::audio::SamplerZoneSettings;

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

constexpr int kMarkerSlots[] = {Sampler::kSampleStart, Sampler::kSampleEnd, Sampler::kLoopStart,
                                Sampler::kLoopEnd};

Zone readZone(const device_state::Node& node) {
    const auto& props = node.props;
    const auto number = [&props](const juce::Identifier& id, double fallback) {
        return props.contains(id) ? static_cast<double>(props[id]) : fallback;
    };
    Zone zone;
    zone.source = props[IDs::source].toString();
    zone.rootNote = static_cast<int>(number(IDs::rootNote, 60));
    for (const auto& field : daw::audio::kSamplerZoneFields)
        zone.zones.*field.member = static_cast<int>(number(field.key, field.fallback));
    zone.zones.roundRobin = static_cast<bool>(props[IDs::roundRobin]);
    zone.tuneCents = static_cast<float>(number(IDs::tune, 0.0));
    zone.gainDb = static_cast<float>(number(IDs::gain, 0.0));
    zone.region.start = number(IDs::start, 0.0);
    zone.region.end = number(IDs::end, 0.0);
    zone.region.loop = static_cast<bool>(props[IDs::loopEnabled]);
    zone.region.loopStart = number(IDs::loopStart, 0.0);
    zone.region.loopEnd = number(IDs::loopEnd, 0.0);
    return zone;
}

/// A zone's settings onto @p node. The one-zone sampler's root leaves out what is at its
/// default, so its document reads as it did before zones, and its markers are parameters.
void writeZone(device_state::Node& node, const Zone& zone, bool isRoot) {
    auto& props = node.props;
    const auto set = [&props, isRoot](const juce::Identifier& id, const juce::var& value,
                                      const juce::var& fallback) {
        if (isRoot && value == fallback)
            props.remove(id);
        else
            props.set(id, value);
    };
    props.set(IDs::source, zone.source);
    props.set(IDs::rootNote, zone.rootNote);
    for (const auto& field : daw::audio::kSamplerZoneFields)
        set(field.key, zone.zones.*field.member, field.fallback);
    set(IDs::tune, zone.tuneCents, 0.0f);
    set(IDs::gain, zone.gainDb, 0.0f);
    set(IDs::roundRobin, zone.zones.roundRobin, false);
    props.set(IDs::loopEnabled, zone.region.loop);
    if (isRoot)
        return;
    props.set(IDs::start, zone.region.start);
    props.set(IDs::end, zone.region.end);
    props.set(IDs::loopStart, zone.region.loopStart);
    props.set(IDs::loopEnd, zone.region.loopEnd);
}

bool isZoneNode(const device_state::Node& node) {
    return node.type == IDs::zone.toString();
}

/// What a newly chosen @p file plays: all of it, rooted where its metadata says.
std::optional<Zone> zoneFor(const juce::File& file) {
    const auto choice = Sampler::readSampleChoice(file);
    if (!choice.valid)
        return std::nullopt;
    Zone zone;
    zone.source = file.getFullPathName();
    zone.rootNote = choice.rootNote;
    zone.region.end = choice.markerSeconds;
    zone.region.loopEnd = choice.markerSeconds;
    return zone;
}

/// The samples among @p files, folders opened.
juce::Array<juce::File> samplesIn(const juce::Array<juce::File>& files) {
    juce::Array<juce::File> samples;
    for (const auto& file : files) {
        if (file.isDirectory()) {
            auto inside = file.findChildFiles(juce::File::findFiles, true);
            inside.sort();
            for (const auto& child : inside)
                if (Sampler::readSampleChoice(child).valid)
                    samples.add(child);
        } else if (Sampler::readSampleChoice(file).valid) {
            samples.add(file);
        }
    }
    return samples;
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
            std::erase_if(doc.root.children, isZoneNode);
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

std::vector<Zone> zones(const ChainNodePath& devicePath) {
    const auto* device = TrackManager::getInstance().getDeviceInChainByPath(devicePath);
    if (device == nullptr)
        return {};
    const auto doc = device_state::decode(device->pluginState);
    if (!doc)
        return {};

    std::vector<Zone> found;
    for (const auto& child : doc->root.children)
        if (isZoneNode(child))
            found.push_back(readZone(child));
    if (!found.empty() || doc->root.props[IDs::source].toString().isEmpty())
        return found;

    auto zone = readZone(doc->root);
    double* const markers[] = {&zone.region.start, &zone.region.end, &zone.region.loopStart,
                               &zone.region.loopEnd};
    for (size_t i = 0; i < std::size(kMarkerSlots); ++i)
        if (const auto* param = device->findParameterByIndex(kMarkerSlots[i]))
            *markers[i] = param->currentValue;
    return {zone};
}

bool setZones(const ChainNodePath& devicePath, const std::vector<Zone>& zones) {
    const bool list = zones.size() > 1;
    if (!authorState(devicePath, [&zones, list](device_state::Doc& doc) {
            std::erase_if(doc.root.children, isZoneNode);
            if (list) {
                for (const auto& id : {IDs::source, IDs::rootNote, IDs::loopEnabled, IDs::tune,
                                       IDs::gain, IDs::roundRobin})
                    doc.root.props.remove(id);
                for (const auto& field : daw::audio::kSamplerZoneFields)
                    doc.root.props.remove(field.key);
                for (const auto& zone : zones) {
                    device_state::Node node;
                    node.type = IDs::zone.toString();
                    writeZone(node, zone, false);
                    doc.root.children.push_back(std::move(node));
                }
            } else if (zones.empty()) {
                doc.root.props.remove(IDs::source);
            } else {
                writeZone(doc.root, zones.front(), true);
            }
        }))
        return false;

    // The one zone's markers are the parameters, written after the state for loadSample()'s
    // reason.
    if (zones.size() == 1) {
        const auto& region = zones.front().region;
        const double markers[] = {region.start, region.end, region.loopStart, region.loopEnd};
        for (size_t i = 0; i < std::size(kMarkerSlots); ++i)
            TrackManager::getInstance().setDeviceParameterValue(devicePath, kMarkerSlots[i],
                                                                static_cast<float>(markers[i]));
    }
    return true;
}

bool loadSamples(const ChainNodePath& devicePath, const juce::Array<juce::File>& files) {
    const auto samples = samplesIn(files);
    if (samples.isEmpty())
        return false;
    if (samples.size() == 1)
        return loadSample(devicePath, samples.getFirst());

    const auto mapped =
        daw::audio::mapSamplesByName(samples, [](const juce::File& file) -> std::optional<int> {
            const auto choice = Sampler::readSampleChoice(file);
            if (choice.rootFromMetadata)
                return choice.rootNote;
            return std::nullopt;
        });
    std::vector<Zone> zones;
    for (const auto& sample : mapped) {
        auto zone = sample.zone;
        const auto seconds = Sampler::readSampleChoice(sample.file).markerSeconds;
        zone.region.end = seconds;
        zone.region.loopEnd = seconds;
        zones.push_back(zone);
    }
    return setZones(devicePath, zones);
}

bool addZones(const ChainNodePath& devicePath, const juce::Array<juce::File>& dropped) {
    const auto files = samplesIn(dropped);
    auto all = zones(devicePath);
    if (all.empty() && files.size() == 1)
        return loadSample(devicePath, files.getFirst());

    const auto before = all.size();
    for (const auto& file : files)
        if (auto added = zoneFor(file)) {
            if (const auto named = daw::audio::noteFromFileName(file.getFileNameWithoutExtension()))
                added->rootNote = *named;
            all.push_back(*added);
        }
    return all.size() > before && setZones(devicePath, all);
}

bool addZone(const ChainNodePath& devicePath, const juce::File& file, int key) {
    auto added = zoneFor(file);
    if (!added)
        return false;
    added->rootNote = added->zones.keyLow = added->zones.keyHigh = juce::jlimit(0, 127, key);
    auto all = zones(devicePath);
    all.push_back(*added);
    return setZones(devicePath, all);
}

bool updateZone(const ChainNodePath& devicePath, int index, const Zone& zone) {
    auto all = zones(devicePath);
    if (index < 0 || index >= static_cast<int>(all.size()))
        return false;
    all[static_cast<size_t>(index)] = zone;
    return setZones(devicePath, all);
}

bool removeZone(const ChainNodePath& devicePath, int index) {
    auto all = zones(devicePath);
    if (index < 0 || index >= static_cast<int>(all.size()))
        return false;
    all.erase(all.begin() + index);
    return setZones(devicePath, all);
}

}  // namespace magda::sampler_edits
