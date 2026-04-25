// Investigative tests for issue #1005's "duplicate VST3 entries" follow-up.
//
// These don't assert the *desired* behaviour — they document what JUCE's
// KnownPluginList::addType actually does for each scenario a user is likely
// to call a "duplicate", so we can decide whether MAGDA needs a dedupe step
// or whether the user is looking at legitimately distinct entries.
//
// JUCE's dedupe key is (fileOrIdentifier, deprecatedUid, uniqueId). Anything
// that varies in any of those three is treated as a *new* plugin — even if
// the human-visible name is identical.

#include <juce_audio_processors/juce_audio_processors.h>

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/engine/TracktionEngineWrapper.hpp"

namespace {

juce::PluginDescription makeDesc(const juce::String& name, const juce::String& fileOrId,
                                 int uniqueId, const juce::String& format = "VST3") {
    juce::PluginDescription d;
    d.name = name;
    d.fileOrIdentifier = fileOrId;
    d.pluginFormatName = format;
    d.manufacturerName = "Test";
    d.uniqueId = uniqueId;
    d.deprecatedUid = uniqueId;
    return d;
}

int countByName(const juce::KnownPluginList& list, const juce::String& name) {
    int n = 0;
    for (int i = 0; i < list.getNumTypes(); ++i)
        if (auto* d = list.getType(i); d && d->name == name)
            ++n;
    return n;
}

}  // namespace

TEST_CASE("addType dedupes when fileOrIdentifier + uniqueId match", "[plugin][duplicates]") {
    juce::KnownPluginList list;
    list.addType(makeDesc("Serum", "/Library/Audio/Plug-Ins/VST3/Serum.vst3", 0xDEADBEEF));
    list.addType(makeDesc("Serum", "/Library/Audio/Plug-Ins/VST3/Serum.vst3", 0xDEADBEEF));

    CHECK(list.getNumTypes() == 1);
    CHECK(countByName(list, "Serum") == 1);
}

TEST_CASE("addType keeps both when uniqueId differs (uid migration)", "[plugin][duplicates]") {
    // Same plugin file on disk, different uniqueId — happens if the vendor
    // ships an update that changes the VST3 uid, or if JUCE's uid extraction
    // changed between MAGDA versions. The old entry is *not* a duplicate per
    // PluginDescription::isDuplicateOf, so addType inserts a second row and
    // the user sees "Serum" twice in the browser even though only one .vst3
    // is installed.
    juce::KnownPluginList list;
    list.addType(makeDesc("Serum", "/Library/Audio/Plug-Ins/VST3/Serum.vst3", 0xAAAA));
    list.addType(makeDesc("Serum", "/Library/Audio/Plug-Ins/VST3/Serum.vst3", 0xBBBB));

    CHECK(list.getNumTypes() == 2);
    CHECK(countByName(list, "Serum") == 2);
}

TEST_CASE("addType keeps both when fileOrIdentifier differs (system + user paths)",
          "[plugin][duplicates]") {
    // Same plugin installed in /Library *and* ~/Library — common for users
    // who installed a plugin without admin rights, then later got a
    // system-wide install from an updated installer. Both files exist, both
    // scan, both surface in the browser. Not a MAGDA bug — they are
    // genuinely two installations that JUCE has no way of knowing are the
    // same vendor's binary.
    juce::KnownPluginList list;
    list.addType(makeDesc("Serum", "/Library/Audio/Plug-Ins/VST3/Serum.vst3", 0xAAAA));
    list.addType(makeDesc("Serum", "/Users/me/Library/Audio/Plug-Ins/VST3/Serum.vst3", 0xAAAA));

    CHECK(list.getNumTypes() == 2);
    CHECK(countByName(list, "Serum") == 2);
}

TEST_CASE("addType keeps both for VST3 with multiple components", "[plugin][duplicates]") {
    // A single .vst3 bundle can expose multiple components — Kontakt Player
    // vs Kontakt, Vital + Vital FX, MeldaProduction MTotalFXBundle's many
    // sub-effects. JUCE stores them with the same fileOrIdentifier and
    // distinct uniqueIds. Looks like a duplicate but isn't; the user's
    // browser is correctly listing each entry point.
    juce::KnownPluginList list;
    list.addType(makeDesc("Vital", "/Library/Audio/Plug-Ins/VST3/Vital.vst3", 0x1111));
    list.addType(makeDesc("Vital", "/Library/Audio/Plug-Ins/VST3/Vital.vst3", 0x2222));

    CHECK(list.getNumTypes() == 2);
}

