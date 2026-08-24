#include "MgdFixture.hpp"

#include <juce_audio_formats/juce_audio_formats.h>

#include <map>
#include <set>

#include "core/SourcePool.hpp"
#include "project/serialization/ProjectSerializer.hpp"

namespace magda::nulldiff {
namespace {

/// What the written file actually is, read back rather than assumed.
struct WrittenFacts {
    bool readable = false;
    double sampleRate = 0.0;
    double durationSeconds = 0.0;
};

WrittenFacts factsOf(const juce::File& file) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0)
        return {};

    return {.readable = true,
            .sampleRate = reader->sampleRate,
            .durationSeconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate};
}

/// Every source the project references, across both of the lists a load can
/// produce.
///
/// Two lists rather than one because a v1 project carried its source inline on
/// the clip and a v2 project carries a pooled table, and a corpus of real
/// projects has both in it: the oldest files here predate the table entirely.
/// The rig walks them together, because "a source this project plays" is one
/// question and which list it arrived in is an accident of when it was saved.
std::vector<Source*> everySource(StagedProjectData& staged) {
    std::vector<Source*> all;
    all.reserve(staged.sources.size() + staged.legacySources.size());
    for (auto& source : staged.sources)
        all.push_back(&source);
    for (auto& source : staged.legacySources)
        all.push_back(&source);
    return all;
}

std::string quoted(const juce::String& text) {
    return "\"" + text.toStdString() + "\"";
}

/// The last component of a path that may name a volume this machine has never
/// had. Built without checking, because checking is what it cannot do.
juce::String fileNameOf(const juce::String& path) {
    return juce::File::createFileWithoutCheckingPath(path).getFileName();
}

using Declarations = std::map<juce::String, const FixtureSource*>;

/**
 * @brief Pair every source the project plays with the manifest entry that
 *        stands in for it, or say which one has no partner.
 *
 * Both directions. A project source the manifest does not name is the failure
 * this rig is built around: its path points at a machine that is gone, so the
 * case would reach a leg with a source that reads nothing, and nothing nulls
 * against nothing. A manifest entry no source claims is refused for the reason
 * the DAWproject loss table refuses one -- a declaration that has stopped being
 * true is worse than no declaration, because it reads like coverage.
 */
std::string matchSources(const MgdFixture& fixture, const std::vector<Source*>& sources,
                         Declarations& declared) {
    for (const auto& source : fixture.sources) {
        const juce::String name{source.fileName};
        if (!declared.emplace(name, &source).second)
            return "the manifest names " + quoted(name) + " twice";
    }

    std::set<juce::String> claimed;
    for (const auto* source : sources) {
        const auto name = fileNameOf(source->filePath);
        if (declared.find(name) == declared.end())
            return "the project plays " + quoted(name) +
                   " and the manifest does not name it, so the case would render silence where "
                   "that source should be";
        claimed.insert(name);
    }

    for (const auto& [name, declaration] : declared)
        if (claimed.count(name) == 0)
            return "the manifest names " + quoted(name) + " and no source in the project claims it";

    return {};
}

/**
 * @brief Write each source's stand-in material and point the project at it.
 *
 * Written under the name the project referenced rather than an invented one, so
 * a path in a failure message is still the path the project asked for and a
 * scratch directory is readable by whoever is debugging a case.
 *
 * Read back rather than trusted. The manifest's rate and duration are what the
 * material was asked for; what the file is is what a leg will play, and a spec
 * whose duration does not survive the writer is a case rendering something
 * other than what it declared.
 */
