// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

#include "../audio/analysis/SpeechAnalysis.hpp"
#include "../core/ClipInfo.hpp"
#include "../core/TempoMap.hpp"
#include "clip_api.hpp"
#include "magda_api.hpp"
#include "project_api.hpp"
#include "remote_engine_jobs.hpp"
#include "remote_handlers.hpp"
#include "remote_jobs.hpp"
#include "remote_speech_to_text.hpp"

namespace magda::remote::handlers {
namespace {

/// Below the job manager's 64 KiB result limit, so a long take gets a reason it can act on.
constexpr std::size_t kMaxResultBytes = 60 * 1024;
constexpr int kReadBlock = 1 << 16;

/// The part of one event the clip plays, as a stretch of its source file.
struct SpeechSegment {
    EventId eventId = INVALID_EVENT_ID;
    juce::File file;
    double sourceStart = 0.0;
    double sourceEnd = 0.0;
    double firstBeat = 0.0;
    double lastBeat = 0.0;
    bool left = true;
    bool right = true;
};

/// Shared by the handler, the worker thread and the completion; the worker writes results.
struct SpeechWork {
    std::vector<SpeechSegment> segments;
    audio::SpeechAnalysisOptions options;
    std::shared_ptr<std::atomic_bool> cancelled;
    std::vector<audio::SpeechAnalysisResult> results;
    juce::String error;
    SpeechToText speechToText;                     ///< Set when the phrases are transcribed.
    std::vector<std::vector<juce::String>> texts;  ///< Per segment, per phrase.
    juce::String transcriptionError;
};

double bpmAt(MagdaApi& api, const ClipInfo& clip) {
    const auto* tempo = api.project().tempoMap();
    return tempo != nullptr ? tempo->bpmAt(clip.placement.startBeat) : 0.0;
}

juce::var clipJson(MagdaApi& api, const ClipInfo& clip) {
    return toJson(makeClipDto(clip, api.project().tempoMap()));
}

/// The timeline beat that plays @p seconds of @p event in an unlooped clip.
double beatOf(const ClipInfo& clip, const AudioEvent& event, double seconds, double bpm) {
    return clip.placement.startBeat + event.startBeat +
           event.sourceInstantToTimelineBeats(seconds, bpm) -
           event.sourceInstantToTimelineBeats(event.anchorSeconds(), bpm);
}

/// The source second heard at @p beat; beatOf rises with the source, so bisection finds it.
double secondsAt(const ClipInfo& clip, const AudioEvent& event, double beat, double bpm,
                 double duration) {
    double low = 0.0;
    double high = duration;
    if (beatOf(clip, event, low, bpm) >= beat)
        return low;
    if (beatOf(clip, event, high, bpm) <= beat)
        return high;
    for (int i = 0; i < 60; ++i) {
        const double middle = 0.5 * (low + high);
        (beatOf(clip, event, middle, bpm) < beat ? low : high) = middle;
    }
    return 0.5 * (low + high);
}

/// Rounded to @p places decimals; dividing by the power of ten keeps the printed value short.
double rounded(double value, int places) {
    const double scale = std::pow(10.0, places);
    return std::round(value * scale) / scale;
}

std::vector<int> channelsOf(const SpeechSegment& segment, const juce::AudioFormatReader& reader) {
    if (reader.numChannels < 2)
        return {0};
    std::vector<int> channels;
    if (segment.left)
        channels.push_back(0);
    if (segment.right)
        channels.push_back(1);
    if (channels.empty())
        channels = {0, 1};
    return channels;
}

/// Each phrase's words; a failing transcriber stops the pass and leaves the regions whole.
void transcribe(SpeechWork& work, juce::AudioFormatManager& formats,
                const std::function<void(double)>& progress) {
    std::size_t total = 0;
    for (const auto& result : work.results)
        total += result.phrases.size();
    std::size_t done = 0;
    work.texts.resize(work.results.size());
    for (std::size_t i = 0; i < work.results.size(); ++i) {
        const auto& segment = work.segments[i];
        const std::unique_ptr<juce::AudioFormatReader> reader(
            formats.createReaderFor(segment.file));
        if (reader == nullptr)
            return;
        const auto rate = reader->sampleRate;
        const auto channels = channelsOf(segment, *reader);
        for (const auto& phrase : work.results[i].phrases) {
            if (work.cancelled->load(std::memory_order_acquire))
                return;
            const auto first =
                static_cast<juce::int64>(std::llround((segment.sourceStart + phrase.start) * rate));
            const auto count = static_cast<int>(std::min<juce::int64>(
                std::llround((phrase.end - phrase.start) * rate), reader->lengthInSamples - first));
            juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels),
                                            std::max(count, 1));
            std::vector<float> mono(static_cast<std::size_t>(std::max(count, 0)), 0.0f);
            if (count > 0) {
                reader->read(&buffer, 0, count, first, true, true);
                for (const int channel : channels)
                    juce::FloatVectorOperations::addWithMultiply(
                        mono.data(), buffer.getReadPointer(channel),
                        1.0f / static_cast<float>(channels.size()), count);
            }
            const auto transcription = work.speechToText(mono, rate, work.cancelled.get());
            if (transcription.error.isNotEmpty()) {
                work.transcriptionError = transcription.error;
                work.texts.clear();
                return;
            }
            work.texts[i].push_back(transcription.text.trim());
            progress(0.5 + 0.5 * static_cast<double>(++done) / static_cast<double>(total));
        }
    }
}

