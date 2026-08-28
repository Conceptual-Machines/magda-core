#include <array>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "StructuralRoundTrip.hpp"
#include "magda/daw/core/DrumGridPads.hpp"
#include "magda/daw/core/PadCommands.hpp"
#include "magda/daw/core/RackInfo.hpp"
#include "magda/daw/core/TrackCommands.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

using namespace magda;
using magda::structural_test::modelDifference;
using magda::structural_test::NotificationCounter;
using magda::structural_test::resetState;
using magda::structural_test::snapshot;

// The structural transition matrix (#2229).
//
// The hand-written cases in test_structural_undo_roundtrip.cpp each record one
// bug that was found and fixed. This is the systematic other half: the same
// oracle, driven from a generated product of operation, container and subject,
// so a container shape nobody thought of is covered by construction rather than
// by having come up.
//
// Every cell asserts one of two outcomes. Either the operation is refused, and
// then the serialized model is byte-identical and nothing was announced; or it
// happened, and then undo restores the bytes it started from and redo
// reproduces the bytes it made. Combinations that cannot exist are excluded by
// name, with the reason, and the excluded set is reported so a shape that stops
// being generated shows up instead of quietly dropping out of the sweep.

namespace {

// ============================================================================
// Axes
// ============================================================================

/// Every place the model can hold a chain element.
enum class Container {
    TrackList,           ///< The track's own FX list
    RackChain,           ///< A chain of a rack on the track
    NestedRackChain,     ///< A chain of a rack inside a rack chain
    PadChain,            ///< A chain a Drum Grid owns
    PadNestedRackChain,  ///< A chain of a rack inside a pad chain
    PostFx,              ///< The post-fader list
    MixerAnalysis,       ///< The rail-managed mixer-analysis section
};

constexpr std::array kContainers{
    Container::TrackList,     Container::RackChain,          Container::NestedRackChain,
    Container::PadChain,      Container::PadNestedRackChain, Container::PostFx,
    Container::MixerAnalysis,
};

const char* label(Container container) {
    switch (container) {
        case Container::TrackList:
            return "track list";
        case Container::RackChain:
            return "rack chain";
        case Container::NestedRackChain:
            return "rack in a rack chain";
        case Container::PadChain:
            return "pad chain";
        case Container::PadNestedRackChain:
            return "rack in a pad chain";
        case Container::PostFx:
            return "post-fader list";
        case Container::MixerAnalysis:
            return "mixer-analysis section";
    }
    return "?";
}

/// True for the two flat sections, which hold bare devices in their own id
/// space rather than chain elements.
bool isFlatSection(Container container) {
    return container == Container::PostFx || container == Container::MixerAnalysis;
}

/// What is being moved, copied, wrapped or removed.
enum class Subject {
    Effect,
    Instrument,
    Rack,
    RoutedGrid,      ///< A Drum Grid with a pad on a multi-out bus
    MultiOutDriver,  ///< A device whose pair drives a child track
};

constexpr std::array kSubjects{
    Subject::Effect,     Subject::Instrument,     Subject::Rack,
    Subject::RoutedGrid, Subject::MultiOutDriver,
};

const char* label(Subject subject) {
    switch (subject) {
        case Subject::Effect:
            return "an effect";
        case Subject::Instrument:
            return "an instrument";
        case Subject::Rack:
            return "a rack";
        case Subject::RoutedGrid:
            return "a routed Drum Grid";
        case Subject::MultiOutDriver:
            return "a multi-out device driving a child track";
    }
    return "?";
}

/// Which track a destination container belongs to.
enum class TrackRole {
    Host,  ///< The track the subject starts on
    Peer,  ///< Another Media track
    Aux,   ///< A track that cannot host an instrument
};

constexpr std::array kRoles{TrackRole::Host, TrackRole::Peer, TrackRole::Aux};

const char* label(TrackRole role) {
    switch (role) {
        case TrackRole::Host:
            return "its own track";
        case TrackRole::Peer:
            return "another media track";
        case TrackRole::Aux:
            return "an aux track";
    }
    return "?";
}

// ============================================================================
// The ledger
// ============================================================================

/// What the sweep ran and what it left out.
///
/// The excluded cells are reported by name, so a shape that stops being
/// generated -- a builder that silently fails, a factory that starts returning
/// nothing -- is visible as a cell that moved from run to excluded rather than
/// as coverage that quietly went away.
struct Ledger {
    juce::String title;
    int ran = 0;
    /// Excluded cells, grouped by the reason they were excluded. Grouped rather
    /// than listed flat because one reason covers dozens of cells, and a report
    /// that repeats it dozens of times is one nobody reads.
    std::map<juce::String, std::vector<juce::String>> excluded;
    std::set<Container> sourcesCovered;
    std::set<Container> destinationsCovered;
    std::set<Subject> subjectsCovered;

