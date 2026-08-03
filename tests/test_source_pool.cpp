#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <unordered_set>

#include "AudioClipTestHelpers.hpp"
#include "core/ClipManager.hpp"
#include "core/SourcePool.hpp"

using namespace magda;
using Catch::Approx;

namespace {

struct PoolFixture {
    PoolFixture() {
        SourcePool::getInstance().clear();
        SourcePool::getInstance().clearSeededFactsForTesting();
    }
    ~PoolFixture() {
        SourcePool::getInstance().clear();
        SourcePool::getInstance().clearSeededFactsForTesting();
    }
};

/// Write a real WAV so the pool's probe path can be exercised end to end.
/// Returns the file; the caller deletes it.
juce::File writeTempWav(const juce::String& name, double sampleRate, int lengthSamples) {
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("magda_source_pool_" + name + ".wav");
    file.deleteFile();

    juce::WavAudioFormat format;
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr)
        return {};

    std::unique_ptr<juce::AudioFormatWriter> writer(
        format.createWriterFor(stream.get(), sampleRate, 1, 16, {}, 0));
    if (writer == nullptr)
        return {};
    stream.release();  // the writer owns it now

    juce::AudioBuffer<float> silence(1, lengthSamples);
    silence.clear();
    writer->writeFromAudioSampleBuffer(silence, 0, lengthSamples);
    writer.reset();

    return file;
}

}  // namespace

TEST_CASE("SourcePool deduplicates by file", "[source][pool]") {
    PoolFixture fixture;
    auto& pool = SourcePool::getInstance();

    SECTION("The same path yields the same id") {
        const auto first = pool.acquire("/tmp/kick.wav");
        const auto second = pool.acquire("/tmp/kick.wav");
        REQUIRE(first != INVALID_SOURCE_ID);
        REQUIRE(first == second);
        REQUIRE(pool.snapshot().size() == 1);
    }

    SECTION("Different paths yield different ids") {
        REQUIRE(pool.acquire("/tmp/kick.wav") != pool.acquire("/tmp/snare.wav"));
        REQUIRE(pool.snapshot().size() == 2);
    }

    SECTION("An empty path is not a source") {
        REQUIRE(pool.acquire("") == INVALID_SOURCE_ID);
        REQUIRE(pool.snapshot().empty());
    }

    SECTION("findByPath sees what acquire created, and nothing else") {
        const auto id = pool.acquire("/tmp/kick.wav");
        REQUIRE(pool.findByPath("/tmp/kick.wav") == id);
        REQUIRE(pool.findByPath("/tmp/never-seen.wav") == INVALID_SOURCE_ID);
    }

#if JUCE_MAC || JUCE_WINDOWS
    SECTION("Case-insensitive platforms treat one file as one source") {
        const auto lower = pool.acquire("/tmp/Kick.wav");
        const auto upper = pool.acquire("/tmp/KICK.WAV");
        REQUIRE(lower == upper);
        REQUIRE(pool.snapshot().size() == 1);
    }
#endif

    SECTION("A relative path keys on itself rather than the working directory") {
        // Resolving it against the CWD would make the same project mean
        // different files depending on where the app was launched from.
        const auto id = pool.acquire("relative/loop.wav");
        REQUIRE(id != INVALID_SOURCE_ID);
        REQUIRE(pool.get(id)->filePath == "relative/loop.wav");
    }
}

TEST_CASE("SourcePool insert preserves ids from a project file", "[source][pool][serialization]") {
    PoolFixture fixture;
    auto& pool = SourcePool::getInstance();

    Source restored;
    restored.id = 7;
    restored.filePath = "/tmp/restored.wav";
    restored.durationSeconds = 3.5;
    restored.sampleRate = 44100.0;

    REQUIRE(pool.insert(restored) == 7);
    REQUIRE(pool.get(7)->durationSeconds == Approx(3.5));

    SECTION("Later acquires do not collide with a restored id") {
        REQUIRE(pool.acquire("/tmp/other.wav") > 7);
    }

    SECTION("Re-inserting the same file keeps the first entry") {
        Source duplicate = restored;
        duplicate.id = 99;
        duplicate.durationSeconds = 999.0;
        REQUIRE(pool.insert(duplicate) == 7);
        REQUIRE(pool.get(7)->durationSeconds == Approx(3.5));
        REQUIRE(pool.get(99) == nullptr);
    }
}

