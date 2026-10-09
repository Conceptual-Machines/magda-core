#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <cmath>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/audio/AudioThumbnailManager.hpp"
#include "magda/daw/audio/TransientDetection.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/WarpMarkerCommands.hpp"

using namespace magda;

// Without the fork rendering, transients come from the native detector and WARP seeds them (#3017).
class NativeTransientDetectionTest final : public juce::UnitTest, private TransientCacheListener {
  public:
    NativeTransientDetectionTest() : juce::UnitTest("Native Transient Detection", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState([this] { testWarpSeedsDetectedTransients(); });
    }

  private:
    static constexpr double kRate = 44100.0;
    static constexpr double kSpacing = 0.5;

    void transientsChanged(const juce::String& filePath) override {
        if (filePath == watchedPath_ &&
            AudioThumbnailManager::getInstance().getCachedTransients(filePath) != nullptr)
            cached_ = true;
    }

    /// Short bursts every kSpacing seconds over silence.
    static juce::File writeBursts() {
        auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile("magda_native_transients.wav");
        file.deleteFile();
        const int length = static_cast<int>(4.0 * kRate);
        juce::AudioBuffer<float> buffer(1, length);
        buffer.clear();
        for (int at = 0; at < length; ++at)
            if (std::fmod(at / kRate, kSpacing) < 0.01)
                buffer.setSample(0, at, 0.5f * static_cast<float>(std::sin(at * 0.14)));

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions()
                                                      .withSampleRate(kRate)
                                                      .withNumChannels(1)
                                                      .withBitsPerSample(32));
        writer->writeFromAudioSampleBuffer(buffer, 0, length);
        return file;
    }

    void testWarpSeedsDetectedTransients() {
        beginTest("WARP seeds markers at natively detected transients");

        auto& clips = ClipManager::getInstance();
        clips.clearAllClips();
        const auto file = writeBursts();
        watchedPath_ = file.getFullPathName();

        const auto trackId = TrackManager::getInstance().createTrack("Audio");
        const auto clipId = clips.createAudioClipBeats(trackId, 0.0, 8.0, watchedPath_);
        expect(clipId != INVALID_CLIP_ID);

        auto& thumbnails = AudioThumbnailManager::getInstance();
        thumbnails.addTransientCacheListener(this);
        expect(!transients::detect(clipId), "nothing is cached before detection runs");

        // Bounded by the detection signal, not a sleep: each turn dispatches pending messages.
        for (int turn = 0; turn < 500 && !cached_; ++turn)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        thumbnails.removeTransientCacheListener(this);

        expect(cached_, "native detection never reached the cache");
        expect(transients::detect(clipId));

        const auto* times = thumbnails.getCachedTransients(watchedPath_);
        expect(times != nullptr && times->size() == 8);
        if (times != nullptr)
            for (int i = 0; i < times->size(); ++i)
                expectWithinAbsoluteError((*times)[i], i * kSpacing, 0.02);

        clips.setClipWarpEnabled(clipId, true);
        seedWarpMarkersFromTransients(clipId, 120.0);
        const auto markers = getClipWarpMarkers(clipId);
        // Two boundaries plus every burst after the first, which sits on the start boundary.
        expectEquals(static_cast<int>(markers.size()), 9);

        clips.clearAllClips();
        thumbnails.clearCachedTransients(watchedPath_);
        file.deleteFile();
    }

    juce::String watchedPath_;
    bool cached_ = false;
};

static NativeTransientDetectionTest nativeTransientDetectionTest;