    void exclude(const juce::String& cell, const juce::String& reason) {
        excluded[reason].push_back(cell);
    }

    int excludedCount() const {
        int total = 0;
        for (const auto& [reason, cells] : excluded)
            total += static_cast<int>(cells.size());
        return total;
    }

    juce::String report() const {
        juce::String out;
        out << "\n" << title << ": " << ran << " cells run, " << excludedCount() << " excluded\n";
        for (const auto& [reason, cells] : excluded) {
            out << "  " << static_cast<int>(cells.size()) << " excluded because " << reason
                << ":\n";
            for (const auto& cell : cells)
                out << "      " << cell << "\n";
        }
        return out;
    }
};

/// Every axis value has to survive into at least one cell that actually ran.
///
/// Without this the sweep degrades silently: a builder that stops producing a
/// pad chain turns every pad cell into an exclusion, the exclusions are printed
/// and nobody reads them, and the suite still passes with a hole in it.
void requireEveryAxisCovered(const Ledger& ledger, const std::vector<Subject>& expectedSubjects) {
    INFO(ledger.report());
    CHECK(ledger.ran > 0);
    for (auto subject : expectedSubjects) {
        INFO("subject: " << label(subject));
        CHECK(ledger.subjectsCovered.count(subject) == 1);
    }
    for (auto container : kContainers) {
        INFO("source container: " << label(container));
        CHECK(ledger.sourcesCovered.count(container) == 1);
        INFO("destination container: " << label(container));
        CHECK(ledger.destinationsCovered.count(container) == 1);
    }
}

std::vector<Subject> everySubject() {
    return {kSubjects.begin(), kSubjects.end()};
}

std::vector<Subject> everySubjectExcept(Subject omitted) {
    std::vector<Subject> subjects;
    for (auto subject : kSubjects)
        if (subject != omitted)
            subjects.push_back(subject);
    return subjects;
}

// ============================================================================
// Devices
// ============================================================================

DeviceInfo effect(const juce::String& name, const juce::String& pluginId = "delay") {
    DeviceInfo device;
    device.name = name;
    device.pluginId = pluginId;
    device.format = PluginFormat::Internal;
    return device;
}

/// The occupant every container starts with, so that index 0 is a real
/// placement rather than the subject's own index. Its own plugin id, because
/// the mixer-analysis section holds one device per plugin id on a track and a
/// resident sharing the subject's would turn every such cell into a refusal.
DeviceInfo resident() {
    return effect("Resident", "reverb");
}

DeviceInfo instrument(const juce::String& name) {
    DeviceInfo device;
    device.name = name;
    device.pluginId = "magdasampler";
    device.format = PluginFormat::Internal;
    device.isInstrument = true;
    device.deviceType = DeviceType::Instrument;
    return device;
}

DeviceInfo drumGrid(const juce::String& name) {
    DeviceInfo device;
    device.name = name;
    device.pluginId = "drumgrid";
    device.format = PluginFormat::Internal;
    device.isInstrument = true;
    device.deviceType = DeviceType::Instrument;
    return device;
}

DeviceInfo multiOutInstrument(const juce::String& name) {
    auto device = instrument(name);
    device.pluginId = "multisynth";
    device.multiOut.isMultiOut = true;
    device.multiOut.totalOutputChannels = 4;
    device.multiOut.outputPairs = {{0, "Main 1-2", 1, 2}, {1, "Out 3-4", 3, 2}};
    return device;
}

ChainElement rackHolding(const juce::String& name, DeviceInfo child) {
    RackInfo rack;
    rack.name = name;
    ChainInfo chain;
    chain.name = "Chain 1";
    chain.elements.push_back(makeDeviceElement(std::move(child)));
    rack.chains.push_back(std::move(chain));
    return makeRackElement(std::move(rack));
}

// ============================================================================
// Reading and filling containers
// ============================================================================

/// The elements a chain-shaped container holds, or null when the path does not
/// name one. The flat sections answer null: they hold their own element type.
const std::vector<ChainElement>* elementsOf(const ChainNodePath& container) {
    auto& tm = TrackManager::getInstance();
    if (container.trackId == INVALID_TRACK_ID)
        return nullptr;
    if (container.steps.empty()) {
        const auto* track = tm.getTrack(container.trackId);
        return track != nullptr ? &track->chain.fxChainElements : nullptr;
    }
    const auto* chain = tm.getChainByPath(container);
    return chain != nullptr ? &chain->elements : nullptr;
}

/// The path @p container gives an element it directly holds.
///
/// A track's own list is not a chain and its devices are addressed as top-level
/// ones, which is the same split every path-based command makes.
ChainNodePath pathOfChild(const ChainNodePath& container, const ChainElement& element) {
    const bool topLevel = container.steps.empty();
    if (isDevice(element)) {
        const auto id = getDevice(element).id;
        return topLevel ? ChainNodePath::topLevelDevice(container.trackId, id)
                        : container.withDevice(id);
    }
    const auto id = getRack(element).id;
    return topLevel ? ChainNodePath::rack(container.trackId, id) : container.withRack(id);
}

/// Put @p element at the end of @p container and answer with its path.
///
/// Goes in through the insert path rather than the add-by-path helpers because
/// that is the one route every chain shape answers to: `addDeviceToChainByPath`
/// and `addRackToChainByPath` resolve their parent through the rack walk, which
/// no pad address answers to.
ChainNodePath append(const ChainNodePath& container, ChainElement element) {
    auto& tm = TrackManager::getInstance();
    const auto* before = elementsOf(container);
    REQUIRE(before != nullptr);
    const int index = static_cast<int>(before->size());

    std::vector<ChainElement> batch;
    batch.push_back(std::move(element));
    REQUIRE(tm.insertChainElementsByPath(container, std::move(batch), index, /*reassignIds=*/true));

    const auto* after = elementsOf(container);
    REQUIRE(after != nullptr);
    REQUIRE(static_cast<int>(after->size()) == index + 1);
    return pathOfChild(container, (*after)[static_cast<size_t>(index)]);
}

ChainNodePath segmentContainer(TrackId trackId, ChainSegment segment) {
    ChainNodePath path;
    path.trackId = trackId;
    path.steps.push_back({ChainStepType::Segment, static_cast<int>(segment)});
    return path;
}

// ============================================================================
// The world
// ============================================================================

/// One track carrying every container shape it can carry.
struct Shapes {
    TrackId id = INVALID_TRACK_ID;
    bool hostsInstruments = false;
    DeviceId gridId = INVALID_DEVICE_ID;
    std::map<Container, ChainNodePath> containers;