std::string writeAndRepoint(const std::vector<Source*>& sources, const Declarations& declared,
                            const juce::File& directory,
                            std::map<juce::String, juce::File>& written) {
    auto& pool = SourcePool::getInstance();

    // The stand-in already written for a path, so a project that names one file
    // twice gets one file back. That is not a corner case: a v2 project can
    // carry a pooled table entry beside a v1 clip that migrated to the same
    // path, which is the pair installStagedSources exists to collapse. Two
    // names for one sound is one sound.
    std::map<juce::String, juce::File> byOriginalPath;

    for (auto* source : sources) {
        const auto original = source->filePath;
        const auto name = fileNameOf(original);
        const auto* declaration = declared.at(name);

        if (const auto seen = byOriginalPath.find(original); seen != byOriginalPath.end()) {
            const auto facts = factsOf(seen->second);
            source->filePath = seen->second.getFullPathName();
            source->sampleRate = facts.sampleRate;
            source->durationSeconds = facts.durationSeconds;
            continue;
        }

        const auto file = writeMaterial(directory, name.upToLastOccurrenceOf(".", false, false),
                                        declaration->material);
        const auto facts = factsOf(file);
        if (!facts.readable)
            return "the material written for " + quoted(name) + " does not read back";

        // Whether the write landed on a file that was already there, asked of
        // the directory rather than of the path.
        //
        // The name check ahead of this predicts what the filesystem will do with
        // two names, and a prediction made out of strings can only model what
        // strings can express. Case it can, by folding. Unicode normalisation it
        // cannot: macOS stores some names decomposed and some composed, so two
        // packs with an accented character produce two paths that differ as
        // strings, fold differently, and name one file. Comparing the written
        // paths would miss exactly that, because they differ as strings too.
        //
        // Counting does not care why two names met. The directory belongs to
        // this fixture and was emptied before the first write, so it holds one
        // file per distinct source or two of them are one file.
        //
        // Counted against the distinct paths rather than against the sources,
        // because a repeated path is meant to reuse its file and would
        // otherwise be refused for doing what it was asked to do.
        const auto distinct = static_cast<int>(byOriginalPath.size()) + 1;
        if (directory.getNumberOfChildFiles(juce::File::findFiles) != distinct)
            return "the stand-in written for " + quoted(original) + " as " + quoted(name) +
                   " landed on a file already written for another source, so the two would "
                   "collapse into one and their clips would play the same sound";

        source->filePath = file.getFullPathName();
        source->sampleRate = facts.sampleRate;
        source->durationSeconds = facts.durationSeconds;

        // Seeded rather than probed, the way every other case's material is:
        // the install path acquires by path and would otherwise open the file a
        // second time to learn what the reader just said.
        pool.seedFactsForTesting(source->filePath, facts.durationSeconds, facts.sampleRate);

        byOriginalPath.emplace(original, file);
        written.emplace(name, file);
    }

    return {};
}

/// Every device anywhere in a track's chain, racks and flat sections included.
void everyDevice(const std::vector<ChainElement>& elements, std::vector<const DeviceInfo*>& out) {
    for (const auto& element : elements) {
        if (magda::isDevice(element)) {
            out.push_back(&magda::getDevice(element));
        } else if (magda::isRack(element)) {
            for (const auto& chain : magda::getRack(element).chains)
                everyDevice(chain.elements, out);
        }
    }
}

void everyDevice(const TrackInfo& track, std::vector<const DeviceInfo*>& out) {
    everyDevice(track.chain.fxChainElements, out);
    for (const auto* section :
         {&track.chain.postFxChainElements, &track.chain.mixerAnalysisElements})
        for (const auto& element : *section)
            out.push_back(&element.device);
}

/**
 * @brief Why this project cannot be a fixture here, if it hosts a plugin.
 *
 * A project with a VST3 in it has no verdict a corpus can hold it to. Without
 * the plugin installed both legs render a passthrough and pass by agreeing
 * about nothing, which is the silence-nulls-against-silence failure wearing a
 * different hat. With it installed the incumbent hosts it and the native leg
 * cannot (#1893), so whether the case passes is a fact about the machine that
 * ran it.
 *
 * #2175 is where those projects go, with the invariant tier and a gate that
 * calls an absent plugin unmeasurable rather than equal. Refused here rather
 * than left to whoever writes a manifest, because the tier is a field somebody
 * fills in and the format is a fact about the file.
 */
std::string refuseHostedPlugins(const StagedProjectData& staged) {
    std::vector<const DeviceInfo*> devices;
    for (const auto& track : staged.tracks)
        everyDevice(track, devices);
    if (staged.masterTrack != nullptr)
        everyDevice(*staged.masterTrack, devices);

    for (const auto* device : devices)
        if (device->format != magda::PluginFormat::Internal)
            return "the project hosts " + quoted(device->name) +
                   ", which is not an internal device, so neither engine owes the other a "
                   "sample on it: see #2175 for the tier those projects get";

    return {};
}

/**
 * @brief Put @p held back in the pool beside what the install just left, and
 *        move this project's sources to ids nothing else is using.
 *
 * The install owns the pool it was handed: it cleared it, and what it left is
 * this project under ids that came out of an allocator reset to one. Both facts
 * are right for an app with one project open and wrong for a corpus holding
 * several cases, whose Cases carry ids and nothing else.
 *
 * So the previous contents go back first, which lifts the allocator above every
 * id already spoken for, and then each of this project's sources is acquired
 * afresh. A path already in @p held keeps the id it had there, because it is the
 * same file and two ids for one file is the collapse this rig spends its time
 * refusing.
 *
 * The clips follow. A pure substitution, not the install's remap: that one
 * rescales sample positions because a v1 nominal rate can differ from the
 * probed one, and here the two ids name the same path with the same facts, so
 * there is nothing to rescale.
 */