TEST_CASE("pruneMissingPlugins does not collapse name collisions", "[plugin][duplicates]") {
    // Confirm the prune step only filters by *existence* — it never inspects
    // names. So it cannot rescue the user from any of the duplicate-by-uid
    // or duplicate-by-path cases above; it only removes entries whose file
    // is missing.
    juce::TemporaryFile temp(".vst3");
    REQUIRE(temp.getFile().create().wasOk());
    const auto path = temp.getFile().getFullPathName();

    juce::AudioPluginFormatManager formatManager;
#if JUCE_PLUGINHOST_VST3
    formatManager.addFormat(std::make_unique<juce::VST3PluginFormat>());
#endif

    juce::KnownPluginList list;
    list.addType(makeDesc("Serum", path, 0xAAAA));
    list.addType(makeDesc("Serum", path, 0xBBBB));

    REQUIRE(list.getNumTypes() == 2);
    const int removed = magda::TracktionEngineWrapper::pruneMissingPlugins(list, formatManager);

    CHECK(removed == 0);
    CHECK(list.getNumTypes() == 2);
}

TEST_CASE("XML round-trip preserves duplicates (persist doesn't dedupe)", "[plugin][duplicates]") {
    // PluginList.xml is what survives across launches. If JUCE collapsed
    // entries on createXml/recreateFromXml the user could "fix" the
    // duplicates with a relaunch — they can't. The XML serialiser is a
    // 1:1 mirror of the in-memory list.
    juce::KnownPluginList original;
    original.addType(makeDesc("Serum", "/Library/Audio/Plug-Ins/VST3/Serum.vst3", 0xAAAA));
    original.addType(makeDesc("Serum", "/Library/Audio/Plug-Ins/VST3/Serum.vst3", 0xBBBB));
    REQUIRE(original.getNumTypes() == 2);

    auto xml = original.createXml();
    REQUIRE(xml != nullptr);

    juce::KnownPluginList restored;
    restored.recreateFromXml(*xml);

    CHECK(restored.getNumTypes() == 2);
    CHECK(countByName(restored, "Serum") == 2);
}

TEST_CASE("addType keeps both for same plugin in VST3 and AU formats", "[plugin][duplicates]") {
    // Many plugins ship both VST3 and AU on macOS. Each format gets its own
    // entry — same name, distinct fileOrIdentifier (path vs AU identifier
    // string), distinct format. This is the user-perceived "duplicate" that
    // is *always* expected and never a bug.
    juce::KnownPluginList list;
    list.addType(makeDesc("Massive X", "/Library/Audio/Plug-Ins/VST3/Massive X.vst3", 0xAAAA));
    list.addType(makeDesc("Massive X", "AudioUnit:Effects/aumu,MaXX,NIcm", 0xAAAA, "AudioUnit"));

    CHECK(list.getNumTypes() == 2);
    CHECK(countByName(list, "Massive X") == 2);
}

TEST_CASE("addType is idempotent on a re-scan", "[plugin][duplicates]") {
    // Detecting "new" plugins re-runs the scan and feeds every found
    // PluginDescription back into addType. As long as fileOrIdentifier +
    // uniqueId are stable across runs (the normal case) the list size is
    // unchanged after a re-scan — JUCE's dedupe path replaces in place.
    juce::KnownPluginList list;
    list.addType(makeDesc("Serum", "/Library/Audio/Plug-Ins/VST3/Serum.vst3", 0xAAAA));
    list.addType(
        makeDesc("Pro-Q 4", "/Library/Audio/Plug-Ins/VST3/FabFilter Pro-Q 4.vst3", 0xBBBB));
    REQUIRE(list.getNumTypes() == 2);

    // Simulate a second scan returning the same descriptions.
    list.addType(makeDesc("Serum", "/Library/Audio/Plug-Ins/VST3/Serum.vst3", 0xAAAA));
    list.addType(
        makeDesc("Pro-Q 4", "/Library/Audio/Plug-Ins/VST3/FabFilter Pro-Q 4.vst3", 0xBBBB));

    CHECK(list.getNumTypes() == 2);
    CHECK(countByName(list, "Serum") == 1);
    CHECK(countByName(list, "Pro-Q 4") == 1);
}

TEST_CASE("addType treats path case differences as distinct on a case-sensitive comparator",
          "[plugin][duplicates]") {
    // PluginDescription::isDuplicateOf does a plain String == on
    // fileOrIdentifier, so "/Library/..." and "/library/..." are different
    // even on macOS where the filesystem itself is case-insensitive. If a
    // user's plugin entry was scanned once with one casing and re-scanned
    // with another (e.g. an installer wrote a different cased path), the
    // browser would show two rows. Documents the behaviour; doesn't
    // currently affect anyone in practice.
    juce::KnownPluginList list;
    list.addType(makeDesc("Serum", "/Library/Audio/Plug-Ins/VST3/Serum.vst3", 0xAAAA));
    list.addType(makeDesc("Serum", "/library/audio/plug-ins/vst3/serum.vst3", 0xAAAA));

    CHECK(list.getNumTypes() == 2);
}

// =============================================================================
// removeSupersededEntries — the actual #1005 fix
// =============================================================================