TEST_CASE("SourcePool garbage collects unreferenced sources", "[source][pool]") {
    PoolFixture fixture;
    auto& pool = SourcePool::getInstance();

    const auto kept = pool.acquire("/tmp/kept.wav");
    const auto dropped = pool.acquire("/tmp/dropped.wav");

    pool.retainOnly({kept});

    REQUIRE(pool.get(kept) != nullptr);
    REQUIRE(pool.get(dropped) == nullptr);
    REQUIRE(pool.snapshot().size() == 1);

    SECTION("A dropped path can be re-acquired afterwards") {
        // The path index has to be cleaned up with the entry, or the file
        // becomes permanently unreachable.
        const auto reacquired = pool.acquire("/tmp/dropped.wav");
        REQUIRE(reacquired != INVALID_SOURCE_ID);
        REQUIRE(pool.get(reacquired) != nullptr);
    }
}

TEST_CASE("SourcePool relink repoints every clip sharing the file", "[source][pool]") {
    PoolFixture fixture;
    auto& pool = SourcePool::getInstance();

    const auto id = pool.acquire("/tmp/before.wav");
    pool.relink(id, "/tmp/after.wav");

    REQUIRE(pool.get(id)->filePath == "/tmp/after.wav");
    REQUIRE(pool.findByPath("/tmp/after.wav") == id);
    REQUIRE(pool.findByPath("/tmp/before.wav") == INVALID_SOURCE_ID);
}

TEST_CASE("SourcePool probes real files for their facts", "[source][pool][probe]") {
    PoolFixture fixture;
    auto& pool = SourcePool::getInstance();

    constexpr double kRate = 44100.0;
    constexpr int kSamples = 22050;  // half a second
    const auto file = writeTempWav("probe", kRate, kSamples);
    REQUIRE(file.existsAsFile());

    SECTION("acquire reads sample rate and duration off disk") {
        const auto id = pool.acquire(file.getFullPathName());
        const auto* source = pool.get(id);
        REQUIRE(source != nullptr);
        REQUIRE(source->isResolved());
        REQUIRE(source->sampleRate == Approx(kRate));
        REQUIRE(source->durationSeconds == Approx(0.5));
    }

    SECTION("A missing file stays unresolved and reports the nominal rate") {
        const auto id = pool.acquire("/tmp/magda_definitely_missing.wav");
        const auto* source = pool.get(id);
        REQUIRE(source != nullptr);
        REQUIRE_FALSE(source->isResolved());
        REQUIRE(source->sampleRate == Approx(0.0));
        REQUIRE(source->effectiveSampleRate() == Approx(kUnresolvedSourceSampleRate));
    }

    SECTION("resolveFacts reports the unresolved-to-resolved transition once") {
        // That transition is what tells the model to rescale anchors computed
        // at the nominal rate, so it must fire exactly once.
        Source offline;
        offline.id = 1;
        offline.filePath = file.getFullPathName();
        REQUIRE(pool.insert(offline) == 1);
        REQUIRE_FALSE(pool.get(1)->isResolved());

        REQUIRE(pool.resolveFacts(1));
        REQUIRE(pool.get(1)->isResolved());
        REQUIRE(pool.get(1)->sampleRate == Approx(kRate));

        REQUIRE_FALSE(pool.resolveFacts(1));
    }

    SECTION("relink onto a real file resolves a previously missing source") {
        const auto id = pool.acquire("/tmp/magda_definitely_missing.wav");
        REQUIRE_FALSE(pool.get(id)->isResolved());
        REQUIRE(pool.relink(id, file.getFullPathName()));
        REQUIRE(pool.get(id)->sampleRate == Approx(kRate));
    }

    file.deleteFile();
}

TEST_CASE("Source converts between its own samples and seconds", "[source][pool]") {
    Source source;
    source.sampleRate = 48000.0;

    REQUIRE(source.secondsToSamples(0.5) == 24000);
    REQUIRE(source.samplesToSeconds(24000) == Approx(0.5));

    SECTION("An unresolved source falls back to the nominal rate") {
        Source unresolved;
        REQUIRE(unresolved.secondsToSamples(1.0) ==
                static_cast<int64_t>(kUnresolvedSourceSampleRate));
    }
}

