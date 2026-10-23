#include "AraHostControllers.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace magda::daw::audio::ara {

// ============================================================================
// AraAudioAccess
// ============================================================================

AraAudioAccess::AraAudioAccess(engine::AudioFileReaderFactory& files) : files_(files) {}

ARA::ARAAudioReaderHostRef AraAudioAccess::createAudioReaderForSource(
    ARA::ARAAudioSourceHostRef source, bool use64BitSamples) noexcept {
    // Every source is declared without 64-bit merit, so a plugin asking for doubles is one
    // that ignored that; refusing is what the API allows.
    const auto* record = AraSourceConverter::fromHostRef(source);
    if (use64BitSamples)
        return nullptr;

    auto file = files_.open(record->path);
    if (file == nullptr) {
        juce::Logger::writeToLog("[ara] could not open " + juce::String(record->path));
        return nullptr;
    }

    auto reader = std::make_unique<Reader>(
        Reader{.file = std::move(file), .numChannels = record->numChannels});
    auto* raw = reader.get();
    const std::scoped_lock lock(mutex_);
    readers_.emplace(raw, std::move(reader));
    return ReaderConverter::toHostRef(raw);
}

bool AraAudioAccess::readAudioSamples(ARA::ARAAudioReaderHostRef readerRef,
                                      ARA::ARASamplePosition position,
                                      ARA::ARASampleCount samplesPerChannel,
                                      void* const* buffers) noexcept {
    auto* reader = ReaderConverter::fromHostRef(readerRef);
    auto* const* channels = reinterpret_cast<float* const*>(buffers);

    // read() takes an int count; ARA's is 64-bit.
    constexpr ARA::ARASampleCount kChunk = std::numeric_limits<int>::max() / 2;
    std::vector<float*> offsets(static_cast<std::size_t>(reader->numChannels));

    for (ARA::ARASampleCount done = 0; done < samplesPerChannel;) {
        const auto count = static_cast<int>(std::min(kChunk, samplesPerChannel - done));
        for (int c = 0; c < reader->numChannels; ++c)
            offsets[static_cast<std::size_t>(c)] = channels[c] + done;

        juce::AudioBuffer<float> view(offsets.data(), reader->numChannels, count);
        // Past the file's end read() zero-fills, which is what ARA asks for there too.
        reader->file->read(view, 0, position + done, count);
        done += count;
    }
    return true;
}

void AraAudioAccess::destroyAudioReader(ARA::ARAAudioReaderHostRef readerRef) noexcept {
    const std::scoped_lock lock(mutex_);
    readers_.erase(ReaderConverter::fromHostRef(readerRef));
}

// ============================================================================
// AraArchiving
// ============================================================================

ARA::ARASize AraArchiving::getArchiveSize(ARA::ARAArchiveReaderHostRef reader) noexcept {
    return static_cast<ARA::ARASize>(AraArchiveInConverter::fromHostRef(reader)->data.getSize());
}

bool AraArchiving::readBytesFromArchive(ARA::ARAArchiveReaderHostRef reader, ARA::ARASize position,
                                        ARA::ARASize length, ARA::ARAByte* buffer) noexcept {
    const auto& data = AraArchiveInConverter::fromHostRef(reader)->data;
    if (position + length > data.getSize())
        return false;

    std::memcpy(buffer, juce::addBytesToPointer(data.getData(), position), length);
    return true;
}

bool AraArchiving::writeBytesToArchive(ARA::ARAArchiveWriterHostRef writer, ARA::ARASize position,
                                       ARA::ARASize length, const ARA::ARAByte* buffer) noexcept {
    auto* stream = AraArchiveOutConverter::fromHostRef(writer);
    return stream->setPosition(static_cast<juce::int64>(position)) && stream->write(buffer, length);
}

ARA::ARAPersistentID AraArchiving::getDocumentArchiveID(
    ARA::ARAArchiveReaderHostRef reader) noexcept {
    const auto& id = AraArchiveInConverter::fromHostRef(reader)->archiveId;
    return id.empty() ? nullptr : id.c_str();
}

// ============================================================================
// AraContentAccess
// ============================================================================

void AraContentAccess::setMusicalContent(std::vector<AraTempoEntry> tempo,
                                         std::vector<AraBarSignature> bars) {
    const std::scoped_lock lock(mutex_);
    tempo_ = std::move(tempo);
    bars_ = std::move(bars);
}

