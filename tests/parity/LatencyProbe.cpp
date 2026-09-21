#include "LatencyProbe.hpp"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <random>

#include "magda/daw/project/serialization/ProjectSerializer.hpp"

namespace magda::parity {

namespace {

constexpr double kBurstStartSeconds = 0.25;
constexpr double kBurstSeconds = 0.1;
constexpr double kClipSeconds = 1.0;
constexpr float kBurstLevel = 0.25f;
constexpr std::uint32_t kBurstSeed = 2082;

bool writeProbe(const juce::File& file, const LatencyProbe& probe, double sampleRate) {
    juce::AudioBuffer<float> audio(1, static_cast<int>(std::llround(kClipSeconds * sampleRate)));
    audio.clear();
    for (std::size_t index = 0; index < probe.signature.size(); ++index)
        audio.setSample(0, probe.burstStartSample + static_cast<int>(index),
                        probe.signature[index]);

    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    if (stream == nullptr)
        return false;

    juce::WavAudioFormat wav;
    const auto options =
        juce::AudioFormatWriterOptions{}
            .withSampleRate(sampleRate)
            .withNumChannels(1)
            .withBitsPerSample(32)
            .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = wav.createWriterFor(stream, options);
    return writer != nullptr && writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
}

}  // namespace

std::optional<LatencyProbe> addLatencyProbe(StagedProjectData& staged, const juce::File& directory,
                                            double beat, double sampleRate) {
    LatencyProbe probe;
    probe.burstStartSample = static_cast<int>(std::llround(kBurstStartSeconds * sampleRate));

    std::mt19937 random(kBurstSeed);
    std::uniform_real_distribution<float> noise(-kBurstLevel, kBurstLevel);
    probe.signature.resize(static_cast<std::size_t>(std::llround(kBurstSeconds * sampleRate)));
    for (auto& sample : probe.signature)
        sample = noise(random);

    const auto file = directory.getChildFile("parity-probe.wav");
    if (!writeProbe(file, probe, sampleRate))
        return std::nullopt;

    staged.clips.clear();
    for (auto& track : staged.tracks)
        track.activeSessionClipId = INVALID_CLIP_ID;

    SourceId sourceId = 1;
    for (const auto& source : staged.sources)
        sourceId = std::max(sourceId, source.id + 1);

    Source source;
    source.id = sourceId;
    source.filePath = file.getFullPathName();
    source.sampleRate = sampleRate;
    source.durationSeconds = kClipSeconds;
    staged.sources.push_back(source);

    TrackId trackId = 1;
    for (const auto& track : staged.tracks)
        trackId = std::max(trackId, track.id + 1);

    TrackInfo track;
    track.id = trackId;
    track.type = TrackType::Media;
    track.name = "Parity Probe";
    track.audioOutputDevice = "master";
    staged.tracks.push_back(track);

    ClipInfo clip;
    clip.id = 1;
    clip.trackId = trackId;
    clip.name = "probe";
    clip.view = ClipView::Arrangement;
    clip.setAudioContent();
    AudioEvent event;
    event.sourceId = sourceId;
    clip.audio().addEvent(std::move(event));
    clip.setPlacementBeats(beat, kClipSeconds * staged.info.tempo / 60.0);
    clip.deriveTimesFromBeats(staged.info.tempo);
    staged.clips.push_back(clip);

    return probe;
}

std::optional<ProbeFinding> findLatencyProbe(const juce::File& exported, const LatencyProbe& probe,
                                             int maxLagSamples) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(exported));
    if (reader == nullptr)
        return std::nullopt;

    juce::AudioBuffer<float> audio(static_cast<int>(reader->numChannels),
                                   static_cast<int>(reader->lengthInSamples));
    reader->read(&audio, 0, audio.getNumSamples(), 0, true, true);

    std::vector<float> mono(static_cast<std::size_t>(audio.getNumSamples()), 0.0f);
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            mono[static_cast<std::size_t>(sample)] += audio.getSample(channel, sample);

    const auto length = static_cast<int>(probe.signature.size());
    const auto firstLag = std::max(-maxLagSamples, -probe.burstStartSample);
    const auto lastLag =
        std::min(maxLagSamples, static_cast<int>(mono.size()) - probe.burstStartSample - length);
    if (lastLag < firstLag)
        return std::nullopt;

    std::vector<double> correlation;
    correlation.reserve(static_cast<std::size_t>(lastLag - firstLag + 1));
    for (int lag = firstLag; lag <= lastLag; ++lag) {
        const auto* heard = mono.data() + probe.burstStartSample + lag;
        double sum = 0.0;
        for (int index = 0; index < length; ++index)
            sum += static_cast<double>(heard[index]) *
                   probe.signature[static_cast<std::size_t>(index)];
        correlation.push_back(std::abs(sum));
    }

    const auto peak = std::max_element(correlation.begin(), correlation.end());

    auto sorted = correlation;
    std::nth_element(sorted.begin(),
                     sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2), sorted.end());
    const auto median = sorted[sorted.size() / 2];

    ProbeFinding finding;
    finding.offsetSamples = firstLag + static_cast<int>(std::distance(correlation.begin(), peak));
    finding.confidence = *peak / std::max(median, 1.0e-12);
    return finding;
}

}  // namespace magda::parity
