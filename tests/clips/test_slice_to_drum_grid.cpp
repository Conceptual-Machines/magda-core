// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_test_macros.hpp>

#include "AudioClipTestHelpers.hpp"
#include "magda/daw/core/ClipCommands.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

using namespace magda;

namespace {

juce::File writeSilentWav(double seconds) {
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getNonexistentChildFile("magda_slice_drum_grid", ".wav");
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(new juce::FileOutputStream(file), 48000.0, 1, 16, {}, 0));
    juce::AudioBuffer<float> buffer(1, static_cast<int>(seconds * 48000.0));
    buffer.clear();
    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    return file;
}

}  // namespace

TEST_CASE("Slicing at warp markers to a Drum Grid builds the grid", "[clips][drumgrid]") {
    ClipManager::getInstance().clearAllClips();
    TrackManager::getInstance().clearAllTracks();
    SelectionManager::getInstance().clearSelection();
    UndoManager::getInstance().clearHistory();

    const auto wav = writeSilentWav(4.0);
    const auto trackId = TrackManager::getInstance().createTrack("Audio");
    const auto clipId =
        ClipManager::getInstance().createAudioClipBeats(trackId, 0.0, 8.0, wav.getFullPathName());
    auto* clip = ClipManager::getInstance().getClip(clipId);
    REQUIRE(clip != nullptr);
    auto& event = *primaryEventOf(clip);
    SourcePool::getInstance().seedFactsForTesting(wav.getFullPathName(), 4.0,
                                                  test::kTestSourceSampleRate);
    SourcePool::getInstance().resolveFacts(event.sourceId);
    event.speedRatio = 1.0;
    event.warpMarkers = {{0.0, 0.0}, {1.0, 1.0}, {2.0, 2.0}, {4.0, 4.0}};

    // The Drum Grid's device was read through a pointer the pad edits had freed.
    sliceWarpMarkersToDrumGrid(clipId, 120.0);

    std::vector<ClipInfo> midiClips;
    for (const auto& c : ClipManager::getInstance().getClips())
        if (c.isMidi())
            midiClips.push_back(c);
    REQUIRE(midiClips.size() == 1);
    CHECK(midiClips.front().midiNotes.size() == 3);

    ClipManager::getInstance().clearAllClips();
    TrackManager::getInstance().clearAllTracks();
    wav.deleteFile();
}