    const ChainNodePath* find(Container container) const {
        const auto it = containers.find(container);
        return it == containers.end() ? nullptr : &it->second;
    }
};

/// Build a track holding one of every container shape, each already occupied by
/// a resident effect so that an index of 0 is always a real placement rather
/// than a subject's own index.
Shapes buildShapes(const juce::String& name, TrackType type) {
    auto& tm = TrackManager::getInstance();
    Shapes shapes;
    shapes.id = tm.createTrack(name, type);
    REQUIRE(shapes.id != INVALID_TRACK_ID);
    const auto* track = tm.getTrack(shapes.id);
    REQUIRE(track != nullptr);
    shapes.hostsInstruments = track->canHostInstrument();

    shapes.containers[Container::TrackList] = ChainNodePath::trackLevel(shapes.id);

    const auto rackId = tm.addRackToTrack(shapes.id, name + " Rack");
    REQUIRE(rackId != INVALID_RACK_ID);
    const auto chainId = tm.addChainToRack(ChainNodePath::rack(shapes.id, rackId));
    REQUIRE(chainId != INVALID_CHAIN_ID);
    const auto rackChain = ChainNodePath::chain(shapes.id, rackId, chainId);
    shapes.containers[Container::RackChain] = rackChain;

    const auto innerRackId = tm.addRackToChainByPath(rackChain, "Inner");
    REQUIRE(innerRackId != INVALID_RACK_ID);
    const auto innerChainId = tm.addChainToRack(rackChain.withRack(innerRackId));
    REQUIRE(innerChainId != INVALID_CHAIN_ID);
    shapes.containers[Container::NestedRackChain] =
        rackChain.withRack(innerRackId).withChain(innerChainId);

    // A pad rack belongs to a Drum Grid, which is an instrument, so a track
    // that cannot host one has no pad shapes at all.
    if (shapes.hostsInstruments) {
        shapes.gridId = tm.addDeviceToTrack(shapes.id, drumGrid(name + " Grid"));
        REQUIRE(shapes.gridId != INVALID_DEVICE_ID);
        const auto gridPath = ChainNodePath::topLevelDevice(shapes.id, shapes.gridId);
        const auto padChainId = tm.ensurePad(gridPath, 0);
        REQUIRE(padChainId != INVALID_CHAIN_ID);
        const auto padChain = ChainNodePath::padChain(shapes.id, shapes.gridId, padChainId);
        shapes.containers[Container::PadChain] = padChain;

        // The rack goes in whole rather than through `addRackToChainByPath`,
        // which resolves its parent through the rack walk and so cannot reach a
        // pad chain. Its chain comes with it for the same reason.
        const auto nestedPath = append(padChain, rackHolding("Pad Inner", effect("Pad Inner FX")));
        // Its chain id is read back out of the pad chain rather than asked of
        // `getRackByPath`, which walks the rack tree a pad rack is not part of.
        const auto* holder = tm.getChainByPath(padChain);
        REQUIRE(holder != nullptr);
        ChainId nestedChainId = INVALID_CHAIN_ID;
        for (const auto& element : holder->elements)
            if (isRack(element) && getRack(element).id == nestedPath.steps.back().id &&
                !getRack(element).chains.empty())
                nestedChainId = getRack(element).chains.front().id;
        REQUIRE(nestedChainId != INVALID_CHAIN_ID);
        shapes.containers[Container::PadNestedRackChain] = nestedPath.withChain(nestedChainId);
    }

    shapes.containers[Container::PostFx] = segmentContainer(shapes.id, ChainSegment::PostFx);
    shapes.containers[Container::MixerAnalysis] =
        segmentContainer(shapes.id, ChainSegment::MixerAnalysis);

    // One resident everywhere, so index 0 is never the subject's own index.
    for (const auto& [container, path] : shapes.containers) {
        if (container == Container::PostFx) {
            REQUIRE(tm.addDeviceToPostFx(shapes.id, resident()) != INVALID_DEVICE_ID);
        } else if (container == Container::MixerAnalysis) {
            REQUIRE(tm.addDeviceToMixerAnalysis(shapes.id, resident()) != INVALID_DEVICE_ID);
        } else {
            append(path, makeDeviceElement(resident()));
        }
    }

    return shapes;
}

struct World {
    Shapes host;
    Shapes peer;
    Shapes aux;

