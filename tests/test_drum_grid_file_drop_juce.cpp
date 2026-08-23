#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/ui/components/tracks/TrackContentPanel.hpp"
#include "magda/daw/ui/state/TimelineController.hpp"

/**
 * Dropping a file on a Drum Grid track (#2172)
 *
 * The panel used to refuse every file dropped on a track whose chain held a
 * Drum Grid, so the track most likely to want a .mid was the one that would
 * not take one. The guard was written for the audio-only importer and
 * generalised from "Audio files" to "Files" when MIDI import landed (#923),
 * rather than being dropped: a track is hybrid, and no other path in the app
 * agreed with it. The same clip could always be dragged onto the track from a
 * neighbour, which is the workaround the reporter found.
 *
 * Both kinds are asserted because the bug refused both, and because the fix is
 * the absence of a rule rather than a narrower one. What decides a drop now is
 * TrackInfo::canHostClips, which asks about the track and never about the file.
 *
 * Driven through the panel's real `filesDropped`: the refusal was never in the
 * import path underneath, so a test below the drop would have passed
 * throughout.
 */

using namespace magda;

namespace {

constexpr double testTempoBPM = 120.0;
constexpr double testZoomPixelsPerBeat = 40.0;

juce::File scratchDirectory() {
    auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("magda_drum_grid_drop");
    directory.createDirectory();
    return directory;
}

/// A one-note .mid, generated rather than checked in.
juce::File writeSingleNoteMidiFile() {
    const auto file = scratchDirectory().getChildFile("drop.mid");
    file.deleteFile();

    juce::MidiMessageSequence sequence;
    sequence.addEvent(juce::MidiMessage::noteOn(1, 36, 0.8f), 0.0);
    sequence.addEvent(juce::MidiMessage::noteOff(1, 36), 960.0);

    juce::MidiFile midi;
    midi.setTicksPerQuarterNote(960);
    midi.addTrack(sequence);

    if (auto stream = std::unique_ptr<juce::FileOutputStream>(file.createOutputStream()))
        midi.writeTo(*stream);

    return file;
}

/// Half a second of a quiet tone. Only its existence and duration matter here.
juce::File writeShortWavFile() {
    const auto file = scratchDirectory().getChildFile("drop.wav");
    file.deleteFile();

    constexpr double sampleRate = 44100.0;
    constexpr int numSamples = 22050;

    juce::AudioBuffer<float> buffer(1, numSamples);
    for (int sample = 0; sample < numSamples; ++sample)
        buffer.setSample(0, sample,
                         0.25f *
                             std::sin(juce::MathConstants<float>::twoPi * 220.0f *
                                      static_cast<float>(sample) / static_cast<float>(sampleRate)));

    juce::WavAudioFormat format;
    if (auto stream = std::unique_ptr<juce::OutputStream>(file.createOutputStream())) {
        const auto options = juce::AudioFormatWriterOptions{}
                                 .withSampleRate(sampleRate)
                                 .withNumChannels(1)
                                 .withBitsPerSample(16);
        if (auto writer = format.createWriterFor(stream, options))
            writer->writeFromAudioSampleBuffer(buffer, 0, numSamples);
    }

    return file;
}

/// A single Drum Grid track, built through the app's own device path.
struct DrumGridFixture {
    TimelineController controller;
    TrackContentPanel panel;
    TrackId trackId = INVALID_TRACK_ID;

    DrumGridFixture() {
        auto& trackManager = TrackManager::getInstance();
        trackId = trackManager.createTrack("Drums", TrackType::Audio);

        DeviceInfo drumGrid;
        drumGrid.name = "Drum Grid";
        drumGrid.pluginId = "drumgrid";
        drumGrid.format = PluginFormat::Internal;
        drumGrid.isInstrument = true;
        trackManager.addDeviceToTrack(trackId, drumGrid);

        panel.setSize(2000, 400);
        panel.setTempo(testTempoBPM);
        panel.setZoom(testZoomPixelsPerBeat);
        panel.setController(&controller);
    }
};

}  // namespace

class DrumGridFileDropTest final : public juce::UnitTest {
  public:
    DrumGridFileDropTest() : juce::UnitTest("Drum Grid File Drop", "magda") {}

    void runTest() override {
        testMidiFileLandsOnTheDrumGridTrackItWasDroppedOn();
        testAudioFileLandsOnTheDrumGridTrackItWasDroppedOn();
    }

  private:
    /// The reported bug.
    void testMidiFileLandsOnTheDrumGridTrackItWasDroppedOn() {
        beginTest("A .mid dropped on a Drum Grid track becomes a MIDI clip on that track");

        magda::test::runWithCleanJuceState([this] {
            DrumGridFixture fixture;
            expect(fixture.trackId != INVALID_TRACK_ID, "the fixture built no track");

            const auto midiFile = writeSingleNoteMidiFile();
            expect(midiFile.existsAsFile(), "no .mid was written to drop");

            // y inside the first track's own lane, x a little way along the
            // timeline: a drop lands where the pointer is, so both matter.
            fixture.panel.filesDropped({midiFile.getFullPathName()}, 200, 10);

            const auto clips = ClipManager::getInstance().getClipsOnTrack(fixture.trackId);
            expect(clips.size() == 1, "expected one clip on the Drum Grid track, got " +
                                          juce::String((int)clips.size()));

            if (!clips.empty()) {
                const auto* clip = ClipManager::getInstance().getClip(clips.front());
                expect(clip != nullptr, "a clip id with no clip behind it");
                if (clip != nullptr)
                    expect(clip->isMidi(), "the drop created a clip that is not MIDI");
            }
        });
    }

    /// The same gate refused this too, so its removal is asserted for both.
    void testAudioFileLandsOnTheDrumGridTrackItWasDroppedOn() {
        beginTest("A .wav dropped on a Drum Grid track becomes an audio clip on that track");

        magda::test::runWithCleanJuceState([this] {
            DrumGridFixture fixture;
            expect(fixture.trackId != INVALID_TRACK_ID, "the fixture built no track");

            const auto wavFile = writeShortWavFile();
            expect(wavFile.existsAsFile(), "no .wav was written to drop");

            fixture.panel.filesDropped({wavFile.getFullPathName()}, 200, 10);

            const auto clips = ClipManager::getInstance().getClipsOnTrack(fixture.trackId);
            expect(clips.size() == 1, "expected one clip on the Drum Grid track, got " +
                                          juce::String((int)clips.size()));

            if (!clips.empty()) {
                const auto* clip = ClipManager::getInstance().getClip(clips.front());
                expect(clip != nullptr, "a clip id with no clip behind it");
                if (clip != nullptr)
                    expect(!clip->isMidi(), "the drop created a MIDI clip from a .wav");
            }
        });
    }
};

static DrumGridFileDropTest drumGridFileDropTest;