std::string renumberOntoPool(const std::vector<Source>& held, std::vector<ClipInfo>& clips) {
    auto& pool = SourcePool::getInstance();
    const auto mine = pool.snapshot();

    pool.clear();
    for (const auto& source : held)
        pool.insert(source);

    std::map<SourceId, SourceId> moved;
    for (const auto& source : mine) {
        const auto id = pool.acquire(source.filePath);
        if (id == INVALID_SOURCE_ID)
            return "the pool would not take " + quoted(source.filePath) + " back";

        if (auto* pooled = pool.getMutable(id); pooled != nullptr) {
            if (pooled->durationSeconds <= 0.0)
                pooled->durationSeconds = source.durationSeconds;
            if (pooled->sampleRate <= 0.0)
                pooled->sampleRate = source.sampleRate;
        }

        if (id != source.id)
            moved[source.id] = id;
    }

    if (moved.empty())
        return {};

    for (auto& clip : clips) {
        if (!clip.isAudio())
            continue;
        for (auto& event : clip.audio().events)
            if (const auto it = moved.find(event.sourceId); it != moved.end())
                event.sourceId = it->second;
    }

    return {};
}

}  // namespace

std::string refuseIndistinguishableSources(const std::vector<juce::String>& paths) {
    std::map<juce::String, juce::String> byName;

    for (const auto& path : paths) {
        // Folded, because the question is whether two names can name one file
        // and the answer belongs to the filesystem the stand-ins are written
        // to. macOS and Windows both answer yes by default, so "Loop.wav" and
        // "loop.wav" are one file there and two here: comparing them exactly
        // would let the pair through and then write the second over the first.
        //
        // Conservative on a case-sensitive filesystem, deliberately. A corpus
        // that refuses a fixture on Linux and accepts it on macOS is worse than
        // one that refuses it everywhere, because the failure would arrive on
        // somebody else's machine.
        const auto name = fileNameOf(path);
        const auto [existing, inserted] = byName.emplace(name.toLowerCase(), path);
        if (!inserted && existing->second != path)
            return "the project plays two different files both called " + quoted(name) + " (" +
                   existing->second.toStdString() + " and " + path.toStdString() +
                   "), and a manifest names a source by that alone, so one would stand in for "
                   "both";
    }

    return {};
}

juce::File fixtureCorpusDir() {
    return juce::File(MAGDA_TEST_CORPUS_DIR);
}