    const Shapes& of(TrackRole role) const {
        switch (role) {
            case TrackRole::Host:
                return host;
            case TrackRole::Peer:
                return peer;
            case TrackRole::Aux:
                return aux;
        }
        return host;
    }
};

World buildWorld() {
    resetState();
    World world;
    world.host = buildShapes("Host", TrackType::Media);
    world.peer = buildShapes("Peer", TrackType::Media);
    world.aux = buildShapes("Aux", TrackType::Aux);
    return world;
}

// ============================================================================
// Subjects
// ============================================================================

/// Why (@p subject, @p container) cannot be built on a media track, or empty
/// when it can. The one place a combination is declared impossible.
juce::String cannotPlace(Subject subject, Container container) {
    const bool carriesAnInstrument = subject == Subject::Instrument ||
                                     subject == Subject::RoutedGrid ||
                                     subject == Subject::MultiOutDriver;
    if (isFlatSection(container)) {
        if (subject == Subject::Rack)
            return "the flat sections hold bare devices, so no rack can be represented in one";
        if (carriesAnInstrument)
            return "the post-fader and mixer-analysis sections take effects only: nothing "
                   "generates sound after the fader";
    }

    if (subject == Subject::RoutedGrid && container != Container::TrackList)
        return "a pad's bus is carried by the output instance made for a top-level instrument, "
               "so only a grid in the track's own list can be on one";

    return {};
}

/// Put @p subject into @p container on @p shapes and answer with its path.
/// Only called for combinations `cannotPlace()` allows.
ChainNodePath placeSubject(Subject subject, const Shapes& shapes, Container container) {
    auto& tm = TrackManager::getInstance();
    const auto* containerPath = shapes.find(container);
    REQUIRE(containerPath != nullptr);

    if (isFlatSection(container)) {
        // Effects only: `cannotPlace()` has already excluded everything else.
        const auto id = container == Container::PostFx
                            ? tm.addDeviceToPostFx(shapes.id, effect("Subject"))
                            : tm.addDeviceToMixerAnalysis(shapes.id, effect("Subject"));
        REQUIRE(id != INVALID_DEVICE_ID);
        return containerPath->withDevice(id);
    }

    switch (subject) {
        case Subject::Effect:
            return append(*containerPath, makeDeviceElement(effect("Subject")));

        case Subject::Instrument:
            return append(*containerPath, makeDeviceElement(instrument("Subject")));

        case Subject::Rack:
            return append(*containerPath, rackHolding("Subject", effect("Inside")));

        case Subject::RoutedGrid: {
            const auto gridPath = append(*containerPath, makeDeviceElement(drumGrid("Subject")));
            REQUIRE(tm.setPadDevice(gridPath, 3, instrument("Kick")) != INVALID_DEVICE_ID);
            REQUIRE(tm.setPadOutput(gridPath, 3, 1));
            return gridPath;
        }

        case Subject::MultiOutDriver: {
            const auto devicePath =
                append(*containerPath, makeDeviceElement(multiOutInstrument("Subject")));
            const auto childId =
                tm.activateMultiOutPair(shapes.id, devicePath.getDeviceId(), /*pairIndex=*/1);
            REQUIRE(childId != INVALID_TRACK_ID);
            return devicePath;
        }
    }

    FAIL("unhandled subject");
    return {};
}

// ============================================================================
// The cell
// ============================================================================

/// The property every cell asserts.
///
/// Refused: the serialized model is byte-identical and nothing was announced,
/// and the no-op step the stack kept is a no-op in both directions too.
/// Applied: undo restores the bytes it started from and redo reproduces the
/// bytes it made.
void requireModel(const juce::String& expected, const char* stage) {
    const auto difference = modelDifference(expected, snapshot());
    INFO(stage << " left the model at " << difference);
    CHECK(difference.empty());
}

void requireRefusedOrReversible(const juce::String& cell,
                                std::unique_ptr<UndoableCommand> command) {
    INFO(cell);
    auto& undo = UndoManager::getInstance();
    undo.clearHistory();

    NotificationCounter notifications;
    const auto before = snapshot();
    undo.executeCommand(std::move(command));
    const auto after = snapshot();

    if (after == before) {
        INFO("refused");
        CHECK(notifications.count() == 0);
        // A refused step is still a step on the stack, and it has to be a no-op
        // in both directions.
        REQUIRE(undo.undo());
        requireModel(before, "undo");
        REQUIRE(undo.redo());
        requireModel(before, "redo");
        return;
    }

    INFO("applied");
    REQUIRE(undo.undo());
    requireModel(before, "undo");
    REQUIRE(undo.redo());
    requireModel(after, "redo");
}

juce::String cellName(const juce::String& operation, Subject subject, Container source) {
    return operation + " " + label(subject) + " in the " + label(source);
}

juce::String cellName(const juce::String& operation, Subject subject, Container source,
                      Container destination, TrackRole role) {
    return cellName(operation, subject, source) + " to the " + label(destination) + " of " +
           label(role);
}

}  // namespace

