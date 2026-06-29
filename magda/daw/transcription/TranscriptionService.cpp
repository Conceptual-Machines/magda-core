// See TranscriptionService.hpp.

#include "TranscriptionService.hpp"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <vector>

#include "BasicPitchTranscriber.hpp"
#include "ClipCommands.hpp"
#include "ClipInfo.hpp"
#include "ClipManager.hpp"
#include "DeviceInfo.hpp"
#include "MidiNoteCommands.hpp"
#include "TempoUtils.hpp"
#include "TrackCommands.hpp"
#include "TrackPropertyCommands.hpp"
#include "UndoManager.hpp"
#include "ui/state/TimelineController.hpp"

namespace magda::transcription {

namespace {

// Resolve the bundled basic_pitch.onnx, mirroring LuaScriptStore's exe-adjacent
// resource walk (Contents/Resources on mac, exe-adjacent elsewhere, plus a
// dev-tree resources/ fallback).
juce::File findBundledModel() {
    auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
    const juce::String rel = "models/basic_pitch.onnx";

    juce::Array<juce::File> candidates;
#if JUCE_MAC
    candidates.add(appFile.getChildFile("Contents/Resources").getChildFile(rel));
#endif
#if JUCE_LINUX
    if (auto real = juce::File("/proc/self/exe").getLinkedTarget(); real.exists())
        candidates.add(real.getParentDirectory().getChildFile(rel));
#endif
    candidates.add(appFile.getParentDirectory().getChildFile(rel));

    auto walk = appFile.getParentDirectory();
    for (int i = 0; i < 8 && walk.exists(); ++i) {
        auto maybe = walk.getChildFile("resources").getChildFile(rel);
        if (maybe.existsAsFile()) {
            candidates.add(maybe);
            break;
        }
        walk = walk.getParentDirectory();
    }

    for (const auto& c : candidates)
        if (c.existsAsFile())
            return c;
    return {};
}

// Decode a file to a single mono float buffer (downmix). Empty on failure.
std::vector<float> decodeMono(const juce::String& filePath, int& sampleRateOut) {
    sampleRateOut = 0;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    juce::File jpath(filePath);
    if (!jpath.existsAsFile())
        return {};

    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(jpath));
    if (!reader || reader->lengthInSamples <= 0 || reader->sampleRate <= 0 ||
        reader->numChannels < 1)
        return {};

    const int channels = static_cast<int>(reader->numChannels);
    const int len = static_cast<int>(reader->lengthInSamples);
    sampleRateOut = static_cast<int>(reader->sampleRate);

    juce::AudioBuffer<float> multi(channels, len);
    multi.clear();
    reader->read(&multi, 0, len, 0, true, true);

    std::vector<float> mono(static_cast<size_t>(len), 0.0F);
    const float gain = 1.0F / static_cast<float>(channels);
    for (int ch = 0; ch < channels; ++ch) {
        const float* src = multi.getReadPointer(ch);
        for (int i = 0; i < len; ++i)
            mono[static_cast<size_t>(i)] += src[i] * gain;
    }
    return mono;
}

double projectBpm() {
    if (auto* controller = magda::TimelineController::getCurrent()) {
        const double bpm = controller->getState().tempo.bpm;
        if (isValidBpm(bpm))
            return bpm;
    }
    return DEFAULT_BPM;
}

// The default instrument for a transcription's new track: the internal 4OSC
// synth, built the same way the "add 4OSC" paths do (pluginId "4osc").
magda::DeviceInfo makeFourOscDevice() {
    magda::DeviceInfo d;
    d.name = "4OSC";
    d.manufacturer = "MAGDA";
    d.pluginId = "4osc";
    d.uniqueId = "4osc";
    d.fileOrIdentifier = "4osc";
    d.isInstrument = true;
    d.deviceType = magda::DeviceType::Instrument;
    d.format = magda::PluginFormat::Internal;
    return d;
}

}  // namespace

TranscriptionService& TranscriptionService::getInstance() {
    static TranscriptionService instance;
    return instance;
}

TranscriptionService::TranscriptionService() : pool_(std::make_unique<juce::ThreadPool>(1)) {}

TranscriptionService::~TranscriptionService() {
    // Drain UNBOUNDED (timeout < 0) before pool_ is freed. The worker job
    // captures `this`, so it must not outlive us; ~ThreadPool's built-in
    // finite-timeout drain could let a long transcribe job overrun and then
    // run against a freed pool/this, crashing in ThreadPool::runNextJob.
    if (pool_)
        pool_->removeAllJobs(true, -1);
}

BasicPitchTranscriber* TranscriptionService::backend() {
    std::call_once(backendOnce_, [this] {
        const juce::File model = findBundledModel();
        if (model.existsAsFile()) {
            auto t = std::make_unique<BasicPitchTranscriber>(model.getFullPathName().toStdString());
            if (t->isLoaded())
                backend_ = std::move(t);
        }
    });
    return backend_.get();
}

bool TranscriptionService::isAvailable() {
    std::scoped_lock lock(backendMutex_);
    return backend() != nullptr;
}

