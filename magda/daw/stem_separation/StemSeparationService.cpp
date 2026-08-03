// See StemSeparationService.hpp.

#include "StemSeparationService.hpp"

#include <juce_audio_formats/juce_audio_formats.h>

#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "ClipCommands.hpp"
#include "ClipInfo.hpp"
#include "ClipManager.hpp"
#include "ClipPropertyCommands.hpp"
#include "DemucsSeparator.hpp"
#include "HpssSeparator.hpp"
#include "ProjectManager.hpp"
#include "SpleeterSeparator.hpp"
#include "StemModelDownloader.hpp"
#include "StemSeparator.hpp"
#include "TempoUtils.hpp"
#include "TrackCommands.hpp"
#include "TrackManager.hpp"
#include "TrackPropertyCommands.hpp"
#include "UndoManager.hpp"
#include "ui/state/TimelineController.hpp"

namespace magda::stems {

namespace {

double projectBpm() {
    if (auto* controller = magda::TimelineController::getCurrent()) {
        const double bpm = controller->getState().tempo.bpm;
        if (isValidBpm(bpm))
            return bpm;
    }
    return DEFAULT_BPM;
}

// Decode a whole audio file, all channels. The cap keeps one user action from
// committing an unbounded contiguous allocation before separation starts.
bool decodeFile(const juce::String& filePath, juce::AudioBuffer<float>& out, double& sampleRate,
                juce::String& error) {
    constexpr juce::int64 kMaxDecodedBytes = 1024LL * 1024LL * 1024LL;

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    juce::File jpath(filePath);
    if (!jpath.existsAsFile()) {
        error = "Audio file not found";
        return false;
    }

    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(jpath));
    if (!reader || reader->lengthInSamples <= 0 || reader->sampleRate <= 0 ||
        reader->numChannels < 1) {
        error = "Could not read audio file";
        return false;
    }

    const auto bytesPerFrame =
        static_cast<juce::int64>(reader->numChannels) * static_cast<juce::int64>(sizeof(float));
    if (reader->lengthInSamples > std::numeric_limits<int>::max() ||
        reader->lengthInSamples > kMaxDecodedBytes / bytesPerFrame) {
        error = "Audio file is too large to split safely";
        return false;
    }

    const int channels = static_cast<int>(reader->numChannels);
    const int len = static_cast<int>(reader->lengthInSamples);
    out.setSize(channels, len);
    out.clear();
    if (!reader->read(&out, 0, len, 0, true, true)) {
        error = "Could not read audio file";
        return false;
    }
    sampleRate = reader->sampleRate;
    return true;
}

// Write one stem as 32-bit float WAV. Returns the file, or {} on failure.
juce::File writeStemWav(const juce::File& dir, const juce::String& baseName, const Stem& stem,
                        double sampleRate) {
    auto file = dir.getChildFile(baseName + " - " + stem.name + ".wav");
    file.deleteFile();

    auto stream = file.createOutputStream();
    if (stream == nullptr)
        return {};

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(stream.get(), sampleRate,
                            static_cast<unsigned int>(stem.audio.getNumChannels()), 32, {}, 0));
    if (writer == nullptr)
        return {};
    stream.release();  // writer owns it now

    if (!writer->writeFromAudioSampleBuffer(stem.audio, 0, stem.audio.getNumSamples()))
        return {};
    return file;
}

// Stem names per engine, known before the (possibly expensive) separator is
// constructed, so the tracks can be created upfront.
std::vector<juce::String> stemNamesFor(StemSeparationService::Engine engine) {
    switch (engine) {
        case StemSeparationService::Engine::Hpss:
            return HpssSeparator{}.stemNames();
        case StemSeparationService::Engine::Demucs:
            return {"Drums", "Bass", "Other", "Vocals"};
        case StemSeparationService::Engine::Spleeter:
            return {"Vocals", "Accompaniment"};
    }
    return {};
}