FixtureLoad loadFixture(const MgdFixture& fixture, const juce::File& scratchDirectory) {
    FixtureLoad result;

    // Each fixture writes into its own directory under the scratch root, named
    // for the case. Two fixtures are allowed to reference files with the same
    // name -- they are different projects and nothing ties their sources
    // together -- and a flat scratch directory would have the second one
    // overwrite the first's material. The first fixture's Case would still be
    // holding a path, and that path would now be playing the other project's
    // sound. Loading them one at a time hides it; #2081 holds a corpus of them
    // at once.
    const auto materialDirectory = scratchDirectory.getChildFile(
        juce::String(fixture.declaration.name).replaceCharacters("/\\:", "___"));

    // Emptied, not just created. writeMaterial overwrites for the reason #2040
    // gives -- a run that reused a file from a previous run with different
    // contents is the one failure nobody would think to look for -- and the
    // collision check below counts what the directory holds, which only means
    // anything if everything in it was written by this load.
    materialDirectory.deleteRecursively();
    materialDirectory.createDirectory();

    const auto file = fixtureCorpusDir().getChildFile(fixture.file);
    if (!file.existsAsFile()) {
        result.failure =
            "the fixture names a file that is not there: " + file.getFullPathName().toStdString();
        return result;
    }

    StagedProjectData staged;
    if (!ProjectSerializer::loadAndStage(file, staged)) {
        result.failure =
            "the project would not load: " + ProjectSerializer::getLastError().toStdString();
        return result;
    }

    if (auto refusal = refuseHostedPlugins(staged); !refusal.empty()) {
        result.failure = std::move(refusal);
        return result;
    }

    auto sources = everySource(staged);

    std::vector<juce::String> paths;
    paths.reserve(sources.size());
    for (const auto* source : sources)
        paths.push_back(source->filePath);

    if (auto refusal = refuseIndistinguishableSources(paths); !refusal.empty()) {
        result.failure = std::move(refusal);
        return result;
    }

    Declarations declared;
    if (auto refusal = matchSources(fixture, sources, declared); !refusal.empty()) {
        result.failure = std::move(refusal);
        return result;
    }

    // --- the material, written and then read back ----------------------------
    //
    // Written under the name the project referenced rather than under an
    // invented one, so a path in a failure message is still the path the
    // project asked for and a scratch directory is readable by whoever is
    // debugging a case.
    //
    // Read back rather than trusted. The manifest's rate and duration are what
    // the material was asked for; what the file is is what a leg will play, and
    // a spec whose duration does not survive the writer is a case rendering
    // something other than what it declared.

    if (auto refusal = writeAndRepoint(sources, declared, materialDirectory, result.written);
        !refusal.empty()) {
        result.failure = std::move(refusal);
        return result;
    }

    // The app's own install, not a copy of it. It clears the pool, inserts the
    // v2 table with ids preserved, acquires the v1 sources under real ids and
    // remaps the events that referenced the provisional ones -- which is a
    // paragraph of behaviour a second implementation would get subtly wrong and
    // then agree with itself about.
    //
    // Then put back what it cleared, and renumber. That half is the corpus's
    // problem rather than the app's: the app has one project open, so clearing
    // the pool is exactly right for it and fatal here. `clear()` resets the id
    // allocator to one, so two fixtures both come back holding source id 1 for
    // different files, and a Case carries nothing but those numbers. Load the
    // second and the first silently starts resolving its clips to the second's
    // audio; load either and the code-built corpus, whose ids were handed out by
    // the same allocator, starts resolving to a fixture's.
    //
    // Renumbering rather than asking callers to reinstall before rendering. The
    // rest of the corpus already holds every case's sources in the pool at once,
    // and a fixture that was only valid while it was the most recent thing
    // loaded would be a second contract for whoever holds a corpus of them
    // (#2081) to get right on every path.
    const auto held = SourcePool::getInstance().snapshot();

    ProjectSerializer::installStagedSources(staged.sources, staged.legacySources, staged.clips);
    ProjectSerializer::resolveStagedSources(staged.sources);

    if (auto refusal = renumberOntoPool(held, staged.clips); !refusal.empty()) {
        result.failure = std::move(refusal);
        return result;
    }

    // --- the case ------------------------------------------------------------
    //
    // The declarations first and the project over the top, so a manifest can
    // never quietly assert a project different from the one on disk: every
    // field the load fills is a field the manifest does not get to have an
    // opinion about.

    result.value = fixture.declaration;
    result.value.tracks = std::move(staged.tracks);
    if (staged.masterTrack != nullptr)
        result.value.master = *staged.masterTrack;
    result.value.clips = std::move(staged.clips);
    result.value.lanes = std::move(staged.automationLanes);
    result.value.automationClips = std::move(staged.automationClips);

    // The project's own tempo rather than the corpus default. A case built in
    // code chooses one; a real project already has one, and rendering it at
    // 120 would put every clip somewhere the person who saved it never heard.
    result.value.tempo = {TempoPoint{.beat = 0.0, .bpm = staged.info.tempo}};

    // This project's sources, found by the files the rig wrote for it, rather
    // than everything the pool holds. The pool now carries other cases' sources
    // too, and a Case listing those would be claiming to play them.
    auto& pool = SourcePool::getInstance();
    for (const auto& [name, file] : result.written) {
        const auto id = pool.findByPath(file.getFullPathName());
        const auto* pooled = id != INVALID_SOURCE_ID ? pool.get(id) : nullptr;
        if (pooled == nullptr) {
            result.failure = "the stand-in written for " + quoted(name) + " is not pooled";
            return result;
        }

        SourceFact fact;
        fact.id = pooled->id;
        fact.path = pooled->filePath;
        fact.sampleRate = pooled->sampleRate;
        fact.durationSeconds = pooled->durationSeconds;
        result.value.sources.push_back(fact);
    }

    // One pooled source per source the manifest names, checked rather than
    // assumed, because both ways of being wrong are silent.
    //
    // Too few means two sources collapsed somewhere past the checks above and
    // two clips are now playing one sound. Too many means the install did not
    // collapse a pair it should have, and a repeated path is holding two ids
    // where the project had one.
    //
    // On every fixture, forever, rather than in a test that names one: the
    // reuse branch above has no fixture exercising it today, since a repeated
    // path needs a v2 table entry beside a v1 clip on the same file and none of
    // the projects here has that shape. This is what will catch it the day one
    // arrives.
    if (result.value.sources.size() != fixture.sources.size()) {
        result.failure = "the project ended up with " +
                         std::to_string(result.value.sources.size()) + " sources where the " +
                         "manifest names " + std::to_string(fixture.sources.size());
        result.value = {};
        return result;
    }

    result.ok = true;
    return result;
}

}  // namespace magda::nulldiff