void TranscriptionService::transcribeAudioClip(ClipId sourceClipId, Completion onComplete) {
    auto fail = [onComplete = std::move(onComplete)](const juce::String& msg) mutable {
        auto cb = std::move(onComplete);
        juce::MessageManager::callAsync([cb = std::move(cb), msg]() mutable {
            if (cb)
                cb(INVALID_CLIP_ID, msg);
        });
    };

    const auto* clip = magda::ClipManager::getInstance().getClip(sourceClipId);
    if (clip == nullptr || !clip->isAudio()) {
        fail("Not an audio clip");
        return;
    }

    {
        std::scoped_lock lock(backendMutex_);
        if (backend() == nullptr) {
            fail("Transcription model not installed");
            return;
        }
    }

    // Snapshot everything we need off the clip now (message thread); the clip
    // pointer must not be touched on the worker.
    const juce::String filePath = clip->audio().source.filePath;
    const juce::String sourceName = juce::File(filePath).getFileNameWithoutExtension();
    const TrackId sourceTrackId = clip->trackId;
    const ClipView view = clip->view;
    const double startBeat = clip->placement.startBeat;
    const double lengthBeats = clip->placement.lengthBeats;
    const double offsetSec = clip->offset;
    const double bpm = projectBpm();

    // onComplete needs to survive the lambda copy into the pool.
    auto completion = std::make_shared<Completion>(std::move(onComplete));

    pool_->addJob([this, filePath, sourceName, sourceTrackId, view, startBeat, lengthBeats,
                   offsetSec, bpm, completion]() {
        int sampleRate = 0;
        std::vector<float> mono = decodeMono(filePath, sampleRate);

        std::vector<TranscribedNote> notes;
        if (!mono.empty() && sampleRate > 0) {
            std::scoped_lock lock(backendMutex_);
            if (auto* t = backend())
                notes = t->transcribe(mono.data(), static_cast<int>(mono.size()), sampleRate);
        }

        juce::MessageManager::callAsync([notes = std::move(notes), sourceName, sourceTrackId, view,
                                         startBeat, lengthBeats, offsetSec, bpm,
                                         completion]() mutable {
            auto report = [&](ClipId id, const juce::String& err) {
                if (completion && *completion)
                    (*completion)(id, err);
            };

            // Convert source-seconds notes -> clip-relative beats, keeping
            // those that fall inside the clip's audible beat range.
            const double beatsPerSec = bpm / 60.0;
            std::vector<MidiNote> midiNotes;
            midiNotes.reserve(notes.size());
            for (const TranscribedNote& n : notes) {
                const double beatInClip = (n.startSec - offsetSec) * beatsPerSec;
                if (beatInClip < 0.0 || beatInClip >= lengthBeats)
                    continue;

                MidiNote mn;
                mn.noteNumber = std::clamp(n.pitch, 0, 127);
                mn.velocity =
                    std::clamp(static_cast<int>(std::lround(n.velocity * 127.0F)), 1, 127);
                mn.startBeat = beatInClip;
                mn.lengthBeats = std::max(n.lengthSec * beatsPerSec, 1.0e-3);
                if (mn.startBeat + mn.lengthBeats > lengthBeats)
                    mn.lengthBeats = lengthBeats - mn.startBeat;

                // Expression offsets are relative to the note start and were
                // computed from the note's full length; keep only those within
                // the (possibly clip-clamped) note so nothing spills past the
                // clip boundary.
                for (const BendPoint& b : n.bend) {
                    const double beat = b.offsetSec * beatsPerSec;
                    if (beat < 0.0 || beat > mn.lengthBeats)
                        continue;
                    MidiPitchExpressionPoint p;
                    p.beat = beat;
                    p.semitones = b.semitones;
                    mn.pitchExpression.push_back(p);
                }
                midiNotes.push_back(std::move(mn));
            }

            if (midiNotes.empty()) {
                report(INVALID_CLIP_ID, "No notes detected");
                return;
            }

            // New 4OSC instrument track + MIDI clip + notes, as one undo
            // step. The result lives on its own track rather than overlapping
            // the source audio.
            magda::CompoundOperationScope scope("Transcribe to MIDI");

            auto trackCmd = std::make_unique<magda::CreateTrackWithDeviceCommand>(
                sourceName.isNotEmpty() ? sourceName : juce::String("Transcription"),
                TrackType::Audio, makeFourOscDevice());
            auto* trackPtr = trackCmd.get();
            magda::UndoManager::getInstance().executeCommand(std::move(trackCmd));
            const TrackId newTrackId = trackPtr->getCreatedTrackId();
            if (newTrackId == INVALID_TRACK_ID) {
                report(INVALID_CLIP_ID, "Could not create MIDI track");
                return;
            }

            // Move the new track to sit directly below the source track
            // rather than at the end of the list.
            const int sourceIdx = magda::TrackManager::getInstance().getTrackIndex(sourceTrackId);
            if (sourceIdx >= 0) {
                auto moveCmd =
                    std::make_unique<magda::MoveTrackToIndexCommand>(newTrackId, sourceIdx + 1);
                magda::UndoManager::getInstance().executeCommand(std::move(moveCmd));
            }

            auto createCmd = std::make_unique<magda::CreateClipCommand>(
                ClipType::MIDI, newTrackId, BeatPosition{startBeat}, BeatDuration{lengthBeats},
                juce::String(), view, bpm);
            auto* createPtr = createCmd.get();
            magda::UndoManager::getInstance().executeCommand(std::move(createCmd));
            const ClipId newClipId = createPtr->getCreatedClipId();
            if (newClipId == INVALID_CLIP_ID) {
                report(INVALID_CLIP_ID, "Could not create MIDI clip");
                return;
            }

            auto notesCmd = std::make_unique<magda::AddMultipleMidiNotesCommand>(
                newClipId, std::move(midiNotes), "Transcribed notes");
            magda::UndoManager::getInstance().executeCommand(std::move(notesCmd));

            report(newClipId, {});
        });
    });
}

}  // namespace magda::transcription
