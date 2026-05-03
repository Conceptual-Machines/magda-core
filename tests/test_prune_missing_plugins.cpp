#include <juce_audio_processors/juce_audio_processors.h>

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/engine/TracktionEngineWrapper.hpp"

using magda::TracktionEngineWrapper;

namespace {

juce::File testTempRoot() {
    auto envTmp = juce::SystemStats::getEnvironmentVariable("TMPDIR", {});
    auto root = envTmp.isNotEmpty() ? juce::File(envTmp)
                                    : juce::File::getSpecialLocation(juce::File::tempDirectory);
    root.createDirectory();
    return root;
}

juce::File createExistingPluginFile(const juce::String& suffix) {
    auto file = testTempRoot().getNonexistentChildFile("plugin", suffix);
    REQUIRE(file.create().wasOk());
    return file;
}

// Build a platform-correct absolute path that is guaranteed not to exist.
// Hard-coded "/foo/bar" style paths are NOT absolute on Windows
// (juce::File::isAbsolutePath needs a drive letter or leading backslash),
// so a path-based existence check would skip them and the test would
// falsely pass on Unix while reporting 0 removals on Windows.
juce::String makeAbsentAbsolutePath(const juce::String& filename) {
    auto path = testTempRoot()
                    .getChildFile("magda_prune_test_does_not_exist_" + juce::Uuid().toString())
                    .getChildFile(filename);
    REQUIRE(juce::File::isAbsolutePath(path.getFullPathName()));
    REQUIRE_FALSE(path.exists());
    return path.getFullPathName();
}

juce::PluginDescription makeDesc(const juce::String& name, const juce::String& fileOrId,
                                 const juce::String& format = "VST3") {
    juce::PluginDescription d;
    d.name = name;
    d.fileOrIdentifier = fileOrId;
    d.pluginFormatName = format;
    d.manufacturerName = "Test";
    d.uniqueId = name.hashCode();
    d.deprecatedUid = d.uniqueId;
    return d;
}

bool listContainsName(const juce::KnownPluginList& list, const juce::String& name) {
    for (int i = 0; i < list.getNumTypes(); ++i) {
        if (auto* d = list.getType(i); d && d->name == name)
            return true;
    }
    return false;
}

// Build a format manager with the formats we actually ship. The tracktion
// fork deletes addDefaultFormats(), so each format is registered explicitly.
void registerTestFormats(juce::AudioPluginFormatManager& fm) {
#if JUCE_PLUGINHOST_VST3
    fm.addFormat(std::make_unique<juce::VST3PluginFormat>());
#endif
#if JUCE_PLUGINHOST_AU && JUCE_MAC
    fm.addFormat(std::make_unique<juce::AudioUnitPluginFormat>());
#endif
}

}  // namespace

TEST_CASE("pruneMissingPlugins removes stale entries across formats", "[plugin][prune]") {
    // Real temp file stands in for an installed VST3.
    auto temp = createExistingPluginFile(".vst3");

    juce::AudioPluginFormatManager formatManager;
    registerTestFormats(formatManager);

    juce::KnownPluginList list;
    list.addType(makeDesc("ExistingVST3", temp.getFullPathName()));
    list.addType(makeDesc("MissingVST3", makeAbsentAbsolutePath("Missing.vst3")));
    // Bogus AU identifier: AudioComponentFindNext returns null for
    // unregistered OSTypes, so this must be pruned on macOS. On platforms
    // without AU support the format isn't registered and the entry is
    // treated as missing, which is the correct outcome either way.
    list.addType(makeDesc("MissingAU", "AudioUnit:Effects/fooo,fooo,fooo", "AudioUnit"));

    REQUIRE(list.getNumTypes() == 3);

    const int removed = TracktionEngineWrapper::pruneMissingPlugins(list, formatManager);

    CHECK(removed == 2);
    CHECK(list.getNumTypes() == 1);
    CHECK(listContainsName(list, "ExistingVST3"));
    CHECK_FALSE(listContainsName(list, "MissingVST3"));
    CHECK_FALSE(listContainsName(list, "MissingAU"));
}

TEST_CASE("pruneMissingPlugins is a no-op when nothing is stale", "[plugin][prune]") {
    auto temp = createExistingPluginFile(".vst3");

    juce::AudioPluginFormatManager formatManager;
    registerTestFormats(formatManager);

    juce::KnownPluginList list;
    list.addType(makeDesc("Existing", temp.getFullPathName()));

    const int removed = TracktionEngineWrapper::pruneMissingPlugins(list, formatManager);

    CHECK(removed == 0);
    CHECK(list.getNumTypes() == 1);
}

TEST_CASE("pruneMissingPlugins handles an empty list", "[plugin][prune]") {
    juce::AudioPluginFormatManager formatManager;
    registerTestFormats(formatManager);
    juce::KnownPluginList list;
    CHECK(TracktionEngineWrapper::pruneMissingPlugins(list, formatManager) == 0);
    CHECK(list.getNumTypes() == 0);
}
