#include "CompService.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <set>
#include <vector>

#include "core/ClipManager.hpp"
#include "core/CompSectionMath.hpp"
#include "core/RangesHelpers.hpp"
#include "project/ProjectManager.hpp"

namespace magda {

namespace {

constexpr double kCrossfadeSeconds = 0.012;  // 12 ms equal-power crossfade at boundaries

// Longest take defines the comp timeline extent.
double compLengthSeconds(const AudioClipModel& audio) {
    if (audio.takes.empty())
        return 0.0;
    return std::ranges::max(audio.takes | std::views::transform(&AudioTake::durationSeconds));
}

// Rebuild the comp section list so [a, b) plays `take`, splitting/merging as
// needed. Sections stay sorted, contiguous and cover [0, compLen). Delegates to
// the shared (domain-neutral) algorithm; CompSection (seconds) <-> CompSpan.
std::vector<CompSection> assignSection(const std::vector<CompSection>& existing, double compLen,
                                       int baseTake, double a, double b, int take) {
    const auto asSpan = [](const CompSection& section) {
        return CompSpan{section.startSeconds, section.endSeconds, section.takeIndex};
    };
    const auto asSection = [](const CompSpan& span) {
        return CompSection{span.start, span.end, span.takeIndex};
    };

    const auto spans = existing | std::views::transform(asSpan) | toStd<std::vector<CompSpan>>();
    return assignCompSections(spans, compLen, baseTake, a, b, take) |
           std::views::transform(asSection) | toStd<std::vector<CompSection>>();
}

// Equal-power gains across a fade position p in [0, 1].
inline float fadeInGain(float p) {
    return std::sin(p * juce::MathConstants<float>::halfPi);
}
inline float fadeOutGain(float p) {
    return std::cos(p * juce::MathConstants<float>::halfPi);
}

struct CompSnapshot {
    std::vector<juce::String> takePaths;
    std::vector<CompSection> sections;
    juce::File outputFile;
    ClipId clipId = INVALID_CLIP_ID;
};

// Read [startSample, startSample + numSamples) of a take into dest at destOffset,
// zero-filling past the take's length. dest must already be sized.
void readTakeInto(juce::AudioFormatReader& reader, juce::AudioBuffer<float>& dest, int destOffset,
                  juce::int64 startSample, int numSamples) {
    if (numSamples <= 0)
        return;
    const auto available = static_cast<int>(juce::jlimit<juce::int64>(
        0, juce::jmax<juce::int64>(0, reader.lengthInSamples - startSample), numSamples));
    if (available > 0)
        reader.read(&dest, destOffset, available, startSample, true, true);
}

// Stitch the comp into a WAV. Returns the rendered duration in seconds, or 0 on
// failure. Runs on a background thread; touches no shared state.
double stitchComp(const CompSnapshot& snap) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::vector<std::unique_ptr<juce::AudioFormatReader>> readers(snap.takePaths.size());
    double sampleRate = 0.0;
    int numChannels = 0;
    for (size_t i = 0; i < snap.takePaths.size(); ++i) {
        if (auto* r = formats.createReaderFor(juce::File(snap.takePaths[i]))) {
            readers[i].reset(r);
            if (sampleRate <= 0.0) {
                sampleRate = r->sampleRate;
                numChannels = juce::jmax(1, static_cast<int>(r->numChannels));
            }
        }
    }
    if (sampleRate <= 0.0 || numChannels <= 0 || snap.sections.empty())
        return 0.0;

    const double compLen =
        std::ranges::max(snap.sections | std::views::transform(&CompSection::endSeconds));
    const int total = static_cast<int>(std::round(compLen * sampleRate));
    if (total <= 0)
        return 0.0;

    const int half =
        juce::jmax(1, static_cast<int>(std::round(kCrossfadeSeconds * sampleRate * 0.5)));

    juce::AudioBuffer<float> out(numChannels, total);
    out.clear();
    std::vector<float> gains;

    for (size_t i = 0; i < snap.sections.size(); ++i) {
        const auto& sec = snap.sections[i];
        const int takeIndex = sec.takeIndex;
        if (takeIndex < 0 || takeIndex >= static_cast<int>(readers.size()) || !readers[takeIndex])
            continue;

        const int secStart =
            juce::jlimit(0, total, static_cast<int>(std::round(sec.startSeconds * sampleRate)));
        const int secEnd =
            juce::jlimit(0, total, static_cast<int>(std::round(sec.endSeconds * sampleRate)));
        const bool blendIn = i > 0;
        const bool blendOut = i + 1 < snap.sections.size();

        const int regionStart = juce::jmax(0, secStart - (blendIn ? half : 0));
        const int regionEnd = juce::jmin(total, secEnd + (blendOut ? half : 0));
        const int regionLen = regionEnd - regionStart;
        if (regionLen <= 0)
            continue;

        juce::AudioBuffer<float> temp(numChannels, regionLen);
        temp.clear();
        readTakeInto(*readers[takeIndex], temp, 0, regionStart, regionLen);

        // Equal-power fades into the overlap windows so neighbouring sections sum
        // to constant power across each boundary. The gain follows the sample
        // position alone, so the ramp is built once and applied per channel.
        gains.assign(static_cast<size_t>(regionLen), 1.0f);
        for (int n = 0; n < regionLen; ++n) {
            const int absN = regionStart + n;
            float gain = 1.0f;
            if (blendIn && absN < secStart + half) {
                const float p = juce::jlimit(
                    0.0f, 1.0f, (absN - (secStart - half)) / static_cast<float>(2 * half));
                gain *= fadeInGain(p);
            }
            if (blendOut && absN > secEnd - half) {
                const float p = juce::jlimit(
                    0.0f, 1.0f, (absN - (secEnd - half)) / static_cast<float>(2 * half));
                gain *= fadeOutGain(p);
            }
            gains[static_cast<size_t>(n)] = gain;
        }
        for (int ch = 0; ch < numChannels; ++ch)
            juce::FloatVectorOperations::multiply(temp.getWritePointer(ch), gains.data(),
                                                  regionLen);

        for (int ch = 0; ch < numChannels; ++ch)
            out.addFrom(ch, regionStart, temp, ch, 0, regionLen);
    }

