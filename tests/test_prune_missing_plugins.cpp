#include <juce_audio_processors/juce_audio_processors.h>

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/engine/TracktionEngineWrapper.hpp"

using magda::TracktionEngineWrapper;

namespace {

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

}  // namespace

TEST_CASE("pruneMissingPlugins removes only missing absolute-path entries", "[plugin][prune]") {
    // Create a real temp file to stand in for an existing plugin.
    juce::TemporaryFile temp(".vst3");
    REQUIRE(temp.getFile().create().wasOk());

    juce::KnownPluginList list;
    list.addType(makeDesc("Existing", temp.getFile().getFullPathName()));
    list.addType(makeDesc("Missing", "/definitely/not/a/real/path/Missing.vst3"));
    list.addType(makeDesc("AURelative", "AudioUnit:Apple:fooo,fooo,fooo", "AudioUnit"));

    REQUIRE(list.getNumTypes() == 3);

    const int removed = TracktionEngineWrapper::pruneMissingPlugins(list);

    CHECK(removed == 1);
    CHECK(list.getNumTypes() == 2);
    CHECK(listContainsName(list, "Existing"));
    CHECK_FALSE(listContainsName(list, "Missing"));
    CHECK(listContainsName(list, "AURelative"));
}

TEST_CASE("pruneMissingPlugins is a no-op when nothing is stale", "[plugin][prune]") {
    juce::TemporaryFile temp(".vst3");
    REQUIRE(temp.getFile().create().wasOk());

    juce::KnownPluginList list;
    list.addType(makeDesc("Existing", temp.getFile().getFullPathName()));
    list.addType(makeDesc("AURelative", "AudioUnit:Apple:fooo,fooo,fooo", "AudioUnit"));

    const int removed = TracktionEngineWrapper::pruneMissingPlugins(list);

    CHECK(removed == 0);
    CHECK(list.getNumTypes() == 2);
}

TEST_CASE("pruneMissingPlugins handles an empty list", "[plugin][prune]") {
    juce::KnownPluginList list;
    CHECK(TracktionEngineWrapper::pruneMissingPlugins(list) == 0);
    CHECK(list.getNumTypes() == 0);
}