TEST_CASE("Resolving a source rescales the anchors computed at the nominal rate",
          "[source][pool][rescale]") {
    // Anchors are sample counts at the source's own rate. While the file is
    // missing the pool reports kUnresolvedSourceSampleRate, so everything
    // computed then is expressed at 48 kHz. Resolving a 44.1 kHz file without
    // rescaling would reinterpret every one of those counts and shift the clip
    // by about 9%.
    PoolFixture fixture;
    auto& pool = SourcePool::getInstance();
    auto& cm = ClipManager::getInstance();
    cm.clearAllClips();

    constexpr double kRealRate = 44100.0;
    const auto file = writeTempWav("rescale", kRealRate, 44100);
    REQUIRE(file.existsAsFile());

    // A source that is not resolved yet: insert() does not probe.
    Source offline;
    offline.id = 1;
    offline.filePath = file.getFullPathName();
    REQUIRE(pool.insert(offline) == 1);
    REQUIRE_FALSE(pool.get(1)->isResolved());

    const auto clipId = cm.createMidiClipBeats(1, 0.0, 4.0, ClipView::Arrangement);
    auto* clip = cm.getClip(clipId);
    REQUIRE(clip != nullptr);
    clip->setAudioContent();
    AudioEvent seed;
    seed.sourceId = 1;
    auto& event = clip->audio().addEvent(std::move(seed));

    // Half a second in, at the only rate anyone can know about yet.
    event.setAnchorSeconds(0.5);
    event.setLoopStartSeconds(0.25);
    event.setLoopLengthSeconds(0.5);
    REQUIRE(event.sourceAnchorSamples == 24000);

    REQUIRE(pool.resolveFacts(1));
    REQUIRE(pool.get(1)->sampleRate == Approx(kRealRate));

    // Same instant in the file, expressed at the rate it actually runs at.
    auto* rescaled = cm.getClip(clipId)->primaryEvent();
    REQUIRE(rescaled != nullptr);
    REQUIRE(rescaled->sourceAnchorSamples == 22050);
    REQUIRE(rescaled->loopStartSamples == 11025);
    REQUIRE(rescaled->loopLengthSamples == 22050);
    REQUIRE(rescaled->anchorSeconds() == Approx(0.5));
    REQUIRE(rescaled->loopStartSeconds() == Approx(0.25));
    REQUIRE(rescaled->loopLengthSeconds() == Approx(0.5));

    cm.clearAllClips();
    file.deleteFile();
}

TEST_CASE("Relinking to a file at another rate keeps positions in time",
          "[source][pool][rescale]") {
    // The 0 -> real transition is not the only rate change: a resolved source
    // relinked onto a file at a different rate moves just as far.
    PoolFixture fixture;
    auto& pool = SourcePool::getInstance();
    auto& cm = ClipManager::getInstance();
    cm.clearAllClips();

    const auto at48k = writeTempWav("relink_48k", 48000.0, 48000);
    const auto at44k = writeTempWav("relink_44k", 44100.0, 44100);
    REQUIRE(at48k.existsAsFile());
    REQUIRE(at44k.existsAsFile());

    const auto id = pool.acquire(at48k.getFullPathName());
    REQUIRE(pool.get(id)->sampleRate == Approx(48000.0));

    const auto clipId = cm.createMidiClipBeats(1, 0.0, 4.0, ClipView::Arrangement);
    auto* clip = cm.getClip(clipId);
    clip->setAudioContent();
    AudioEvent seed;
    seed.sourceId = id;
    auto& event = clip->audio().addEvent(std::move(seed));
    event.setAnchorSeconds(0.5);
    REQUIRE(event.sourceAnchorSamples == 24000);

    REQUIRE(pool.relink(id, at44k.getFullPathName()) == id);

    auto* rescaled = cm.getClip(clipId)->primaryEvent();
    REQUIRE(rescaled->sourceAnchorSamples == 22050);
    REQUIRE(rescaled->anchorSeconds() == Approx(0.5));

    cm.clearAllClips();
    at48k.deleteFile();
    at44k.deleteFile();
}

TEST_CASE("Relinking onto an already pooled file does not steal its key", "[source][pool]") {
    // insert() guards this case; relink() has to as well, or two sources end
    // up claiming one file with only one of them reachable by path.
    PoolFixture fixture;
    auto& pool = SourcePool::getInstance();

    const auto first = pool.acquire("/tmp/one.wav");
    const auto second = pool.acquire("/tmp/two.wav");
    REQUIRE(first != second);

    // The existing owner comes back, and nothing moved.
    REQUIRE(pool.relink(second, "/tmp/one.wav") == first);
    REQUIRE(pool.findByPath("/tmp/one.wav") == first);
    REQUIRE(pool.get(second)->filePath == "/tmp/two.wav");
    REQUIRE(pool.findByPath("/tmp/two.wav") == second);
}