// ============================================================================
// Move and paste: every container to every container
// ============================================================================

TEST_CASE("Every move across the container matrix is refused or reversible",
          "[structural][undo][roundtrip][matrix]") {
    Ledger ledger;
    ledger.title = "move matrix";

    for (auto subject : kSubjects) {
        for (auto source : kContainers) {
            if (const auto reason = cannotPlace(subject, source); reason.isNotEmpty()) {
                ledger.exclude(cellName("move", subject, source), reason);
                continue;
            }

            for (auto destination : kContainers) {
                for (auto role : kRoles) {
                    const auto cell = cellName("move", subject, source, destination, role);

                    auto world = buildWorld();
                    const auto* target = world.of(role).find(destination);
                    if (target == nullptr) {
                        ledger.exclude(cell, "an aux track cannot host the Drum Grid a pad rack "
                                             "belongs to, so it has no pad shapes");
                        continue;
                    }

                    const auto subjectPath = placeSubject(subject, world.host, source);
                    requireRefusedOrReversible(
                        cell, std::make_unique<MoveChainElementCommand>(subjectPath, *target, 0));

                    ++ledger.ran;
                    ledger.sourcesCovered.insert(source);
                    ledger.destinationsCovered.insert(destination);
                    ledger.subjectsCovered.insert(subject);
                }
            }
        }
    }

    requireEveryAxisCovered(ledger, everySubject());
    WARN(ledger.report().toStdString());
    resetState();
}

