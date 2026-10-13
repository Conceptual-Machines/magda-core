#include "io/AudioFileSink.hpp"

#include <algorithm>
#include <utility>

#include "io/Mp3AudioFormat.hpp"

namespace magda::engine {

/**
 * @brief A file stream that keeps what the writer never reports.
 *
 * The writer owns its stream and finishes the file while it is being destroyed:
 * a WAV header rewritten over the front, a FLAC encoder flushed. A disk that
 * has filled up fails there, after the last write anything could have checked,
 * and the writer is already gone by the time anyone could ask. So the report is
 * held apart from both and read once it is.
 *
 * Bytes as well as failures. JUCE's stream keeps only its last operation's
 * result, and a flush that fails inside a seek is overwritten by the header
 * write that follows it, so a file can end up short with nothing having
 * returned false. What the writer handed over is the length the file has to be.
 */
struct StreamReport {
    bool failed = false;
    std::int64_t bytes = 0;
};

namespace {

std::unique_ptr<juce::AudioFormat> formatFor(AudioFileFormat format) {
    switch (format) {
        case AudioFileFormat::flac:
            return std::make_unique<juce::FlacAudioFormat>();
        case AudioFileFormat::opus:
            return std::make_unique<juce::OpusAudioFormat>();
        case AudioFileFormat::mp3:
            return std::make_unique<Mp3AudioFormat>();
        case AudioFileFormat::wav:
            break;
    }
    return std::make_unique<juce::WavAudioFormat>();
}

/// The writer's quality option for @p spec: no bitrate is the format's first
/// option (the encoder's own choice), and a rate or level it does not list is unset.
std::optional<int> qualityOptionFor(juce::AudioFormat& format, const AudioFileSpec& spec) {
    if (!isLossy(spec.format))
        return spec.vbrQuality ? std::nullopt : std::optional<int>(0);
    if (spec.vbrQuality && spec.bitrateKbps != 0)
        return std::nullopt;

    juce::String option;
    if (spec.vbrQuality)
        option = "V" + juce::String(*spec.vbrQuality);
    else if (spec.bitrateKbps != 0)
        option = juce::String(spec.bitrateKbps) + " kbps";
    else
        return 0;

    const auto index = format.getQualityOptions().indexOf(option);
    return index > 0 ? std::optional<int>(index) : std::nullopt;
}

class ReportingStream final : public juce::OutputStream {
  public:
    ReportingStream(std::unique_ptr<juce::FileOutputStream> file,
                    std::shared_ptr<StreamReport> report)
        : file_(std::move(file)), report_(std::move(report)) {}

    ~ReportingStream() override {
        flush();
    }

    ReportingStream(const ReportingStream&) = delete;
    ReportingStream& operator=(const ReportingStream&) = delete;
    ReportingStream(ReportingStream&&) = delete;
    ReportingStream& operator=(ReportingStream&&) = delete;

    void flush() override {
        file_->flush();
        note(file_->getStatus().wasOk());
    }

    bool write(const void* data, std::size_t numBytes) override {
        const auto ok = note(file_->write(data, numBytes));
        report_->bytes = std::max(report_->bytes, static_cast<std::int64_t>(file_->getPosition()));
        return ok;
    }

    bool setPosition(juce::int64 position) override {
        return note(file_->setPosition(position));
    }

    juce::int64 getPosition() override {
        return file_->getPosition();
    }

  private:
    bool note(bool ok) {
        if (!ok)
            report_->failed = true;
        return ok;
    }