std::unique_ptr<StemSeparator> makeSeparator(StemSeparationService::Engine engine) {
    switch (engine) {
        case StemSeparationService::Engine::Hpss:
            return std::make_unique<HpssSeparator>();
        case StemSeparationService::Engine::Demucs: {
            // Constructed per job on purpose: the ORT session holds the
            // ~300 MB graph in memory, and separation is rare enough that
            // a few seconds of load beats keeping it resident.
            auto demucs = std::make_unique<DemucsSeparator>(
                StemModelDownloader::modelFiles(StemModel::Htdemucs)
                    .front()
                    .getFullPathName()
                    .toStdString());
            if (!demucs->isLoaded())
                return nullptr;
            return demucs;
        }
        case StemSeparationService::Engine::Spleeter: {
            const auto files = StemModelDownloader::modelFiles(StemModel::Spleeter2s);
            auto spleeter = std::make_unique<SpleeterSeparator>(
                files[0].getFullPathName().toStdString(), files[1].getFullPathName().toStdString());
            if (!spleeter->isLoaded())
                return nullptr;
            return spleeter;
        }
    }
    return nullptr;
}

}  // namespace

StemSeparationService& StemSeparationService::getInstance() {
    static StemSeparationService instance;
    return instance;
}

StemSeparationService::StemSeparationService() : pool_(std::make_unique<juce::ThreadPool>(1)) {}

StemSeparationService::~StemSeparationService() {
    // Drain UNBOUNDED (timeout < 0) before pool_ is freed; see
    // TranscriptionService for why the built-in finite drain is not enough.
    ++cancelGeneration_;
    if (pool_)
        pool_->removeAllJobs(true, -1);
}

bool StemSeparationService::isEngineAvailable(Engine engine) {
    switch (engine) {
        case Engine::Hpss:
            return true;  // Pure DSP, no model.
        case Engine::Demucs:
            return DemucsSeparator::backendAvailable() &&
                   StemModelDownloader::isInstalled(StemModel::Htdemucs);
        case Engine::Spleeter:
            return SpleeterSeparator::backendAvailable() &&
                   StemModelDownloader::isInstalled(StemModel::Spleeter2s);
    }
    return false;
}

bool StemSeparationService::isBusy() const {
    return pendingJobs_.load() > 0;
}

void StemSeparationService::setActivityCallback(ActivityCallback callback) {
    activityCallback_ = std::move(callback);
}

void StemSeparationService::cancelAll() {
    // Bumping the generation cancels only jobs enqueued before this call;
    // a split started afterwards snapshots the new value and runs normally.
    ++cancelGeneration_;
}