bool AraContentAccess::isMusicalContextContentAvailable(ARA::ARAMusicalContextHostRef,
                                                        ARA::ARAContentType type) noexcept {
    return type == ARA::kARAContentTypeTempoEntries || type == ARA::kARAContentTypeBarSignatures;
}

ARA::ARAContentGrade AraContentAccess::getMusicalContextContentGrade(ARA::ARAMusicalContextHostRef,
                                                                     ARA::ARAContentType) noexcept {
    // The user's own tempo track, not a guess.
    return ARA::kARAContentGradeAdjusted;
}

ARA::ARAContentReaderHostRef AraContentAccess::createMusicalContextContentReader(
    ARA::ARAMusicalContextHostRef, ARA::ARAContentType type,
    const ARA::ARAContentTimeRange*) noexcept {
    // The range is a hint; the whole grid is small enough to hand over every time.
    auto reader = std::make_unique<Reader>();
    reader->isTempo = type == ARA::kARAContentTypeTempoEntries;

    const std::scoped_lock lock(mutex_);
    for (const auto& entry : tempo_)
        reader->tempo.push_back({entry.seconds, entry.quarters});
    for (const auto& bar : bars_)
        reader->bars.push_back({bar.numerator, bar.denominator, bar.quarters});

    auto* raw = reader.get();
    readers_.emplace(raw, std::move(reader));
    return ReaderConverter::toHostRef(raw);
}

ARA::ARAInt32 AraContentAccess::getContentReaderEventCount(
    ARA::ARAContentReaderHostRef readerRef) noexcept {
    const auto* reader = ReaderConverter::fromHostRef(readerRef);
    return static_cast<ARA::ARAInt32>(reader->isTempo ? reader->tempo.size() : reader->bars.size());
}

const void* AraContentAccess::getContentReaderDataForEvent(ARA::ARAContentReaderHostRef readerRef,
                                                           ARA::ARAInt32 eventIndex) noexcept {
    const auto* reader = ReaderConverter::fromHostRef(readerRef);
    const auto index = static_cast<std::size_t>(eventIndex);
    if (reader->isTempo)
        return index < reader->tempo.size() ? &reader->tempo[index] : nullptr;
    return index < reader->bars.size() ? &reader->bars[index] : nullptr;
}

void AraContentAccess::destroyContentReader(ARA::ARAContentReaderHostRef readerRef) noexcept {
    const std::scoped_lock lock(mutex_);
    readers_.erase(ReaderConverter::fromHostRef(readerRef));
}

// ============================================================================
// AraModelUpdates
// ============================================================================

void AraModelUpdates::notifyAudioSourceAnalysisProgress(ARA::ARAAudioSourceHostRef source,
                                                        ARA::ARAAnalysisProgressState state,
                                                        float value) noexcept {
    if (events_.analysisProgress)
        events_.analysisProgress(*AraSourceConverter::fromHostRef(source), state, value);
}

void AraModelUpdates::notifyAudioModificationContentChanged(
    ARA::ARAAudioModificationHostRef modification, const ARA::ARAContentTimeRange*,
    ARA::ContentUpdateScopes) noexcept {
    if (events_.modificationContentChanged)
        events_.modificationContentChanged(modification);
}

void AraModelUpdates::notifyPlaybackRegionContentChanged(ARA::ARAPlaybackRegionHostRef region,
                                                         const ARA::ARAContentTimeRange*,
                                                         ARA::ContentUpdateScopes) noexcept {
    if (events_.regionContentChanged)
        events_.regionContentChanged(region);
}

void AraModelUpdates::notifyDocumentDataChanged() noexcept {
    if (events_.documentDataChanged)
        events_.documentDataChanged();
}

// ============================================================================
// AraPlaybackRequests
// ============================================================================

void AraPlaybackRequests::requestStartPlayback() noexcept {
    if (requests_.start)
        requests_.start();
}

void AraPlaybackRequests::requestStopPlayback() noexcept {
    if (requests_.stop)
        requests_.stop();
}

void AraPlaybackRequests::requestSetPlaybackPosition(ARA::ARATimePosition seconds) noexcept {
    if (requests_.setPosition)
        requests_.setPosition(seconds);
}

void AraPlaybackRequests::requestSetCycleRange(ARA::ARATimePosition start,
                                               ARA::ARATimeDuration duration) noexcept {
    if (requests_.setCycleRange)
        requests_.setCycleRange(start, duration);
}

void AraPlaybackRequests::requestEnableCycle(bool enable) noexcept {
    if (requests_.enableCycle)
        requests_.enableCycle(enable);
}

}  // namespace magda::daw::audio::ara