    auto stream = snap.outputFile.createOutputStream();
    if (!stream)
        return 0.0;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(
        stream.get(), sampleRate, static_cast<unsigned>(numChannels), 24, {}, 0));
    if (!writer)
        return 0.0;
    static_cast<void>(stream.release());  // writer owns it now
    writer->writeFromAudioSampleBuffer(out, 0, total);
    writer.reset();
    return total / sampleRate;
}

}  // namespace

CompService& CompService::getInstance() {
    static CompService instance;
    return instance;
}

CompService::CompService() : pool_(std::make_unique<juce::ThreadPool>(1)) {}

CompService::~CompService() {
    // Drain UNBOUNDED (timeout < 0) before pool_ is freed. Relying on
    // ~ThreadPool's built-in finite-timeout drain is a use-after-free hazard:
    // if a stitch job overruns the timeout the pool is destroyed under the
    // running worker, crashing it in ThreadPool::runNextJob.
    if (pool_)
        pool_->removeAllJobs(true, -1);
}

void CompService::setSection(ClipId clipId, double startSeconds, double endSeconds, int takeIndex) {
    auto& cm = ClipManager::getInstance();
    auto* clip = cm.getClip(clipId);
    if (!clip || !clip->isAudio() || clip->audio().takes.size() < 2)
        return;

    auto& audio = clip->audio();
    const double compLen = compLengthSeconds(audio);
    if (compLen <= 0.0)
        return;
    if (takeIndex < 0 || takeIndex >= static_cast<int>(audio.takes.size()))
        return;

    ClipInfo before = *clip;
    audio.comp = assignSection(audio.comp, compLen, audio.currentTakeIndex, startSeconds,
                               endSeconds, takeIndex);
    audio.compActive = true;
    renderComp(clipId);
    cm.pushClipTakeUndo("Comp Section", before);
}

void CompService::clearComp(ClipId clipId) {
    auto& cm = ClipManager::getInstance();
    auto* clip = cm.getClip(clipId);
    if (!clip || !clip->isAudio())
        return;

    auto& audio = clip->audio();
    if (!audio.compActive && audio.comp.empty())
        return;
    ClipInfo before = *clip;
    audio.comp.clear();
    audio.compActive = false;
    const int idx = juce::jlimit(0, juce::jmax(0, static_cast<int>(audio.takes.size()) - 1),
                                 audio.currentTakeIndex);
    if (idx < static_cast<int>(audio.takes.size())) {
        if (auto* event = audio.primaryEvent())
            event->sourceId = SourcePool::getInstance().acquire(audio.takes[idx].filePath);
    }
    cm.forceNotifyClipPropertyChanged(clipId);
    cm.pushClipTakeUndo("Clear Comp", before);
}

void CompService::renderComp(ClipId clipId) {
    auto& cm = ClipManager::getInstance();
    auto* clip = cm.getClip(clipId);
    if (!clip || !clip->isAudio() || !clip->audio().compActive || clip->audio().comp.empty())
        return;

    auto rendersDir = ProjectManager::getInstance().getRendersDirectory();
    if (rendersDir == juce::File())
        return;
    rendersDir.createDirectory();

    CompSnapshot snap;
    snap.clipId = clipId;
    snap.sections = clip->audio().comp;
    for (const auto& t : clip->audio().takes)
        snap.takePaths.push_back(t.filePath);
    snap.outputFile = rendersDir.getNonexistentChildFile(
        "comp_" + juce::String(static_cast<int>(clipId)), ".wav");

    pool_->addJob([snap]() {
        const double dur = stitchComp(snap);
        if (dur <= 0.0)
            return;
        const juce::String path = snap.outputFile.getFullPathName();
        const ClipId clipId = snap.clipId;
        juce::MessageManager::callAsync([clipId, path, dur]() {
            auto& cm = ClipManager::getInstance();
            auto* clip = cm.getClip(clipId);
            if (!clip || !clip->isAudio() || !clip->audio().compActive)
                return;
            auto* event = clip->primaryEvent();
            if (event == nullptr)
                return;
            event->sourceId = SourcePool::getInstance().acquire(path);
            if (auto* source = SourcePool::getInstance().getMutable(event->sourceId))
                source->durationSeconds = dur;
            cm.forceNotifyClipPropertyChanged(clipId);
        });
    });
}

}  // namespace magda
