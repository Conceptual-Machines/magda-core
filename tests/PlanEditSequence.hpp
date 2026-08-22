#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/TrackInfo.hpp"
#include "plan/RenderPlan.hpp"

/**
 * @file PlanEditSequence.hpp
 * @brief Random structural edit sequences over a project (#2077).
 *
 * The differ and the crossfade pass are tested elsewhere by pairs of plans
 * somebody sat down and wrote, which means they are tested against one reading
 * of the compatibility matrix. This is the other half: a project, a vocabulary
 * of edits a user can perform on it, and a generator that strings them together
 * so the properties are asserted over pairs nobody thought of.
 *
 * Two decisions here are what make the failures usable.
 *
 * **An edit names what it touches, never where it sits.** A device is addressed
 * by its id and a rack by its own, so an edit applies to whatever the project
 * has become, and one that finds nothing simply does nothing. That is what lets
 * a failing sequence be shrunk: any subsequence of it is still a sequence that
 * runs, so the shrinker can delete an edit without rewriting the ones after it.
 * Addressing by index would make every deletion invalidate its successors and
 * the minimal case would be whatever survived the rewriting.
 *
 * **The generator refuses to build a project the compiler cannot express.**
 * Sends and routes only ever point later in project order, so no sequence
 * produces a routing cycle, and removing a track takes the sends and routes
 * that named it with it. The runner asserts the compiler emitted no diagnostics,
 * which turns a generator that has drifted into a test failure rather than into
 * a corpus quietly exercising the diagnostic path.
 *
 * Nothing here compiles, renders or asserts. It is model values and the edits
 * between them, so the properties and the shrinker share one definition of what
 * a sequence is.
 */

namespace magda::edits {

/// Tracks the generator never removes, so a property has something to follow
/// from the first edit to the last.
constexpr TrackId kFirstBaseTrack = 1;
constexpr int kNumBaseTracks = 3;

/// Tracks the generator may add and remove.
constexpr TrackId kFirstExtraTrack = kFirstBaseTrack + kNumBaseTracks;
constexpr int kNumExtraTracks = 3;

/**
 * @brief The most a generated device may claim to delay its output by.
 *
 * Here rather than beside the device that honours it, because it is the one
 * number both ends have to agree on and they are in different layers: the
 * generator writes it into an edit, and whatever plays that edit has to be able
 * to delay by it. A device that reported more than it delayed would leave the
 * plan compensating for samples the signal never spent, which is a graph that
 * is misaligned in fact while every latency assertion about it passes.
 *
 * It also bounds how long a render has to run before the last delay line has
 * filled, which is what lets both legs of a comparison render the same fixed
 * number of blocks. Raising it means raising that window too.
 */
constexpr int kMaxDeviceLatency = 32;

/**
 * @brief The analysis device every track carries, and the id it carries it under.
 *
 * The render properties need to hear one track on its own, and a track's own
 * output is not a port anything hands back: everything meets at the master, and
 * a residual in a sum says nothing about which term moved. So every track gets
 * a device in its mixer-analysis section, which is a transparent tap by
 * construction, and the harness binds it to something that records what passes
 * through. It sits at the very end of the track, after the fader and after both
 * flat sections, so what it records is what the track contributes.
 *
 * Ids are per section and unique across tracks, which is why this is a function
 * of the track rather than a constant: two tracks with the same analysis id
 * would be one runtime instance shared by both.
 */
constexpr DeviceId captureDeviceId(TrackId trackId) {
    return 900 + trackId;
}

/// Where a device or a rack can be added: a track's insert chain, or one chain
/// of a rack anywhere inside it.
struct Site {
    TrackId trackId = INVALID_TRACK_ID;
    RackId rackId = INVALID_RACK_ID;
    ChainId chainId = INVALID_CHAIN_ID;

    bool operator==(const Site& other) const {
        return trackId == other.trackId && rackId == other.rackId && chainId == other.chainId;
    }
};

/**
 * @brief The project an edit sequence runs over.
 *
 * Device latency is here rather than in the model because it is not a model
 * value: it is what a loaded instance reports, and changing it re-prepares the
 * plan rather than recompiling it. Keeping it beside the tracks is what lets one
 * sequence exercise both paths.
 */
struct Project {
    std::vector<TrackInfo> tracks;
    TrackInfo master;
    std::map<engine::DeviceKey, int> deviceLatency;