    std::unique_ptr<juce::FileOutputStream> file_;
    std::shared_ptr<StreamReport> report_;
};

}  // namespace

DitherMode defaultDitherFor(int bitDepth) {
    return bitDepth >= 32 ? DitherMode::none : DitherMode::tpdf;
}

bool isLossy(AudioFileFormat format) {
    return format == AudioFileFormat::opus || format == AudioFileFormat::mp3;
}

juce::String extensionFor(AudioFileFormat format) {
    switch (format) {
        case AudioFileFormat::flac:
            return ".flac";
        case AudioFileFormat::opus:
            return ".opus";
        case AudioFileFormat::mp3:
            return ".mp3";
        case AudioFileFormat::wav:
            break;
    }
    return ".wav";
}

std::unique_ptr<AudioFileSink> AudioFileSink::create(const juce::File& destination,
                                                     const AudioFileSpec& spec,
                                                     const RenderContext& context) {
    const auto channels = juce::jmax(1, context.numChannels);

    // 32 bits means float. A 32-bit integer file is not a thing anything asks
    // for, and FLAC refuses the depth itself, so this is the only place the two
    // ways of spending 32 bits have to be told apart. A lossy encoder has no
    // grid to quantise to, so it takes the render as float.
    const auto lossy = isLossy(spec.format);
    const auto floating = lossy || spec.bitDepth >= 32;
    const auto bitDepth = lossy ? 32 : spec.bitDepth;

    auto format = formatFor(spec.format);

    const auto qualityOption = qualityOptionFor(*format, spec);
    if (!qualityOption)
        return nullptr;

    // Beside the destination, never over it. Everything below can still refuse
    // -- a format that will not take the channel count, a disk that will not
    // open -- and a caller re-rendering an export over yesterday's must not
    // lose yesterday's to a render that never ran. The destination is written
    // once, in close(), by a render that reached the end.
    auto temporary = std::make_unique<juce::TemporaryFile>(destination);

    auto file = std::make_unique<juce::FileOutputStream>(temporary->getFile());
    if (!file->openedOk())
        return nullptr;

    auto report = std::make_shared<StreamReport>();
    std::unique_ptr<juce::OutputStream> stream =
        std::make_unique<ReportingStream>(std::move(file), report);

    auto options =
        juce::AudioFormatWriterOptions()
            .withSampleRate(context.sampleRate)
            .withNumChannels(channels)
            .withBitsPerSample(bitDepth)
            .withQualityOptionIndex(*qualityOption)
            .withSampleFormat(floating ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                       : juce::AudioFormatWriterOptions::SampleFormat::integral);
    if (spec.format == AudioFileFormat::wav)
        options = options.withMetadataValues(wavMetadataFor(spec.metadata));
    else if (lossy)
        options = options.withMetadataValues(lossyTagsFor(spec.metadata));

    auto* handedOver = stream.get();
    auto writer = format->createWriterFor(stream, options);

    if (writer == nullptr) {
        // A failed createWriterFor is documented to leave the stream where it
        // was, and JUCE's FLAC writer does not: an encoder that refuses the
        // channel count or the rate takes the pointer and then drops it
        // (~FlacWriter, juce_FlacAudioFormat.cpp), which leaks the open file.
        // Reclaimed here rather than leaked. Worth re-reading on a JUCE bump --
        // a version that deleted it instead would make this a double free, and
        // the FLAC refusal case in test_audio_file_sink.cpp is what says so.
        const std::unique_ptr<juce::OutputStream> reclaimed(stream == nullptr ? handedOver
                                                                              : nullptr);
        return nullptr;
    }

    std::optional<PcmQuantiser> quantiser;
    if (!floating)
        quantiser.emplace(spec.bitDepth, channels,
                          spec.dither.value_or(defaultDitherFor(spec.bitDepth)));

    return std::unique_ptr<AudioFileSink>(new AudioFileSink(std::move(writer), std::move(temporary),
                                                            std::move(report), channels,
                                                            std::move(quantiser)));
}

AudioFileSink::AudioFileSink(std::unique_ptr<juce::AudioFormatWriter> writer,
                             std::unique_ptr<juce::TemporaryFile> temporary,
                             std::shared_ptr<StreamReport> report, int numChannels,
                             std::optional<PcmQuantiser> quantiser)
    : writer_(std::move(writer)),
      temporary_(std::move(temporary)),
      report_(std::move(report)),
      numChannels_(numChannels),
      quantiser_(std::move(quantiser)) {}

AudioFileSink::~AudioFileSink() {
    close();
}

void AudioFileSink::write(const juce::AudioBuffer<float>& block, int numSamples) {
    // The const buffer picks AudioBlock's read-pointer constructor, so nothing
    // here marks the caller's buffer as written to.
    write(juce::dsp::AudioBlock<const float>(block), numSamples);
}

void AudioFileSink::write(juce::dsp::AudioBlock<const float> block, int numSamples) {
    if (failed_ || writer_ == nullptr || numSamples <= 0)
        return;

    // A block narrower than the file has no samples for the channels past its
    // own, and the writer would store whatever was in that memory. Refused, so
    // the file ends where the render stopped making sense.
    if (static_cast<int>(block.getNumChannels()) < numChannels_) {
        failed_ = true;
        return;
    }

    sourceChannels_.resize(static_cast<std::size_t>(numChannels_));
    for (auto channel = 0; channel < numChannels_; ++channel)
        sourceChannels_[static_cast<std::size_t>(channel)] =
            block.getChannelPointer(static_cast<std::size_t>(channel));

    if (!quantiser_) {
        failed_ = !writer_->writeFromFloatArrays(sourceChannels_.data(), numChannels_, numSamples);
    } else {
        resizeCodes(numSamples);

        for (auto channel = 0; channel < numChannels_; ++channel)
            scratch_.copyFrom(channel, 0, sourceChannels_[static_cast<std::size_t>(channel)],
                              numSamples);

        quantiser_->processToCodes(scratch_, numSamples, codeChannels_.data());

        // The integer path, which is the only one allowed after the quantiser:
        // writeFromAudioSampleBuffer would round the codes a second time
        // through INT_MAX and land some of them one low (io/PcmQuantiser.hpp).
        failed_ = !writer_->write(const_cast<const int**>(codeChannels_.data()), numSamples);
    }

    if (!failed_)
        samplesWritten_ += numSamples;
}

void AudioFileSink::resizeCodes(int numSamples) {
    if (numSamples <= codeCapacity_)
        return;

    codeCapacity_ = numSamples;
    codes_.assign(static_cast<std::size_t>(numChannels_) * static_cast<std::size_t>(numSamples), 0);

    // One past the channels, left null: JUCE's own writers stop at a null
    // pointer as well as at the channel count.
    codeChannels_.assign(static_cast<std::size_t>(numChannels_) + 1, nullptr);
    for (auto channel = 0; channel < numChannels_; ++channel)
        codeChannels_[static_cast<std::size_t>(channel)] =
            codes_.data() + (static_cast<std::ptrdiff_t>(channel) * numSamples);

    scratch_.setSize(numChannels_, numSamples, false, false, true);
}

bool AudioFileSink::close() {
    if (closed_)
        return !failed_;

    closed_ = true;

    // Destroying the writer is what finishes the file: the header it could not
    // write until the length was known, and whatever the encoder still holds.
    writer_.reset();

    // Only now, because until now the stream had nothing to say. Short counts
    // as failed: the bytes the writer handed over are the length the file has
    // to be, and one that stops shorter is a render the disk did not take.
    if (report_->failed || temporary_->getFile().getSize() != report_->bytes)
        failed_ = true;

    // The one moment the destination changes. A render that failed leaves it
    // alone, so what was there yesterday survives today's full disk.
    if (failed_)
        return false;

    if (!temporary_->overwriteTargetFileWithTemporary()) {
        failed_ = true;
        return false;
    }

    return true;
}

}  // namespace magda::engine