TEST_CASE("Every paste across the container matrix is refused or reversible",
          "[structural][undo][roundtrip][matrix]") {
    Ledger ledger;
    ledger.title = "paste matrix";

    for (auto subject : kSubjects) {
        for (auto source : kContainers) {
            if (const auto reason = cannotPlace(subject, source); reason.isNotEmpty()) {
                ledger.exclude(cellName("paste", subject, source), reason);
                continue;
            }

            for (auto destination : kContainers) {
                for (auto role : kRoles) {
                    const auto cell = cellName("paste", subject, source, destination, role);

                    auto world = buildWorld();
                    const auto* target = world.of(role).find(destination);
                    if (target == nullptr) {
                        ledger.exclude(cell, "an aux track cannot host the Drum Grid a pad rack "
                                             "belongs to, so it has no pad shapes");
                        continue;
                    }

                    const auto subjectPath = placeSubject(subject, world.host, source);
                    auto copied = TrackManager::getInstance().copyChainElements({subjectPath});
                    requireRefusedOrReversible(cell, std::make_unique<PasteChainElementsCommand>(
                                                         *target, std::move(copied), 0));

                    ++ledger.ran;
                    ledger.sourcesCovered.insert(source);
                    ledger.destinationsCovered.insert(destination);
                    ledger.subjectsCovered.insert(subject);
                }
            }
        }
    }

    requireEveryAxisCovered(ledger, everySubject());
    WARN(ledger.report().toStdString());
    resetState();
}

// ============================================================================
// Operations that stay where the subject already is
// ============================================================================

TEST_CASE("Every wrap across the container matrix is refused or reversible",
          "[structural][undo][roundtrip][matrix]") {
    Ledger ledger;
    ledger.title = "wrap matrix";

    for (auto subject : kSubjects) {
        for (auto container : kContainers) {
            const auto cell = cellName("wrap", subject, container);
            if (const auto reason = cannotPlace(subject, container); reason.isNotEmpty()) {
                ledger.exclude(cell, reason);
                continue;
            }

            auto world = buildWorld();
            const auto subjectPath = placeSubject(subject, world.host, container);
            requireRefusedOrReversible(cell,
                                       std::make_unique<WrapChainElementsInRackCommand>(
                                           std::vector<ChainNodePath>{subjectPath}, "Wrapper"));

            ++ledger.ran;
            ledger.sourcesCovered.insert(container);
            ledger.destinationsCovered.insert(container);
            ledger.subjectsCovered.insert(subject);
        }
    }

    requireEveryAxisCovered(ledger, everySubject());
    WARN(ledger.report().toStdString());
    resetState();
}