    /// Where each track sits in project order, as a sort key rather than as an
    /// index. Project order decides two things a property compares across runs:
    /// which direction a connection may point, and the order a fan-in sums its
    /// inputs. Both have to be a function of which tracks exist rather than of
    /// which edits ran, or a sequence with an unrelated track removed would put
    /// the remaining tracks in a different order and every comparison after it
    /// would be measuring that instead.
    std::map<TrackId, int> trackOrder;
};

/** What a single edit does. */
enum class EditKind : std::uint8_t {
    AddDevice,
    AddInstrument,
    RemoveDevice,
    MoveDevice,
    BypassDevice,
    DeltaSoloDevice,
    SetDeviceLatency,
    AddRack,
    RemoveRack,
    AddChain,
    RemoveChain,
    ChainPower,
    AddSend,
    RemoveSend,
    SetSidechain,
    AddTrack,
    RemoveTrack,
    RouteTrack,
};

/// One past the last edit kind. The generator draws from it and the coverage
/// test requires every one of them to appear, so a kind added here and nowhere
/// else is a test failure rather than a hole nobody notices.
constexpr int kNumEditKinds = static_cast<int>(EditKind::RouteTrack) + 1;

/**
 * @brief One structural edit, as a value.
 *
 * Flat and copyable so a sequence is a vector the shrinker can slice. Only the
 * fields an edit kind uses are set; the rest stay invalid, and toString prints
 * only what the kind reads so a printed case is unambiguous.
 */
struct Edit {
    EditKind kind = EditKind::AddDevice;

    Site site;

    /// The object the edit names, wherever the project keeps it.
    DeviceId deviceId = INVALID_DEVICE_ID;
    RackId rackId = INVALID_RACK_ID;
    ChainId chainId = INVALID_CHAIN_ID;

    /// The other end: a send's destination, a sidechain's source, a route's
    /// destination. INVALID_TRACK_ID means the master, or means none.
    TrackId otherTrackId = INVALID_TRACK_ID;

    /// Where in its container an added or moved element lands. Clamped on
    /// application, so a shrunk sequence never addresses past the end. For a
    /// track it is the sort key of Project::trackOrder rather than an index,
    /// because where a track lands has to depend on the tracks that exist and
    /// not on which other edits ran.
    int position = 0;

    /// Latency in samples, for SetDeviceLatency and for a device as it arrives.
    int latencySamples = 0;

    /// Bypass on or off, chain power on or off, a send pre or post fader.
    bool flag = false;
};

/// Canonical one-line text, so a failing case is something to paste back.
std::string toString(const Edit& edit);

/// A whole sequence, one edit per line, numbered from one.
std::string toString(const std::vector<Edit>& edits);

/**
 * @brief The project every sequence starts from.
 *
 * Three audio tracks into the master, each with its capture tap and nothing
 * else. Deliberately bare: what a sequence covers should come from the edits,
 * so that a shrunk case is the edits that mattered rather than a starting
 * project somebody chose.
 */
Project startingProject();

/**
 * @brief Apply @p edit to @p project.
 *
 * @return false when the edit found nothing to do, which is not a failure: a
 *         shrunk sequence is full of edits whose target was removed by the
 *         deletion that shrank it, and they are what a subsequence means.
 */
bool apply(const Edit& edit, Project& project);

/**
 * @brief A sequence of @p length edits from @p seed.
 *
 * Stateful: each edit is chosen against the project as it stands, so the
 * sequence is mostly edits that do something rather than mostly misses. The
 * edits it emits are still addressed by id, so the sequence survives being cut
 * up afterwards.
 */
std::vector<Edit> generate(std::uint64_t seed, int length);

/**
 * @brief The tracks whose plan @p edit can change.
 *
 * Both ends of a connection, and the track that owns the object an edit names,
 * read off @p before because that is where the object still is.
 */
std::set<TrackId> touched(const Edit& edit, const Project& before);

/**
 * @brief Every track whose signal reaches @p target, transitively.
 *
 * Sends into it, tracks routed into it, and the sources of any sidechain a
 * device on it reads. This is what "an unrelated edit" is measured against: an
 * edit on a track outside this set cannot change a sample of what @p target
 * produces, and the render properties are the assertion that it does not.
 */
std::set<TrackId> feeds(const Project& project, TrackId target);

/// Every device id in @p project, per section, which is what the runtime store
/// keys instances on.
std::set<engine::DeviceKey> deviceKeys(const Project& project);

}  // namespace magda::edits
