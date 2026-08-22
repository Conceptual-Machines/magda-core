#include "PlanEditSequence.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

#include "core/RackInfo.hpp"
#include "plan/TrackRouting.hpp"

/**
 * @file PlanEditSequence.cpp
 * @brief The edit vocabulary, and the generator that strings it together (#2077).
 *
 * Every edit here is one a user can perform, which is the only reason to have a
 * vocabulary at all: a generator free to write arbitrary plans would spend its
 * time on shapes the compiler never emits, and the differ's decisions about
 * those are nobody's problem.
 */

namespace magda::edits {
namespace {

// --- reading and writing the project ----------------------------------------

/// The elements a rack chain holds, wherever the rack is nested.
std::vector<ChainElement>* chainElements(std::vector<ChainElement>& elements, ChainId chainId) {
    for (auto& element : elements) {
        if (!isRack(element))
            continue;

        auto& rack = getRack(element);
        for (auto& chain : rack.chains) {
            if (chain.id == chainId)
                return &chain.elements;
            if (auto* found = chainElements(chain.elements, chainId))
                return found;
        }
    }
    return nullptr;
}

/// The rack with this id, wherever it is nested, and the list holding it.
struct RackAt {
    std::vector<ChainElement>* container = nullptr;
    std::size_t index = 0;
    TrackId trackId = INVALID_TRACK_ID;
};

bool findRackIn(std::vector<ChainElement>& elements, RackId rackId, TrackId trackId, RackAt& out) {
    for (std::size_t i = 0; i < elements.size(); ++i) {
        if (!isRack(elements[i]))
            continue;

        auto& rack = getRack(elements[i]);
        if (rack.id == rackId) {
            out = RackAt{&elements, i, trackId};
            return true;
        }
        for (auto& chain : rack.chains)
            if (findRackIn(chain.elements, rackId, trackId, out))
                return true;
    }
    return false;
}

bool findRack(Project& project, RackId rackId, RackAt& out) {
    for (auto& track : project.tracks)
        if (findRackIn(track.chain.fxChainElements, rackId, track.id, out))
            return true;
    return false;
}

/// The chain with this id, and the rack that owns it.
struct ChainAt {
    RackInfo* rack = nullptr;
    std::size_t index = 0;
    TrackId trackId = INVALID_TRACK_ID;
};

bool findChainIn(std::vector<ChainElement>& elements, ChainId chainId, TrackId trackId,
                 ChainAt& out) {
    for (auto& element : elements) {
        if (!isRack(element))
            continue;

        auto& rack = getRack(element);
        for (std::size_t i = 0; i < rack.chains.size(); ++i) {
            if (rack.chains[i].id == chainId) {
                out = ChainAt{&rack, i, trackId};
                return true;
            }
            if (findChainIn(rack.chains[i].elements, chainId, trackId, out))
                return true;
        }
    }
    return false;
}

bool findChain(Project& project, ChainId chainId, ChainAt& out) {
    for (auto& track : project.tracks)
        if (findChainIn(track.chain.fxChainElements, chainId, track.id, out))
            return true;
    return false;
}

/// A device of the insert chain, wherever it sits, and the list holding it.
struct DeviceAt {
    std::vector<ChainElement>* container = nullptr;
    std::size_t index = 0;
    TrackId trackId = INVALID_TRACK_ID;
};

bool findDeviceIn(std::vector<ChainElement>& elements, DeviceId deviceId, TrackId trackId,
                  DeviceAt& out) {
    for (std::size_t i = 0; i < elements.size(); ++i) {
        if (isDevice(elements[i])) {
            if (getDevice(elements[i]).id == deviceId) {
                out = DeviceAt{&elements, i, trackId};
                return true;
            }
            continue;
        }

        for (auto& chain : getRack(elements[i]).chains)
            if (findDeviceIn(chain.elements, deviceId, trackId, out))
                return true;
    }
    return false;
}

bool findDevice(Project& project, DeviceId deviceId, DeviceAt& out) {
    for (auto& track : project.tracks)
        if (findDeviceIn(track.chain.fxChainElements, deviceId, track.id, out))
            return true;
    return false;
}

/// Where an edit adds something, or null when the project no longer has it.
std::vector<ChainElement>* siteElements(Project& project, const Site& site) {
    for (auto& track : project.tracks) {
        if (track.id != site.trackId)
            continue;
        if (site.chainId == INVALID_CHAIN_ID)
            return &track.chain.fxChainElements;
        return chainElements(track.chain.fxChainElements, site.chainId);
    }
    return nullptr;
}

TrackInfo* findTrack(Project& project, TrackId trackId) {
    for (auto& track : project.tracks)
        if (track.id == trackId)
            return &track;
    return nullptr;
}

int trackIndex(const Project& project, TrackId trackId) {
    for (std::size_t i = 0; i < project.tracks.size(); ++i)
        if (project.tracks[i].id == trackId)
            return static_cast<int>(i);
    return -1;
}

std::size_t clampPosition(int position, std::size_t size) {
    if (position <= 0)
        return 0;
    return std::min(static_cast<std::size_t>(position), size);
}

// --- what a track holds ------------------------------------------------------

/// Every insert-chain device of a track, nested racks included.
void collectDevices(const std::vector<ChainElement>& elements,
                    std::vector<const DeviceInfo*>& out) {
    for (const auto& element : elements) {
        if (isDevice(element)) {
            out.push_back(&getDevice(element));
            continue;
        }
        for (const auto& chain : getRack(element).chains)
            collectDevices(chain.elements, out);
    }
}

std::vector<const DeviceInfo*> devicesOf(const TrackInfo& track) {
    std::vector<const DeviceInfo*> devices;
    collectDevices(track.chain.fxChainElements, devices);
    return devices;
}

/// Device ids in the insert chain go into one id space, and each flat section
/// into its own. Latency, and therefore the store's identity for an instance,
/// follows the same split.
void collectKeys(const std::vector<ChainElement>& elements, std::set<engine::DeviceKey>& out) {
    for (const auto& element : elements) {
        if (isDevice(element)) {
            out.insert(engine::DeviceKey{ChainSegment::Fx, getDevice(element).id});
            continue;
        }
        for (const auto& chain : getRack(element).chains)
            collectKeys(chain.elements, out);
    }
}

/// A device the harness adds to every track, at the end of it, where what it
/// sees is what the track contributes to everything downstream.
DeviceInfo captureDevice(TrackId trackId) {
    DeviceInfo device;
    device.id = captureDeviceId(trackId);
    device.name = "Capture " + juce::String(trackId);
    device.deviceType = DeviceType::Analysis;
    return device;
}

DeviceInfo effect(DeviceId id) {
    DeviceInfo device;
    device.id = id;
    device.name = "Effect " + juce::String(id);
    device.deviceType = DeviceType::Effect;
    return device;
}

DeviceInfo instrument(DeviceId id) {
    DeviceInfo device;
    device.id = id;
    device.name = "Instrument " + juce::String(id);
    device.deviceType = DeviceType::Instrument;
    device.isInstrument = true;
    device.canReceiveMidi = true;
    return device;
}

TrackInfo audioTrack(TrackId id) {
    TrackInfo track;
    track.id = id;
    track.type = TrackType::Audio;
    track.name = "Track " + juce::String(id);
    track.audioOutputDevice = "master";
    track.chain.mixerAnalysisElements.push_back(PostFxChainElement{captureDevice(id)});
    return track;
}

/// Everything a removed track leaves behind: sends aimed at it, routes into it,
/// and sidechains reading it. A project that kept them would compile with
/// diagnostics, and the runner treats a diagnostic as a generator that has
/// drifted rather than as coverage.
void forgetTrack(Project& project, TrackId gone) {
    const auto clearSidechains = [gone](std::vector<ChainElement>& elements,
                                        const auto& self) -> void {
        for (auto& element : elements) {
            if (isDevice(element)) {
                auto& device = getDevice(element);
                if (device.sidechain.sourceTrackId == gone)
                    device.sidechain = {};
                continue;
            }
            for (auto& chain : getRack(element).chains)
                self(chain.elements, self);
        }
    };

    for (auto& track : project.tracks) {
        std::erase_if(track.sends,
                      [gone](const SendInfo& send) { return send.destTrackId == gone; });

        if (const auto route = engine::parseTrackRoute(track.audioOutputDevice);
            route.namesTrack() && route.trackId == gone)
            track.audioOutputDevice = "master";

        clearSidechains(track.chain.fxChainElements, clearSidechains);
        for (auto* section : {&track.chain.postFxChainElements, &track.chain.mixerAnalysisElements})
            for (auto& element : *section)
                if (element.device.sidechain.sourceTrackId == gone)
                    element.device.sidechain = {};
    }
}

/// Latency is remembered per instance, so a device leaving the model takes its
/// entry with it. Otherwise a device id reused later would arrive latent.
void forgetLatency(Project& project, const std::vector<ChainElement>& elements) {
    std::set<engine::DeviceKey> keys;
    collectKeys(elements, keys);
    for (const auto& key : keys)
        project.deviceLatency.erase(key);
}

// --- the generator -----------------------------------------------------------

/// splitmix64, written out rather than taken from <random>: the standard
/// distributions are implementation defined, and a seed that reproduces a
/// failure on one machine and not on another is not a recorded seed.
class Rng {
  public:
    explicit Rng(std::uint64_t seed) : state_(seed) {}