TEST_CASE("Every duplication across the container matrix is refused or reversible",
          "[structural][undo][roundtrip][matrix]") {
    // Duplicating a device or a rack is a copy of it pasted back beside itself,
    // which is what the chain view's alt-drag and its duplicate action both do.
    Ledger ledger;
    ledger.title = "duplication matrix";

    for (auto subject : kSubjects) {
        for (auto container : kContainers) {
            const auto cell = cellName("duplicate", subject, container);
            if (const auto reason = cannotPlace(subject, container); reason.isNotEmpty()) {
                ledger.exclude(cell, reason);
                continue;
            }

            auto world = buildWorld();
            const auto* containerPath = world.host.find(container);
            REQUIRE(containerPath != nullptr);
            const auto subjectPath = placeSubject(subject, world.host, container);

            auto& tm = TrackManager::getInstance();
            auto copied = tm.copyChainElements({subjectPath});
            const int index = tm.getChainElementIndex(subjectPath);
            requireRefusedOrReversible(cell, std::make_unique<PasteChainElementsCommand>(
                                                 *containerPath, std::move(copied), index + 1));

            ++ledger.ran;
            ledger.sourcesCovered.insert(container);
            ledger.destinationsCovered.insert(container);
            ledger.subjectsCovered.insert(subject);
        }
    }

    requireEveryAxisCovered(ledger, everySubject());
    WARN(ledger.report().toStdString());
    resetState();
}

TEST_CASE("Every removal across the container matrix is refused or reversible",
          "[structural][undo][roundtrip][matrix]") {
    Ledger ledger;
    ledger.title = "removal matrix";

    for (auto subject : kSubjects) {
        for (auto container : kContainers) {
            const auto cell = cellName("remove", subject, container);
            if (const auto reason = cannotPlace(subject, container); reason.isNotEmpty()) {
                ledger.exclude(cell, reason);
                continue;
            }
            if (subject == Subject::Rack) {
                ledger.exclude(cell, "no undoable command removes a rack: the chain view calls "
                                     "removeRackFromChainByPath() directly");
                continue;
            }

            auto world = buildWorld();
            const auto subjectPath = placeSubject(subject, world.host, container);
            requireRefusedOrReversible(cell,
                                       std::make_unique<RemoveDeviceByPathCommand>(subjectPath));

            ++ledger.ran;
            ledger.sourcesCovered.insert(container);
            ledger.destinationsCovered.insert(container);
            ledger.subjectsCovered.insert(subject);
        }
    }

    requireEveryAxisCovered(ledger, everySubjectExcept(Subject::Rack));
    WARN(ledger.report().toStdString());
    resetState();
}

TEST_CASE("Duplicating a track carrying any subject in any container is reversible",
          "[structural][undo][roundtrip][matrix]") {
    Ledger ledger;
    ledger.title = "track duplication matrix";

    for (auto subject : kSubjects) {
        for (auto container : kContainers) {
            const auto cell = cellName("duplicate the track holding", subject, container);
            if (const auto reason = cannotPlace(subject, container); reason.isNotEmpty()) {
                ledger.exclude(cell, reason);
                continue;
            }

            auto world = buildWorld();
            placeSubject(subject, world.host, container);
            requireRefusedOrReversible(
                cell, std::make_unique<DuplicateTrackCommand>(world.host.id, /*duplicateContent=*/
                                                              true, /*duplicateDevices=*/true));

            ++ledger.ran;
            ledger.sourcesCovered.insert(container);
            ledger.destinationsCovered.insert(container);
            ledger.subjectsCovered.insert(subject);
        }
    }

    requireEveryAxisCovered(ledger, everySubject());
    WARN(ledger.report().toStdString());
    resetState();
}