void StemSeparationService::splitClipIntoStems(ClipId sourceClipId, Engine engine,
                                               Completion onComplete) {
    // Every exit path reports exactly once, on the message thread, and
    // releases the job slot (and the banner) with it.
    auto completion = std::make_shared<Completion>(std::move(onComplete));
    auto report = [this, completion](TrackId groupId, const juce::String& error) {
        juce::MessageManager::callAsync([this, completion, groupId, error]() {
            --pendingJobs_;
            if (pendingJobs_.load() == 0 && activityCallback_)
                activityCallback_(false, 1.0F);
            if (completion && *completion)
                (*completion)(groupId, error);
        });
    };
    ++pendingJobs_;
    if (activityCallback_)
        activityCallback_(true, 0.0F);

    const auto* clip = magda::ClipManager::getInstance().getClip(sourceClipId);
    if (clip == nullptr || !clip->isAudio()) {
        report(INVALID_TRACK_ID, "Not an audio clip");
        return;
    }
    if (!isEngineAvailable(engine)) {
        report(INVALID_TRACK_ID, "Separation model not installed");
        return;
    }

    // Snapshot everything we need off the clip now (message thread); the clip
    // pointer must not be touched on the worker.
    const juce::String filePath = audioEventRef(*clip).sourceFilePath();
    if (!juce::File(filePath).existsAsFile()) {
        report(INVALID_TRACK_ID, "Audio file not found");
        return;
    }
    const juce::String baseName = juce::File(filePath).getFileNameWithoutExtension();
    const TrackId sourceTrackId = clip->trackId;
    const double startBeat = clip->placement.startBeat;
    const int sceneIndex = clip->sceneIndex;
    const double bpm = projectBpm();

    const juce::File stemsRoot = ProjectManager::getInstance().getStemsDirectory();

    // Phase 1, immediately on the message thread: the empty stem tracks and
    // their group appear the moment the user clicks, so the track area
    // reflects the operation in real time; the clips land in phase 2 when
    // separation finishes. One undo step per phase.
    std::vector<TrackId> stemTrackIds;
    TrackId groupId = INVALID_TRACK_ID;
    {
        magda::CompoundOperationScope scope("Split into Stems");
        auto& undo = magda::UndoManager::getInstance();

        TrackId after = sourceTrackId;
        for (const auto& stemName : stemNamesFor(engine)) {
            auto trackCmd =
                std::make_unique<magda::CreateTrackCommand>(TrackType::Audio, stemName, after);
            auto* trackPtr = trackCmd.get();
            undo.executeCommand(std::move(trackCmd));
            const TrackId trackId = trackPtr->getCreatedTrackId();
            if (trackId == INVALID_TRACK_ID) {
                report(INVALID_TRACK_ID, "Could not create stem track");
                return;
            }
            stemTrackIds.push_back(trackId);
            after = trackId;
        }

        auto groupCmd =
            std::make_unique<magda::GroupTracksCommand>(stemTrackIds, baseName + " Stems");
        auto* groupPtr = groupCmd.get();
        undo.executeCommand(std::move(groupCmd));
        groupId = groupPtr->getCreatedGroupId();
    }

    // On any later failure the pre-created tracks must not linger.
    auto failAndCleanup = [stemTrackIds, groupId, report](const juce::String& error) {
        juce::MessageManager::callAsync([stemTrackIds, groupId, report, error]() {
            magda::CompoundOperationScope scope("Remove Stem Tracks");
            auto& undo = magda::UndoManager::getInstance();
            for (const TrackId trackId : stemTrackIds) {
                if (magda::TrackManager::getInstance().getTrack(trackId) != nullptr)
                    undo.executeCommand(std::make_unique<magda::DeleteTrackCommand>(trackId));
            }
            if (groupId != INVALID_TRACK_ID &&
                magda::TrackManager::getInstance().getTrack(groupId) != nullptr)
                undo.executeCommand(std::make_unique<magda::DeleteTrackCommand>(groupId));
            report(INVALID_TRACK_ID, error);
        });
    };

    pool_->addJob([this, sourceClipId, engine, filePath, baseName, sourceTrackId, startBeat,
                   sceneIndex, bpm, stemsRoot, stemTrackIds, groupId, report = std::move(report),
                   failAndCleanup = std::move(failAndCleanup),
                   enqueueGeneration = cancelGeneration_.load()]() {
        // Cancelled iff cancelAll() ran after this job was enqueued.
        auto cancelled = [this, enqueueGeneration]() {
            return cancelGeneration_.load() != enqueueGeneration;
        };
        try {
            if (cancelled()) {
                failAndCleanup("Separation cancelled");
                return;
            }

            auto separator = makeSeparator(engine);
            if (separator == nullptr) {
                failAndCleanup("Separation engine unavailable");
                return;
            }

            juce::AudioBuffer<float> input;
            double sampleRate = 0.0;
            juce::String decodeError;
            if (!decodeFile(filePath, input, sampleRate, decodeError)) {
                failAndCleanup(decodeError);
                return;
            }

            // Throttle progress to whole-percent steps on the message thread,
            // feeding the activity banner.
            auto lastPercent = std::make_shared<int>(-1);
            auto progress = [this, lastPercent, cancelled](float frac) {
                if (cancelled())
                    return false;
                const int percent = static_cast<int>(frac * 100.0F);
                if (percent != *lastPercent) {
                    *lastPercent = percent;
                    juce::MessageManager::callAsync([this, frac]() {
                        if (activityCallback_)
                            activityCallback_(true, frac);
                    });
                }
                return true;
            };

            std::vector<Stem> stems = separator->separate(input, sampleRate, progress);
            if (stems.empty()) {
                failAndCleanup(cancelled() ? "Separation cancelled" : "Separation failed");
                return;
            }

            // One folder per split: <media>/stems/<source> - <engine>/
            juce::File dir = stemsRoot.getChildFile(baseName + " - " + separator->modelId());
            if (dir.exists())
                dir = dir.getNonexistentSibling();
            if (!dir.createDirectory().wasOk()) {
                failAndCleanup("Could not create stems folder");
                return;
            }

            std::vector<std::pair<juce::String, juce::File>> stemFiles;  // name -> wav
            for (const auto& stem : stems) {
                auto file = writeStemWav(dir, baseName, stem, sampleRate);
                if (file == juce::File()) {
                    failAndCleanup("Could not write stem file");
                    return;
                }
                stemFiles.emplace_back(stem.name, file);
            }

            juce::MessageManager::callAsync(
                [sourceClipId, sourceTrackId, startBeat, sceneIndex, bpm, stemTrackIds, groupId,
                 stemFiles = std::move(stemFiles), report, failAndCleanup]() mutable {
                    // The source clip may have been deleted while we were separating;
                    // its placement is gone with it, so bail rather than guess.
                    if (magda::ClipManager::getInstance().getClip(sourceClipId) == nullptr) {
                        failAndCleanup("Source clip no longer exists");
                        return;
                    }

                    // The user may have deleted the pre-created tracks mid-split.
                    for (const TrackId trackId : stemTrackIds) {
                        if (magda::TrackManager::getInstance().getTrack(trackId) == nullptr) {
                            failAndCleanup("Stem tracks were deleted");
                            return;
                        }
                    }

                    // Phase 2: land the clips on the tracks created in phase 1.
                    magda::CompoundOperationScope scope("Add Stem Clips");
                    auto& undo = magda::UndoManager::getInstance();

                    for (size_t i = 0; i < stemFiles.size() && i < stemTrackIds.size(); ++i) {
                        const auto& [stemName, file] = stemFiles[i];
                        const TrackId trackId = stemTrackIds[i];

                        std::unique_ptr<magda::DuplicateClipCommand> dupCmd;
                        if (sceneIndex >= 0)
                            dupCmd = magda::DuplicateClipCommand::forSessionSlot(
                                sourceClipId, trackId, sceneIndex);
                        else
                            dupCmd = std::make_unique<magda::DuplicateClipCommand>(
                                sourceClipId, BeatPosition{startBeat}, trackId, bpm);
                        auto* dupPtr = dupCmd.get();
                        undo.executeCommand(std::move(dupCmd));
                        const ClipId stemClipId = dupPtr->getDuplicatedClipId();
                        if (stemClipId == INVALID_CLIP_ID) {
                            failAndCleanup("Could not create stem clip");
                            return;
                        }

                        // Point the duplicate at the stem render. Takes/comp state
                        // belongs to the original recording, not the stem.
                        const juce::String path = file.getFullPathName();
                        undo.executeCommand(std::make_unique<magda::SetClipPropertyCommand>(
                            stemClipId, "Set Stem Source",
                            [path, stemName](magda::ClipManager& manager, ClipId id) {
                                if (auto* c = manager.getClip(id)) {
                                    c->name = stemName;
                                    auto& a = c->audio();
                                    if (auto* event = a.primaryEvent())
                                        event->sourceId = SourcePool::getInstance().acquire(path);
                                    a.takes.clear();
                                    a.currentTakeIndex = 0;
                                    a.comp.clear();
                                    a.compActive = false;
                                    manager.forceNotifyClipPropertyChanged(id);
                                }
                            }));
                    }

                    // Mute the source track so the stems don't double it; undoing
                    // the split restores it with the same step.
                    if (magda::TrackManager::getInstance().getTrack(sourceTrackId) != nullptr)
                        undo.executeCommand(
                            std::make_unique<magda::SetTrackMuteCommand>(sourceTrackId, true));

                    report(groupId, {});
                });
        } catch (const std::exception& error) {
            failAndCleanup("Separation failed: " + juce::String(error.what()));
        } catch (...) {
            failAndCleanup("Separation failed with an unknown error");
        }
    });
}

}  // namespace magda::stems