    std::uint64_t next() {
        state_ += 0x9e3779b97f4a7c15ULL;
        auto z = state_;
        z = (z ^ (z >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27U)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31U);
    }

    int below(int bound) {
        return bound <= 0 ? 0 : static_cast<int>(next() % static_cast<std::uint64_t>(bound));
    }

    bool chance(int percent) {
        return below(100) < percent;
    }

    template <typename T> const T& pick(const std::vector<T>& values) {
        return values[static_cast<std::size_t>(below(static_cast<int>(values.size())))];
    }

  private:
    std::uint64_t state_;
};

/// Every place an element can be added: each track's insert chain, and every
/// chain of every rack in it.
void collectSites(const std::vector<ChainElement>& elements, TrackId trackId,
                  std::vector<Site>& out) {
    for (const auto& element : elements) {
        if (!isRack(element))
            continue;
        const auto& rack = getRack(element);
        for (const auto& chain : rack.chains) {
            out.push_back(Site{trackId, rack.id, chain.id});
            collectSites(chain.elements, trackId, out);
        }
    }
}

std::vector<Site> sitesOf(const Project& project) {
    std::vector<Site> sites;
    for (const auto& track : project.tracks) {
        sites.push_back(Site{track.id, INVALID_RACK_ID, INVALID_CHAIN_ID});
        collectSites(track.chain.fxChainElements, track.id, sites);
    }
    return sites;
}

struct Inventory {
    std::vector<std::pair<DeviceId, TrackId>> devices;
    std::vector<RackId> racks;
    std::vector<ChainId> chains;
};

void collectInventory(const std::vector<ChainElement>& elements, TrackId trackId,
                      Inventory& inventory) {
    for (const auto& element : elements) {
        if (isDevice(element)) {
            inventory.devices.emplace_back(getDevice(element).id, trackId);
            continue;
        }
        const auto& rack = getRack(element);
        inventory.racks.push_back(rack.id);
        for (const auto& chain : rack.chains) {
            inventory.chains.push_back(chain.id);
            collectInventory(chain.elements, trackId, inventory);
        }
    }
}

Inventory inventoryOf(const Project& project) {
    Inventory inventory;
    for (const auto& track : project.tracks)
        collectInventory(track.chain.fxChainElements, track.id, inventory);
    return inventory;
}

/// Where the tracks a project starts with sit in project order, spaced so a
/// generated track can land before, between or after them.
constexpr int kBaseTrackOrder = 100;

/// One past the largest sort key a generated track may take.
constexpr int kMaxTrackOrder = kBaseTrackOrder * (kNumBaseTracks + 1);

/// Kept small on purpose. The properties are about what an edit does to the
/// plan around it, and a project with sixty devices makes a shrunk case that
/// takes longer to read than the code it is about.
constexpr int kMaxDevices = 18;
constexpr int kMaxRacks = 4;
constexpr int kMaxSendsPerTrack = 3;

/// Latencies a generated device reports. Zero most of the time, because a plan
/// with a delay on every edge is not the common shape, and never so large that
/// a render property would need a longer settling window than the block budget.
int pickLatency(Rng& rng) {
    if (rng.chance(60))
        return 0;
    return 1 + rng.below(kMaxDeviceLatency);
}

}  // namespace

// --- text --------------------------------------------------------------------

std::string toString(const Edit& edit) {
    std::ostringstream out;

    const auto site = [&edit] {
        std::ostringstream text;
        text << "T" << edit.site.trackId;
        if (edit.site.chainId != INVALID_CHAIN_ID)
            text << "/R" << edit.site.rackId << "/C" << edit.site.chainId;
        return text.str();
    };

    switch (edit.kind) {
        case EditKind::AddDevice:
            out << "addDevice " << site() << " D" << edit.deviceId << " at " << edit.position
                << " latency " << edit.latencySamples;
            break;
        case EditKind::AddInstrument:
            out << "addInstrument " << site() << " D" << edit.deviceId << " at " << edit.position;
            break;
        case EditKind::RemoveDevice:
            out << "removeDevice D" << edit.deviceId;
            break;
        case EditKind::MoveDevice:
            out << "moveDevice D" << edit.deviceId << " to " << edit.position;
            break;
        case EditKind::MoveDeviceToSite:
            out << "moveDeviceToSite D" << edit.deviceId << " into " << site() << " at "
                << edit.position;
            break;
        case EditKind::WrapDeviceInRack:
            out << "wrapDeviceInRack D" << edit.deviceId << " in R" << edit.rackId << "/C"
                << edit.chainId;
            break;
        case EditKind::BypassDevice:
            out << "bypassDevice D" << edit.deviceId << " " << (edit.flag ? "on" : "off");
            break;
        case EditKind::DeltaSoloDevice:
            out << "deltaSoloDevice D" << edit.deviceId << " " << (edit.flag ? "on" : "off");
            break;
        case EditKind::SetDeviceLatency:
            out << "setDeviceLatency D" << edit.deviceId << " " << edit.latencySamples;
            break;
        case EditKind::AddRack:
            out << "addRack " << site() << " R" << edit.rackId << " chains C" << edit.chainId
                << ",C" << (edit.chainId + 1) << " devices D" << edit.deviceId << ",D"
                << (edit.deviceId + 1) << " at " << edit.position;
            break;
        case EditKind::RemoveRack:
            out << "removeRack R" << edit.rackId;
            break;
        case EditKind::AddChain:
            out << "addChain R" << edit.rackId << " C" << edit.chainId << " device D"
                << edit.deviceId;
            break;
        case EditKind::RemoveChain:
            out << "removeChain C" << edit.chainId;
            break;
        case EditKind::ChainPower:
            out << "chainPower T" << edit.site.trackId << " " << (edit.flag ? "on" : "off");
            break;
        case EditKind::AddSend:
            out << "addSend T" << edit.site.trackId << " to T" << edit.otherTrackId << " "
                << (edit.flag ? "pre" : "post");
            break;
        case EditKind::RemoveSend:
            out << "removeSend T" << edit.site.trackId << " slot " << edit.position;
            break;
        case EditKind::SetSidechain:
            out << "setSidechain D" << edit.deviceId << " from ";
            if (edit.otherTrackId == INVALID_TRACK_ID)
                out << "none";
            else
                out << "T" << edit.otherTrackId;
            break;
        case EditKind::AddTrack:
            out << "addTrack T" << edit.site.trackId << " order " << edit.position;
            break;
        case EditKind::RemoveTrack:
            out << "removeTrack T" << edit.site.trackId;
            break;
        case EditKind::RouteTrack:
            out << "routeTrack T" << edit.site.trackId << " to ";
            if (edit.otherTrackId == INVALID_TRACK_ID)
                out << "master";
            else
                out << "T" << edit.otherTrackId;
            break;
    }

    return out.str();
}

std::string toString(const std::vector<Edit>& edits) {
    std::ostringstream out;
    for (std::size_t i = 0; i < edits.size(); ++i)
        out << "  [" << (i + 1) << "] " << toString(edits[i]) << "\n";
    return out.str();
}

// --- the project -------------------------------------------------------------

Project startingProject() {
    Project project;
    for (int i = 0; i < kNumBaseTracks; ++i) {
        project.tracks.push_back(audioTrack(kFirstBaseTrack + i));
        project.trackOrder[kFirstBaseTrack + i] = kBaseTrackOrder * (i + 1);
    }

    project.master = audioTrack(MASTER_TRACK_ID);
    project.master.type = TrackType::Master;
    project.master.audioOutputDevice = {};
    project.master.name = "Master";
    return project;
}

bool apply(const Edit& edit, Project& project) {
    switch (edit.kind) {
        case EditKind::AddDevice:
        case EditKind::AddInstrument: {
            DeviceAt existing;
            if (findDevice(project, edit.deviceId, existing))
                return false;

            auto* elements = siteElements(project, edit.site);
            if (elements == nullptr)
                return false;

            auto device = edit.kind == EditKind::AddInstrument ? instrument(edit.deviceId)
                                                               : effect(edit.deviceId);
            elements->insert(elements->begin() + static_cast<std::ptrdiff_t>(clampPosition(
                                                     edit.position, elements->size())),
                             makeDeviceElement(std::move(device)));

            if (edit.latencySamples > 0)
                project.deviceLatency[engine::DeviceKey{ChainSegment::Fx, edit.deviceId}] =
                    edit.latencySamples;
            return true;
        }

        case EditKind::RemoveDevice: {
            DeviceAt at;
            if (!findDevice(project, edit.deviceId, at))
                return false;

            at.container->erase(at.container->begin() + static_cast<std::ptrdiff_t>(at.index));
            project.deviceLatency.erase(engine::DeviceKey{ChainSegment::Fx, edit.deviceId});
            return true;
        }

        case EditKind::MoveDevice: {
            DeviceAt at;
            if (!findDevice(project, edit.deviceId, at))
                return false;

            auto element = std::move((*at.container)[at.index]);
            at.container->erase(at.container->begin() + static_cast<std::ptrdiff_t>(at.index));
            at.container->insert(at.container->begin() + static_cast<std::ptrdiff_t>(clampPosition(
                                                             edit.position, at.container->size())),
                                 std::move(element));
            return true;
        }

        case EditKind::MoveDeviceToSite: {
            DeviceAt at;
            if (!findDevice(project, edit.deviceId, at))
                return false;

            // Resolved before the device leaves, and safe to hold across the
            // erase: a chain's element list lives in a heap RackInfo, so moving
            // the owning pointers around in the source list does not move it.
            auto* destination = siteElements(project, edit.site);
            if (destination == nullptr || destination == at.container)
                return false;

            auto element = std::move((*at.container)[at.index]);
            at.container->erase(at.container->begin() + static_cast<std::ptrdiff_t>(at.index));

            // A connection in a generated project only ever points earlier in
            // project order, which is what keeps any sequence from producing a
            // routing cycle. A device carries its sidechain with it, and a move
            // to another track can land it on the track it keys off or in front
            // of it; that sidechain goes the way a removed track's sends go.
            if (edit.site.trackId != at.trackId) {
                auto& sidechain = getDevice(element).sidechain;
                if (sidechain.isActive() && trackIndex(project, sidechain.sourceTrackId) >=
                                                trackIndex(project, edit.site.trackId))
                    sidechain = SidechainConfig{};
            }

            destination->insert(destination->begin() + static_cast<std::ptrdiff_t>(clampPosition(
                                                           edit.position, destination->size())),
                                std::move(element));
            return true;
        }

        case EditKind::WrapDeviceInRack: {
            RackAt existing;
            if (findRack(project, edit.rackId, existing))
                return false;

            DeviceAt at;
            if (!findDevice(project, edit.deviceId, at))
                return false;

            // In the slot it was standing in, which is what the model's own
            // wrapChainElementsInRack does to a selection of one: the device
            // ends up one level down and everything around it stays put.
            auto rack = std::make_unique<RackInfo>();
            rack->id = edit.rackId;
            rack->name = "Rack " + juce::String(edit.rackId);

            ChainInfo chain;
            chain.id = edit.chainId;
            chain.name = "Chain " + juce::String(chain.id);
            chain.elements.push_back(std::move((*at.container)[at.index]));
            rack->chains.push_back(std::move(chain));

            (*at.container)[at.index] = ChainElement{std::move(rack)};
            return true;
        }

        case EditKind::BypassDevice: {
            DeviceAt at;
            if (!findDevice(project, edit.deviceId, at))
                return false;

            auto& device = getDevice((*at.container)[at.index]);
            if (device.bypassed == edit.flag)
                return false;
            device.bypassed = edit.flag;
            return true;
        }

        case EditKind::DeltaSoloDevice: {
            DeviceAt at;
            if (!findDevice(project, edit.deviceId, at))
                return false;

            auto& device = getDevice((*at.container)[at.index]);
            if (device.deltaSolo == edit.flag)
                return false;
            device.deltaSolo = edit.flag;
            return true;
        }

        case EditKind::SetDeviceLatency: {
            DeviceAt at;
            if (!findDevice(project, edit.deviceId, at))
                return false;

            const engine::DeviceKey key{ChainSegment::Fx, edit.deviceId};
            if (edit.latencySamples <= 0)
                project.deviceLatency.erase(key);
            else
                project.deviceLatency[key] = edit.latencySamples;
            return true;
        }

        case EditKind::AddRack: {
            RackAt existing;
            if (findRack(project, edit.rackId, existing))
                return false;

            auto* elements = siteElements(project, edit.site);
            if (elements == nullptr)
                return false;

            auto rack = std::make_unique<RackInfo>();
            rack->id = edit.rackId;
            rack->name = "Rack " + juce::String(edit.rackId);
            for (int i = 0; i < 2; ++i) {
                ChainInfo chain;
                chain.id = edit.chainId + i;
                chain.name = "Chain " + juce::String(chain.id);
                chain.elements.push_back(makeDeviceElement(effect(edit.deviceId + i)));
                rack->chains.push_back(std::move(chain));
            }

            elements->insert(elements->begin() + static_cast<std::ptrdiff_t>(clampPosition(
                                                     edit.position, elements->size())),
                             ChainElement{std::move(rack)});
            return true;
        }

        case EditKind::RemoveRack: {
            RackAt at;
            if (!findRack(project, edit.rackId, at))
                return false;

            for (const auto& chain : getRack((*at.container)[at.index]).chains)
                forgetLatency(project, chain.elements);

            at.container->erase(at.container->begin() + static_cast<std::ptrdiff_t>(at.index));
            return true;
        }

        case EditKind::AddChain: {
            ChainAt existing;
            if (findChain(project, edit.chainId, existing))
                return false;

            RackAt at;
            if (!findRack(project, edit.rackId, at))
                return false;

            DeviceAt device;
            if (findDevice(project, edit.deviceId, device))
                return false;

            ChainInfo chain;
            chain.id = edit.chainId;
            chain.name = "Chain " + juce::String(chain.id);
            chain.elements.push_back(makeDeviceElement(effect(edit.deviceId)));
            getRack((*at.container)[at.index]).chains.push_back(std::move(chain));
            return true;
        }

        case EditKind::RemoveChain: {
            ChainAt at;
            if (!findChain(project, edit.chainId, at))
                return false;

            // A rack with no chains is a rack the compiler reports rather than
            // compiles, and it is not what removing a chain in the app does.
            if (at.rack->chains.size() <= 1)
                return false;

            forgetLatency(project, at.rack->chains[at.index].elements);
            at.rack->chains.erase(at.rack->chains.begin() + static_cast<std::ptrdiff_t>(at.index));
            return true;
        }

        case EditKind::ChainPower: {
            auto* track = findTrack(project, edit.site.trackId);
            if (track == nullptr || track->chain.enabled == edit.flag)
                return false;
            track->chain.enabled = edit.flag;
            return true;
        }

        case EditKind::AddSend: {
            auto* track = findTrack(project, edit.site.trackId);
            if (track == nullptr || findTrack(project, edit.otherTrackId) == nullptr)
                return false;

            // Later in project order, always: the generator never builds a
            // routing cycle, so a cycle the compiler broke can never be
            // mistaken for a property the differ got wrong.
            if (trackIndex(project, edit.otherTrackId) <= trackIndex(project, edit.site.trackId))
                return false;

            SendInfo send;
            send.busIndex = 0;
            send.level = 0.5f;
            send.preFader = edit.flag;
            send.destTrackId = edit.otherTrackId;
            track->sends.push_back(send);
            return true;
        }

        case EditKind::RemoveSend: {
            auto* track = findTrack(project, edit.site.trackId);
            if (track == nullptr || edit.position < 0 ||
                static_cast<std::size_t>(edit.position) >= track->sends.size())
                return false;

            track->sends.erase(track->sends.begin() + edit.position);
            return true;
        }

        case EditKind::SetSidechain: {
            DeviceAt at;
            if (!findDevice(project, edit.deviceId, at))
                return false;

            auto& device = getDevice((*at.container)[at.index]);
            if (edit.otherTrackId == INVALID_TRACK_ID) {
                if (!device.sidechain.isActive())
                    return false;
                device.sidechain = {};
                return true;
            }

            if (findTrack(project, edit.otherTrackId) == nullptr)
                return false;
            if (trackIndex(project, edit.otherTrackId) >= trackIndex(project, at.trackId))
                return false;

            device.sidechain.type = SidechainConfig::Type::Audio;
            device.sidechain.sourceTrackId = edit.otherTrackId;
            return true;
        }

        case EditKind::AddTrack: {
            if (findTrack(project, edit.site.trackId) != nullptr)
                return false;

            // By sort key, so that where this track lands depends on the tracks
            // that are there and not on which edits ran. A position taken as an
            // index would put it before a track in one run and after it in
            // another, which changes both what may connect to what and the
            // order a fan-in sums.
            const auto key = std::clamp(edit.position, 0, kMaxTrackOrder);
            project.trackOrder[edit.site.trackId] = key;

            auto at = project.tracks.begin();
            while (at != project.tracks.end() && std::pair(project.trackOrder[at->id], at->id) <
                                                     std::pair(key, edit.site.trackId))
                ++at;

            project.tracks.insert(at, audioTrack(edit.site.trackId));
            return true;
        }

        case EditKind::RemoveTrack: {
            if (edit.site.trackId < kFirstExtraTrack)
                return false;

            const auto found =
                std::ranges::find_if(project.tracks, [&edit](const TrackInfo& track) {
                    return track.id == edit.site.trackId;
                });
            if (found == project.tracks.end())
                return false;

            forgetLatency(project, found->chain.fxChainElements);
            project.tracks.erase(found);
            project.trackOrder.erase(edit.site.trackId);
            forgetTrack(project, edit.site.trackId);
            return true;
        }

        case EditKind::RouteTrack: {
            auto* track = findTrack(project, edit.site.trackId);
            if (track == nullptr)
                return false;

            if (edit.otherTrackId == INVALID_TRACK_ID) {
                if (track->audioOutputDevice == "master")
                    return false;
                track->audioOutputDevice = "master";
                return true;
            }

            if (findTrack(project, edit.otherTrackId) == nullptr)
                return false;
            if (trackIndex(project, edit.otherTrackId) <= trackIndex(project, edit.site.trackId))
                return false;

            track->audioOutputDevice = "track:" + juce::String(edit.otherTrackId);
            return true;
        }
    }

    return false;
}

// --- what an edit touches ----------------------------------------------------

std::set<TrackId> touched(const Edit& edit, const Project& before) {
    auto project = before;  // the lookups below are non-const; nothing is kept
    std::set<TrackId> tracks;

    const auto ownerOfDevice = [&project](DeviceId deviceId) {
        DeviceAt at;
        return findDevice(project, deviceId, at) ? at.trackId : INVALID_TRACK_ID;
    };

    const auto add = [&tracks](TrackId trackId) {
        if (trackId != INVALID_TRACK_ID)
            tracks.insert(trackId);
    };

    switch (edit.kind) {
        case EditKind::AddDevice:
        case EditKind::AddInstrument:
        case EditKind::AddRack:
        case EditKind::ChainPower:
        case EditKind::AddTrack:
        case EditKind::RemoveTrack:
            add(edit.site.trackId);
            break;

        case EditKind::RemoveDevice:
        case EditKind::MoveDevice:
        case EditKind::WrapDeviceInRack:
        case EditKind::BypassDevice:
        case EditKind::DeltaSoloDevice:
        case EditKind::SetDeviceLatency:
            add(ownerOfDevice(edit.deviceId));
            break;

        // Both ends, because the device leaves one track's plan and arrives in
        // another's. Read off the project as it stands, which is where the
        // device still is.
        case EditKind::MoveDeviceToSite:
            add(ownerOfDevice(edit.deviceId));
            add(edit.site.trackId);
            break;

        case EditKind::RemoveRack: {
            RackAt at;
            if (findRack(project, edit.rackId, at))
                add(at.trackId);
            break;
        }

        case EditKind::AddChain: {
            RackAt at;
            if (findRack(project, edit.rackId, at))
                add(at.trackId);
            break;
        }

        case EditKind::RemoveChain: {
            ChainAt at;
            if (findChain(project, edit.chainId, at))
                add(at.trackId);
            break;
        }

        case EditKind::AddSend:
            add(edit.site.trackId);
            add(edit.otherTrackId);
            break;

        case EditKind::RemoveSend: {
            add(edit.site.trackId);
            if (const auto* track = findTrack(project, edit.site.trackId);
                track != nullptr && edit.position >= 0 &&
                static_cast<std::size_t>(edit.position) < track->sends.size())
                add(track->sends[static_cast<std::size_t>(edit.position)].destTrackId);
            break;
        }

        case EditKind::SetSidechain:
            add(ownerOfDevice(edit.deviceId));
            add(edit.otherTrackId);
            break;

        case EditKind::RouteTrack: {
            add(edit.site.trackId);
            add(edit.otherTrackId);
            if (const auto* track = findTrack(project, edit.site.trackId); track != nullptr)
                if (const auto route = engine::parseTrackRoute(track->audioOutputDevice);
                    route.namesTrack())
                    add(route.trackId);
            break;
        }
    }

    return tracks;
}

std::set<TrackId> feeds(const Project& project, TrackId target) {
    std::set<TrackId> reaching{target};

    // Fixed point rather than one pass: a send into a track that is itself
    // routed into the target is upstream of the target too, and the generator
    // builds those chains freely.
    for (bool grew = true; grew;) {
        grew = false;

        for (const auto& track : project.tracks) {
            const auto reaches = [&reaching](TrackId trackId) {
                return trackId != INVALID_TRACK_ID && reaching.contains(trackId);
            };

            auto upstream = false;
            for (const auto& send : track.sends)
                upstream = upstream || reaches(send.destTrackId);

            if (const auto route = engine::parseTrackRoute(track.audioOutputDevice);
                route.namesTrack())
                upstream = upstream || reaches(route.trackId);
            else if (reaching.contains(MASTER_TRACK_ID))
                upstream = true;

            if (upstream && reaching.insert(track.id).second)
                grew = true;
        }

        // A sidechain reads another track into this one, so its source is
        // upstream of everything this track feeds.
        for (const auto& track : project.tracks) {
            if (!reaching.contains(track.id))
                continue;

            std::vector<const DeviceInfo*> devices = devicesOf(track);
            for (const auto* section :
                 {&track.chain.postFxChainElements, &track.chain.mixerAnalysisElements})
                for (const auto& element : *section)
                    devices.push_back(&element.device);

            for (const auto* device : devices)
                if (device->sidechain.isActive() &&
                    reaching.insert(device->sidechain.sourceTrackId).second)
                    grew = true;
        }
    }

    return reaching;
}

std::set<engine::DeviceKey> deviceKeys(const Project& project) {
    std::set<engine::DeviceKey> keys;

    const auto collectTrack = [&keys](const TrackInfo& track) {
        collectKeys(track.chain.fxChainElements, keys);
        for (const auto& element : track.chain.postFxChainElements)
            keys.insert(engine::DeviceKey{ChainSegment::PostFx, element.device.id});
        for (const auto& element : track.chain.mixerAnalysisElements)
            keys.insert(engine::DeviceKey{ChainSegment::MixerAnalysis, element.device.id});
    };

    for (const auto& track : project.tracks)
        collectTrack(track);
    collectTrack(project.master);
    return keys;
}

// --- the generator -----------------------------------------------------------

std::vector<Edit> generate(std::uint64_t seed, int length) {
    Rng rng(seed);

    auto project = startingProject();
    std::vector<Edit> edits;
    edits.reserve(static_cast<std::size_t>(length));

    DeviceId nextDevice = 10;
    RackId nextRack = 100;
    ChainId nextChain = 200;

    const auto trackIds = [&project] {
        std::vector<TrackId> ids;
        for (const auto& track : project.tracks)
            ids.push_back(track.id);
        return ids;
    };

    for (int step = 0; step < length; ++step) {
        const auto sites = sitesOf(project);
        const auto inventory = inventoryOf(project);
        const auto ids = trackIds();

        // Chosen against the project as it stands, then written down by id. A
        // kind with nothing to work on is retried rather than emitted, so a
        // sequence is mostly edits that did something even before it is shrunk.
        Edit edit;
        auto usable = false;

        for (int attempt = 0; attempt < 12 && !usable; ++attempt) {
            edit = Edit{};
            const auto kind = static_cast<EditKind>(rng.below(kNumEditKinds));
            edit.kind = kind;

            switch (kind) {
                case EditKind::AddDevice:
                case EditKind::AddInstrument:
                    if (static_cast<int>(inventory.devices.size()) >= kMaxDevices || sites.empty())
                        break;
                    edit.site = rng.pick(sites);
                    edit.deviceId = nextDevice;
                    edit.position = rng.below(6);
                    edit.latencySamples = kind == EditKind::AddDevice ? pickLatency(rng) : 0;
                    usable = true;
                    break;

                case EditKind::MoveDeviceToSite:
                    if (inventory.devices.empty() || sites.empty())
                        break;
                    edit.deviceId = rng.pick(inventory.devices).first;
                    edit.site = rng.pick(sites);
                    edit.position = rng.below(6);
                    usable = true;
                    break;

                case EditKind::WrapDeviceInRack:
                    if (inventory.devices.empty() ||
                        static_cast<int>(inventory.racks.size()) >= kMaxRacks)
                        break;
                    edit.deviceId = rng.pick(inventory.devices).first;
                    edit.rackId = nextRack;
                    edit.chainId = nextChain;
                    usable = true;
                    break;

                case EditKind::RemoveDevice:
                case EditKind::MoveDevice:
                case EditKind::BypassDevice:
                case EditKind::DeltaSoloDevice:
                case EditKind::SetDeviceLatency:
                case EditKind::SetSidechain: {
                    if (inventory.devices.empty())
                        break;
                    const auto& [deviceId, trackId] = rng.pick(inventory.devices);
                    edit.deviceId = deviceId;
                    edit.site = Site{trackId, INVALID_RACK_ID, INVALID_CHAIN_ID};
                    edit.position = rng.below(6);
                    edit.flag = rng.chance(50);
                    edit.latencySamples = pickLatency(rng);
                    if (kind == EditKind::SetSidechain) {
                        const auto index = trackIndex(project, trackId);
                        edit.otherTrackId =
                            index > 0 && rng.chance(70)
                                ? project.tracks[static_cast<std::size_t>(rng.below(index))].id
                                : INVALID_TRACK_ID;
                    }
                    usable = true;
                    break;
                }

                case EditKind::AddRack:
                    if (static_cast<int>(inventory.racks.size()) >= kMaxRacks || sites.empty())
                        break;
                    edit.site = rng.pick(sites);
                    edit.rackId = nextRack;
                    edit.chainId = nextChain;
                    edit.deviceId = nextDevice;
                    edit.position = rng.below(4);
                    usable = true;
                    break;

                case EditKind::RemoveRack:
                    if (inventory.racks.empty())
                        break;
                    edit.rackId = rng.pick(inventory.racks);
                    usable = true;
                    break;

                case EditKind::AddChain:
                    if (inventory.racks.empty())
                        break;
                    edit.rackId = rng.pick(inventory.racks);
                    edit.chainId = nextChain;
                    edit.deviceId = nextDevice;
                    usable = true;
                    break;

                case EditKind::RemoveChain:
                    if (inventory.chains.empty())
                        break;
                    edit.chainId = rng.pick(inventory.chains);
                    usable = true;
                    break;

                case EditKind::ChainPower:
                    edit.site = Site{rng.pick(ids), INVALID_RACK_ID, INVALID_CHAIN_ID};
                    edit.flag = rng.chance(40);
                    usable = true;
                    break;

                case EditKind::AddSend: {
                    if (project.tracks.size() < 2)
                        break;
                    const auto from = rng.below(static_cast<int>(project.tracks.size()) - 1);
                    const auto to =
                        from + 1 + rng.below(static_cast<int>(project.tracks.size()) - from - 1);
                    if (project.tracks[static_cast<std::size_t>(from)].sends.size() >=
                        static_cast<std::size_t>(kMaxSendsPerTrack))
                        break;
                    edit.site = Site{project.tracks[static_cast<std::size_t>(from)].id};
                    edit.otherTrackId = project.tracks[static_cast<std::size_t>(to)].id;
                    edit.flag = rng.chance(40);
                    usable = true;
                    break;
                }

                case EditKind::RemoveSend: {
                    std::vector<TrackId> withSends;
                    for (const auto& track : project.tracks)
                        if (!track.sends.empty())
                            withSends.push_back(track.id);
                    if (withSends.empty())
                        break;
                    edit.site = Site{rng.pick(withSends)};
                    edit.position = rng.below(kMaxSendsPerTrack);
                    usable = true;
                    break;
                }

                case EditKind::AddTrack: {
                    std::vector<TrackId> missing;
                    for (int i = 0; i < kNumExtraTracks; ++i)
                        if (std::ranges::none_of(project.tracks, [i](const TrackInfo& track) {
                                return track.id == kFirstExtraTrack + i;
                            }))
                            missing.push_back(kFirstExtraTrack + i);
                    if (missing.empty())
                        break;
                    edit.site = Site{rng.pick(missing)};
                    edit.position = rng.below(kMaxTrackOrder);
                    usable = true;
                    break;
                }

                case EditKind::RemoveTrack: {
                    std::vector<TrackId> removable;
                    for (const auto& track : project.tracks)
                        if (track.id >= kFirstExtraTrack)
                            removable.push_back(track.id);
                    if (removable.empty())
                        break;
                    edit.site = Site{rng.pick(removable)};
                    usable = true;
                    break;
                }

                case EditKind::RouteTrack: {
                    if (project.tracks.size() < 2)
                        break;
                    const auto from = rng.below(static_cast<int>(project.tracks.size()) - 1);
                    edit.site = Site{project.tracks[static_cast<std::size_t>(from)].id};
                    if (rng.chance(35)) {
                        edit.otherTrackId = INVALID_TRACK_ID;
                    } else {
                        const auto to =
                            from + 1 +
                            rng.below(static_cast<int>(project.tracks.size()) - from - 1);
                        edit.otherTrackId = project.tracks[static_cast<std::size_t>(to)].id;
                    }
                    usable = true;
                    break;
                }
            }
        }

        if (!usable)
            continue;

        // The ids an edit brings with it are consumed whether or not the edit
        // lands, so that no two edits in one sequence can name the same new
        // device: a shrunk sequence would otherwise depend on which of them
        // survived.
        switch (edit.kind) {
            case EditKind::AddDevice:
            case EditKind::AddInstrument:
                ++nextDevice;
                break;
            case EditKind::AddRack:
                ++nextRack;
                nextChain += 2;
                nextDevice += 2;
                break;
            case EditKind::AddChain:
                ++nextChain;
                ++nextDevice;
                break;
            case EditKind::WrapDeviceInRack:
                ++nextRack;
                ++nextChain;
                break;
            default:
                break;
        }

        apply(edit, project);
        edits.push_back(edit);
    }

    return edits;
}

}  // namespace magda::edits