// ============================================================================
// Pad structural edits, from a grid in every container that can hold one
// ============================================================================

namespace {

/// Where a Drum Grid can stand while its pads are edited.
constexpr std::array kGridContainers{
    Container::TrackList, Container::RackChain,          Container::NestedRackChain,
    Container::PadChain,  Container::PadNestedRackChain,
};

struct PadOperation {
    const char* name;
    /// Runs against the live model inside one `EditPadsCommand`.
    void (*apply)(const ChainNodePath& gridPath, ChainId padChainId, DeviceId padDeviceId);
};

constexpr std::array kPadOperations{
    PadOperation{"set a pad's device",
                 [](const ChainNodePath& grid, ChainId, DeviceId) {
                     TrackManager::getInstance().setPadDevice(grid, 5, instrument("Snare"));
                 }},
    PadOperation{"clear a pad", [](const ChainNodePath& grid, ChainId,
                                   DeviceId) { TrackManager::getInstance().clearPad(grid, 0); }},
    PadOperation{"swap two pads",
                 [](const ChainNodePath& grid, ChainId, DeviceId) {
                     TrackManager::getInstance().swapPads(grid, 0, 1);
                 }},
    PadOperation{"remove a pad chain",
                 [](const ChainNodePath& grid, ChainId padChainId, DeviceId) {
                     TrackManager::getInstance().removePadChain(grid, padChainId);
                 }},
    PadOperation{"add a device to a pad",
                 [](const ChainNodePath& grid, ChainId padChainId, DeviceId) {
                     TrackManager::getInstance().addDeviceToPad(grid, padChainId, effect("Pad FX"));
                 }},
    PadOperation{"remove a device from a pad",
                 [](const ChainNodePath& grid, ChainId padChainId, DeviceId padDeviceId) {
                     TrackManager::getInstance().removeDeviceFromPad(grid, padChainId, padDeviceId);
                 }},
    PadOperation{"reorder a pad's devices",
                 [](const ChainNodePath& grid, ChainId padChainId, DeviceId) {
                     TrackManager::getInstance().moveDeviceInPad(grid, padChainId, 0, 1);
                 }},
};

}  // namespace

TEST_CASE("Every pad structural edit from every container is refused or reversible",
          "[structural][undo][roundtrip][matrix][pads]") {
    Ledger ledger;
    ledger.title = "pad edit matrix";

    for (const auto& operation : kPadOperations) {
        for (auto container : kGridContainers) {
            const juce::String cell =
                juce::String(operation.name) + " on a grid in the " + label(container);

            auto world = buildWorld();
            const auto* containerPath = world.host.find(container);
            REQUIRE(containerPath != nullptr);

            auto& tm = TrackManager::getInstance();
            const auto gridPath = append(*containerPath, makeDeviceElement(drumGrid("Subject")));
            const auto padChainId = tm.ensurePad(gridPath, 0);
            REQUIRE(padChainId != INVALID_CHAIN_ID);
            const auto padDeviceId = tm.setPadDevice(gridPath, 0, instrument("Kick"));
            REQUIRE(padDeviceId != INVALID_DEVICE_ID);
            REQUIRE(tm.addDeviceToPad(gridPath, padChainId, effect("Pad FX")) != INVALID_DEVICE_ID);
            REQUIRE(tm.ensurePad(gridPath, 1) != INVALID_CHAIN_ID);

            const auto apply = operation.apply;
            requireRefusedOrReversible(
                cell, std::make_unique<EditPadsCommand>(
                          gridPath, operation.name, [gridPath, padChainId, padDeviceId, apply] {
                              apply(gridPath, padChainId, padDeviceId);
                          }));

            ++ledger.ran;
            ledger.sourcesCovered.insert(container);
            ledger.destinationsCovered.insert(container);
        }
    }

    for (auto container : kGridContainers) {
        INFO("grid container: " << label(container));
        CHECK(ledger.sourcesCovered.count(container) == 1);
    }
    CHECK(ledger.ran == static_cast<int>(kPadOperations.size() * kGridContainers.size()));
    WARN(ledger.report().toStdString());
    resetState();
}
