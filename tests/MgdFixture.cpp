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
                            const juce::File& directory, std::vector<juce::File>& written) {
    auto& pool = SourcePool::getInstance();

    for (auto* source : sources) {
        const auto name = fileNameOf(source->filePath);
        const auto* declaration = declared.at(name);

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
        // Splice packs with an accented character produce two paths that differ
        // as strings, fold differently, and name one file. Comparing the written
        // paths would miss exactly that, because they differ as strings too.
        //
        // Counting does not care why two names met. The directory belongs to
        // this fixture and was emptied before the first write, so after n writes
        // it holds n files or two of them are one file.
        const auto grewTo = directory.getNumberOfChildFiles(juce::File::findFiles);
        if (grewTo != static_cast<int>(written.size()) + 1)
            return "the stand-in written for " + quoted(source->filePath) + " as " + quoted(name) +
                   " landed on a file already written for another source, so the two would "
                   "collapse into one and their clips would play the same sound";

        source->filePath = file.getFullPathName();
        source->sampleRate = facts.sampleRate;
        source->durationSeconds = facts.durationSeconds;

        // Seeded rather than probed, the way every other case's material is:
        // the install path acquires by path and would otherwise open the file a
        // second time to learn what the reader just said.
        pool.seedFactsForTesting(source->filePath, facts.durationSeconds, facts.sampleRate);
        written.push_back(file);
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
    ProjectSerializer::installStagedSources(staged.sources, staged.legacySources, staged.clips);
    ProjectSerializer::resolveStagedSources(staged.sources);

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

    for (const auto& pooled : SourcePool::getInstance().snapshot()) {
        SourceFact fact;
        fact.id = pooled.id;
        fact.path = pooled.filePath;
        fact.sampleRate = pooled.sampleRate;
        fact.durationSeconds = pooled.durationSeconds;
        result.value.sources.push_back(fact);
    }

    result.ok = true;
    return result;
}

}  // namespace magda::nulldiff