void analyse(SpeechWork& work, const std::function<void(double)>& reportProgress) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const double share = work.speechToText ? 0.5 : 1.0;
    const auto progress = [&](double value) { reportProgress(share * value); };
    double total = 0.0;
    for (const auto& segment : work.segments)
        total += segment.sourceEnd - segment.sourceStart;
    double done = 0.0;

    for (const auto& segment : work.segments) {
        const std::unique_ptr<juce::AudioFormatReader> reader(
            formats.createReaderFor(segment.file));
        if (reader == nullptr || reader->sampleRate <= 0.0) {
            work.error = "the clip's audio file cannot be read";
            return;
        }
        const auto rate = reader->sampleRate;
        const auto channels = channelsOf(segment, *reader);

        audio::SpeechAnalyser analyser(rate, static_cast<int>(channels.size()), work.options);
        juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), kReadBlock);
        std::vector<const float*> pointers(channels.size());
        const auto first = static_cast<juce::int64>(std::llround(segment.sourceStart * rate));
        const auto end = std::min(reader->lengthInSamples,
                                  static_cast<juce::int64>(std::llround(segment.sourceEnd * rate)));
        for (auto position = first; position < end; position += kReadBlock) {
            if (work.cancelled->load(std::memory_order_acquire))
                return;
            const auto count = static_cast<int>(std::min<juce::int64>(kReadBlock, end - position));
            reader->read(&buffer, 0, count, position, true, true);
            for (std::size_t i = 0; i < channels.size(); ++i)
                pointers[i] = buffer.getReadPointer(channels[i]);
            analyser.process(pointers.data(), count);
            if (total > 0.0)
                progress(std::min(
                    1.0, (done + static_cast<double>(position + count - first) / rate) / total));
        }
        done += segment.sourceEnd - segment.sourceStart;
        work.results.push_back(analyser.finish());
    }
    if (work.speechToText)
        transcribe(work, formats, reportProgress);
}

/// The regions in timeline beats, each segment's clamped to the beats its event plays.
juce::var resultJson(const ClipInfo& clip, const SpeechWork& work, double bpm,
                     const juce::var& targetLoudness) {
    juce::Array<juce::var> phrases;
    juce::Array<juce::var> breaths;
    juce::Array<juce::var> silences;
    for (std::size_t i = 0; i < work.segments.size() && i < work.results.size(); ++i) {
        const auto& segment = work.segments[i];
        const auto& events = clip.events();
        const auto event = std::ranges::find(events, segment.eventId, &AudioEvent::id);
        if (event == events.end())
            continue;
        const auto span = [&](double start, double end) {
            const auto beat = [&](double seconds) {
                return rounded(std::clamp(beatOf(clip, *event, segment.sourceStart + seconds, bpm),
                                          segment.firstBeat, segment.lastBeat),
                               4);
            };
            auto* object = new juce::DynamicObject();
            object->setProperty("startBeat", beat(start));
            object->setProperty("endBeat", beat(end));
            return object;
        };
        const auto& result = work.results[i];
        for (std::size_t p = 0; p < result.phrases.size(); ++p) {
            const auto& phrase = result.phrases[p];
            auto* object = span(phrase.start, phrase.end);
            object->setProperty("loudness", rounded(phrase.loudness, 1));
            if (i < work.texts.size() && p < work.texts[i].size())
                object->setProperty("text", work.texts[i][p]);
            if (!targetLoudness.isVoid())
                object->setProperty(
                    "gainToTargetDb",
                    rounded(static_cast<double>(targetLoudness) - phrase.loudness, 1));
            phrases.add(juce::var(object));
        }
        for (const auto& breath : result.breaths)
            breaths.add(juce::var(span(breath.start, breath.end)));
        for (const auto& silence : result.silences)
            silences.add(juce::var(span(silence.start, silence.end)));
    }

    auto* out = new juce::DynamicObject();
    out->setProperty("clipId", clip.id);
    if (!work.results.empty())
        out->setProperty("thresholdDb", rounded(work.results.front().thresholdDb, 1));
    if (!targetLoudness.isVoid())
        out->setProperty("targetLoudness", targetLoudness);
    if (work.transcriptionError.isNotEmpty())
        out->setProperty("transcriptionError", work.transcriptionError);
    out->setProperty("phrases", phrases);
    out->setProperty("breaths", breaths);
    out->setProperty("silences", silences);
    return juce::var(out);
}