TEST_CASE("removeSupersededEntries drops old uid when vendor bumps it", "[plugin][duplicates]") {
    // Same .vst3 path, vendor shipped a new release with a different
    // uniqueId. After a rescan we want only the fresh uid to remain.
    juce::KnownPluginList list;
    const juce::String path = "/Library/Audio/Plug-Ins/VST3/Nebula De-Esser.vst3";
    list.addType(makeDesc("Nebula De-Esser", path, 0xAAAA));  // old install

    juce::Array<juce::PluginDescription> freshScan;
    freshScan.add(makeDesc("Nebula De-Esser", path, 0xBBBB));  // new install

    const int removed = magda::TracktionEngineWrapper::removeSupersededEntries(list, freshScan);

    CHECK(removed == 1);
    CHECK(list.getNumTypes() == 0);  // fresh entry isn't added by this helper
}

TEST_CASE("removeSupersededEntries keeps multi-component VST3 entries", "[plugin][duplicates]") {
    // Vital exposes two uids from one .vst3 bundle. Both must survive a
    // rescan — they're not duplicates, they're separate entry points.
    juce::KnownPluginList list;
    const juce::String path = "/Library/Audio/Plug-Ins/VST3/Vital.vst3";
    list.addType(makeDesc("Vital", path, 0x1111));
    list.addType(makeDesc("Vital", path, 0x2222));

    juce::Array<juce::PluginDescription> freshScan;
    freshScan.add(makeDesc("Vital", path, 0x1111));
    freshScan.add(makeDesc("Vital", path, 0x2222));

    const int removed = magda::TracktionEngineWrapper::removeSupersededEntries(list, freshScan);

    CHECK(removed == 0);
    CHECK(list.getNumTypes() == 2);
}

TEST_CASE("removeSupersededEntries keeps system + user installs", "[plugin][duplicates]") {
    // Two genuinely separate .vst3 files on disk — different paths, same
    // name. Both surface in the fresh scan and both must remain.
    juce::KnownPluginList list;
    list.addType(makeDesc("Serum", "/Library/Audio/Plug-Ins/VST3/Serum.vst3", 0xAAAA));
    list.addType(makeDesc("Serum", "/Users/me/Library/Audio/Plug-Ins/VST3/Serum.vst3", 0xAAAA));

    juce::Array<juce::PluginDescription> freshScan;
    freshScan.add(makeDesc("Serum", "/Library/Audio/Plug-Ins/VST3/Serum.vst3", 0xAAAA));
    freshScan.add(makeDesc("Serum", "/Users/me/Library/Audio/Plug-Ins/VST3/Serum.vst3", 0xAAAA));

    const int removed = magda::TracktionEngineWrapper::removeSupersededEntries(list, freshScan);

    CHECK(removed == 0);
    CHECK(list.getNumTypes() == 2);
}

TEST_CASE("removeSupersededEntries keeps cross-format VST3 + AU entries", "[plugin][duplicates]") {
    // Same plugin shipped as VST3 and AU. Different (path, format) keys, so
    // each is independent.
    juce::KnownPluginList list;
    list.addType(makeDesc("Massive X", "/Library/Audio/Plug-Ins/VST3/Massive X.vst3", 0xAAAA));
    list.addType(makeDesc("Massive X", "AudioUnit:Effects/aumu,MaXX,NIcm", 0xAAAA, "AudioUnit"));

    juce::Array<juce::PluginDescription> freshScan;
    freshScan.add(makeDesc("Massive X", "/Library/Audio/Plug-Ins/VST3/Massive X.vst3", 0xAAAA));
    freshScan.add(makeDesc("Massive X", "AudioUnit:Effects/aumu,MaXX,NIcm", 0xAAAA, "AudioUnit"));

    const int removed = magda::TracktionEngineWrapper::removeSupersededEntries(list, freshScan);

    CHECK(removed == 0);
    CHECK(list.getNumTypes() == 2);
}

TEST_CASE("removeSupersededEntries leaves entries whose path was not rescanned",
          "[plugin][duplicates]") {
    // Excluded plugins, or formats not enabled this run, won't appear in
    // freshScan. Their entries must survive — we only collapse paths the
    // current scan actually visited.
    juce::KnownPluginList list;
    list.addType(makeDesc("Serum", "/Library/Audio/Plug-Ins/VST3/Serum.vst3", 0xAAAA));
    list.addType(makeDesc("Excluded", "/Library/Audio/Plug-Ins/VST3/Excluded.vst3", 0xBBBB));

    juce::Array<juce::PluginDescription> freshScan;
    freshScan.add(makeDesc("Serum", "/Library/Audio/Plug-Ins/VST3/Serum.vst3", 0xAAAA));

    const int removed = magda::TracktionEngineWrapper::removeSupersededEntries(list, freshScan);

    CHECK(removed == 0);
    CHECK(list.getNumTypes() == 2);
}
