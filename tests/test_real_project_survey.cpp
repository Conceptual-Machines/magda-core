#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "LegacyCorpus.hpp"
#include "MgdFixture.hpp"
#include "NullDiffNativeLeg.hpp"
#include "core/SourcePool.hpp"
#include "project/serialization/ProjectSerializer.hpp"

/**
 * @file test_real_project_survey.cpp
 * @brief What the legacy projects would cost as render cases (#2081).
 *
 * #2081 asks which real projects the corpus carries and what each declares, and
 * says the cost question is answered by measuring rather than by deciding in
 * advance. This is the measuring. It is not the corpus: nothing here declares a
 * tier or asserts a residual, and every project is handed the same placeholder
 * material rather than material chosen for what its path goes through.
 *
 * What it reports, per project: whether the fixture rig will take it at all,
 * what the plan compiler says about it, how many ops the plan carries, and how
 * long one render takes. That is the table #2081 needs before anybody writes a
 * manifest, because three of those four answers are reasons a project cannot be
 * a case and the fourth decides where the cases run.
 *
 * It asserts almost nothing on purpose. A project the engine cannot yet render
 * is a fact about #2174's device coverage, not a failure of this file, and a
 * survey that went red every time the engine was mid-way through something
 * would be deleted rather than read.
 */

using namespace magda;
using namespace magda::nulldiff;

namespace {

juce::File scratch() {
    auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("magda_real_project_survey");
    root.createDirectory();
    return root;
}

/// The file names a project's sources end in, read by loading it.
///
/// The manifest below is generated rather than written, which is the one thing
/// a survey may do and a corpus may not: choosing material is a judgement about
/// what stands between the two engines on that path (#2040), and generating it
/// would be pretending to have made that judgement. Here nothing is asserted
/// about the audio, so a placeholder is honest.
std::vector<juce::String> sourceNamesOf(const juce::File& file) {
    StagedProjectData staged;
    if (!ProjectSerializer::loadAndStage(file, staged))
        return {};

    std::set<juce::String> names;
    const auto add = [&names](const std::vector<Source>& sources) {
        for (const auto& source : sources)
            names.insert(source.filePath.fromLastOccurrenceOf("/", false, false)
                             .fromLastOccurrenceOf("\\", false, false));
    };
    add(staged.sources);
    add(staged.legacySources);

    return {names.begin(), names.end()};
}

struct Surveyed {
    std::string file;
    bool loaded = false;
    std::string refusal;
    int tracks = 0;
    int clips = 0;
    int sources = 0;
    bool rendered = false;
    std::string renderFailure;
    std::vector<std::string> diagnostics;
    std::int64_t renderMs = 0;
};

Surveyed survey(const test::legacy_corpus::ProjectFixture& project) {
    Surveyed out;
    out.file = project.file;

    // Held for the whole call: MgdFixture::file is a const char* because the
    // corpus's own table is string literals, and a survey building one at
    // runtime has to own the storage it points at.
    const std::string path = std::string("legacy/projects/") + project.file;

    MgdFixture fixture;
    fixture.file = path.c_str();
    fixture.savedBy = project.savedBy;
    fixture.declaration.name = std::string("survey.") + project.file;
    fixture.declaration.covers = project.covers;
    fixture.declaration.endBeat = 16.0;

    // A four-second impulse grid for every source. Long enough that nothing
    // runs out inside sixteen beats at any tempo a project here uses.
    MaterialSpec spec;
    spec.kind = MaterialKind::Impulses;
    spec.durationSeconds = 8.0;
    spec.intervalSeconds = 0.25;

    std::vector<std::string> names;
    for (const auto& name : sourceNamesOf(fixtureCorpusDir().getChildFile(fixture.file)))
        names.push_back(name.toStdString());
    for (const auto& name : names)
        fixture.sources.push_back({.fileName = name.c_str(), .material = spec, .covers = "survey"});

    const auto loaded = loadFixture(fixture, scratch());
    out.loaded = loaded.ok;
    out.refusal = loaded.failure;
    if (!loaded.ok)
        return out;

    out.tracks = static_cast<int>(loaded.value.tracks.size());
    out.clips = static_cast<int>(loaded.value.clips.size());
    out.sources = static_cast<int>(loaded.value.sources.size());

    const auto started = std::chrono::steady_clock::now();
    const auto render = renderNative(loaded.value);
    out.renderMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - started)
                       .count();

    out.rendered = render.failure.empty();
    out.renderFailure = render.failure;
    out.diagnostics = render.diagnostics;
    return out;
}

}  // namespace

TEST_CASE("What the legacy projects would cost as render cases", "[nulldiff][survey]") {
    const PooledSourcesUnwind unwind;

    std::vector<Surveyed> rows;
    for (const auto& project : test::legacy_corpus::projectFixtures())
        rows.push_back(survey(project));

    juce::String report;
    report << "\nmagda-real-project-survey\n";
    report << juce::String("projects=") << juce::String(static_cast<int>(rows.size())) << "\n";

    auto usable = 0;
    for (const auto& row : rows) {
        report << juce::String(row.file).paddedRight(' ', 32) << " ";

        if (!row.loaded) {
            report << "refused: " << juce::String(row.refusal).substring(0, 96) << "\n";
            continue;
        }

        report << "tracks=" << juce::String(row.tracks).paddedRight(' ', 3)
               << " clips=" << juce::String(row.clips).paddedRight(' ', 4)
               << " srcs=" << juce::String(row.sources).paddedRight(' ', 3)
               << " render=" << juce::String(static_cast<int>(row.renderMs)).paddedLeft(' ', 5)
               << "ms ";

        if (!row.rendered) {
            report << " FAILED: " << juce::String(row.renderFailure).substring(0, 72);
        } else if (!row.diagnostics.empty()) {
            report << " diagnostics=" << juce::String(static_cast<int>(row.diagnostics.size()))
                   << ": " << juce::String(row.diagnostics.front()).substring(0, 72);
        } else {
            report << " clean";
            ++usable;
        }

        report << "\n";
    }

    report << "\nclean and renderable: " << juce::String(usable) << " of "
           << juce::String(static_cast<int>(rows.size())) << "\n";

    // Every distinct diagnostic once, which is the list of what stands between
    // these projects and being cases.
    std::map<std::string, int> reasons;
    for (const auto& row : rows) {
        if (!row.loaded)
            reasons[row.refusal.substr(0, 72)]++;
        for (const auto& diagnostic : row.diagnostics)
            reasons[diagnostic.substr(0, 72)]++;
    }
    report << "\nreasons, with how many projects each covers:\n";
    for (const auto& [reason, count] : reasons)
        report << "  " << juce::String(count).paddedLeft(' ', 3) << "  " << juce::String(reason)
               << "\n";

    WARN(report.toStdString());

    // The survey asserts only that it ran. What it found is a fact about the
    // engine's coverage today and belongs in the report rather than in a
    // verdict: a file that went red every time the device layer was mid-way
    // through something would be deleted rather than read.
    CHECK(rows.size() == test::legacy_corpus::projectFixtures().size());
}