audio::SpeechAnalysisOptions optionsFrom(const juce::var& input) {
    audio::SpeechAnalysisOptions options;
    const auto read = [&](const char* key, double& field) {
        if (!input[key].isVoid())
            field = static_cast<double>(input[key]);
    };
    read("minSilenceSeconds", options.minSilenceSeconds);
    read("breathMinSeconds", options.breathMinSeconds);
    read("breathMaxSeconds", options.breathMaxSeconds);
    read("paddingSeconds", options.paddingSeconds);
    if (!input["thresholdDb"].isVoid())
        options.thresholdDb = static_cast<double>(input["thresholdDb"]);
    return options;
}

Error startError(EngineJobStartStatus status) {
    switch (status) {
        case EngineJobStartStatus::Busy:
            return {ErrorCode::Conflict, "too many analyses are running; try again shortly", {}};
        case EngineJobStartStatus::Unsupported:
            return {ErrorCode::ValidationFailed, "this engine does not analyse audio files", {}};
        case EngineJobStartStatus::Unavailable:
            return {ErrorCode::Conflict, "the audio engine is unavailable", {}};
        case EngineJobStartStatus::Failed:
        case EngineJobStartStatus::Started:
            break;
    }
    return {ErrorCode::InternalError, "the analysis could not start", {}};
}

}  // namespace

HandlerResult clipsAnalyzeSpeech(MagdaApi& api, const juce::var& input,
                                 const RequestContext& context) {
    const auto clipId = static_cast<ClipId>(static_cast<int>(input["clipId"]));
    const auto* clip = api.clips().getClip(clipId);
    if (clip == nullptr)
        return HandlerResult::fail(ErrorCode::NotFound,
                                   "clip " + juce::String(clipId) + " not found");
    if (!clip->isAudio())
        return HandlerResult::fail(ErrorCode::ValidationFailed, "only an audio clip has speech");
    if (clip->view != ClipView::Arrangement)
        return HandlerResult::fail(ErrorCode::Conflict, "only an arrangement clip is analysed");
    if (clip->loopEnabled)
        return HandlerResult::fail(ErrorCode::Conflict,
                                   "a looped clip repeats its audio; turn its loop off first");
    if (context.jobs == nullptr || context.engineJobs == nullptr)
        return HandlerResult::fail(ErrorCode::InternalError,
                                   "engine file services are unavailable");

    const double rangeStart =
        std::max(clip->placement.startBeat,
                 static_cast<double>(input.getProperty("startBeat", clip->placement.startBeat)));
    const double rangeEnd =
        std::min(clip->placement.endBeat(),
                 static_cast<double>(input.getProperty("endBeat", clip->placement.endBeat())));
    if (rangeEnd <= rangeStart)
        return HandlerResult::fail(ErrorCode::ValidationFailed,
                                   "startBeat and endBeat leave nothing of the clip");

    auto work = std::make_shared<SpeechWork>();
    work->options = optionsFrom(input);
    if (work->options.breathMaxSeconds <= work->options.breathMinSeconds)
        return HandlerResult::fail(ErrorCode::ValidationFailed,
                                   "breathMaxSeconds must be greater than breathMinSeconds");

    const double bpm = bpmAt(api, *clip);
    if (!isValidBpm(bpm))
        return HandlerResult::fail(ErrorCode::Conflict, "the project has no tempo at the clip");
    for (const auto& event : clip->events()) {
        if (event.reversed)
            return HandlerResult::fail(ErrorCode::Conflict,
                                       "a reversed clip plays its audio backwards; un-reverse it "
                                       "first");
        const double duration = event.sourceDurationSeconds();
        const double clipStart = clip->placement.startBeat;
        const double firstBeat = std::max(rangeStart, clipStart + event.startBeat);
        const double lastBeat =
            std::min({rangeEnd, clipStart + event.endBeat(), beatOf(*clip, event, duration, bpm)});
        if (duration <= 0.0 || lastBeat <= firstBeat)
            continue;
        work->segments.push_back({
            .eventId = event.id,
            .file = juce::File(event.sourceFilePath()),
            .sourceStart = secondsAt(*clip, event, firstBeat, bpm, duration),
            .sourceEnd = secondsAt(*clip, event, lastBeat, bpm, duration),
            .firstBeat = firstBeat,
            .lastBeat = lastBeat,
            .left = event.leftChannelActive,
            .right = event.rightChannelActive,
        });
    }
    if (work->segments.empty())
        return HandlerResult::fail(ErrorCode::Conflict, "the clip plays no audio in that range");

    if (static_cast<bool>(input["transcribe"])) {
        work->speechToText = speechToText();
        if (!work->speechToText)
            return HandlerResult::fail(ErrorCode::Conflict,
                                       "this build has no speech to text; analyse without "
                                       "transcribe");
    }
    work->cancelled = std::make_shared<std::atomic_bool>(false);
    const auto jobId = context.jobs->accept(
        {.kind = "clips.analyzeSpeech",
         .ownerClientId = context.clientId,
         .requiredScope = Scope::Read,
         .acceptedRevision = context.revision,
         .revisionPolicy = JobRevisionPolicy::CheckAtStartOnly,
         .projectBound = true,
         .cancellable = true},
        [cancelled = work->cancelled] { cancelled->store(true, std::memory_order_release); });
    if (jobId.isEmpty())
        return HandlerResult::fail(ErrorCode::InternalError, "analysis job was not accepted");

    auto jobs = context.jobsOwner;
    auto revision = context.revisionOwner;
    const auto owner = context.clientId;
    const auto before = juce::JSON::toString(clipJson(api, *clip));
    const auto& targetLoudness = input["targetLoudness"];
    auto* live = &api;
    const auto started = context.engineJobs->runFileTask(
        [work, jobs, jobId] {
            analyse(*work, [&](double progress) {
                if (jobs)
                    jobs->reportProgress(jobId, progress);
            });
        },
        work->cancelled,
        [work, jobs, revision, jobId, owner, before, targetLoudness, live, clipId] {
            if (!jobs)
                return;
            const auto now =
                revision ? revision->load(std::memory_order_acquire) : INITIAL_REVISION;
            Error lookup;
            const auto job = jobs->get(jobId, owner, allScopes(), lookup);
            if (!job ||
                (job->state != RemoteJobState::Accepted && job->state != RemoteJobState::Running))
                return;
            if (work->cancelled->load(std::memory_order_acquire))
                return void(
                    jobs->fail(jobId, {ErrorCode::Cancelled, "analysis was cancelled", {}}, now));
            if (work->error.isNotEmpty())
                return void(jobs->fail(jobId, {ErrorCode::InternalError, work->error, {}}, now));
            const auto* current = live->clips().getClip(clipId);
            if (current == nullptr || juce::JSON::toString(clipJson(*live, *current)) != before)
                return void(jobs->fail(
                    jobId, {ErrorCode::Conflict, "the clip changed while it was analysed", {}},
                    now));
            auto result = resultJson(*current, *work, bpmAt(*live, *current), targetLoudness);
            if (juce::JSON::toString(result, true).getNumBytesAsUTF8() > kMaxResultBytes)
                return void(jobs->fail(jobId,
                                       {ErrorCode::ValidationFailed,
                                        "the clip holds too many phrases for one result; analyse "
                                        "it in parts with startBeat and endBeat",
                                        {}},
                                       now));
            jobs->complete(jobId, std::move(result), now);
        });

    if (started == EngineJobStartStatus::Started)
        context.jobs->markRunning(jobId);
    else
        context.jobs->fail(jobId, startError(started), context.revision);
    Error error;
    const auto job = context.jobs->get(jobId, context.clientId, context.scopes, error);
    return job ? HandlerResult::unchanged(toJson(*job)) : HandlerResult::fail(std::move(error));
}

}  // namespace magda::remote::handlers
