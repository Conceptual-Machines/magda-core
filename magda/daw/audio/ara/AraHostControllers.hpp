// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "AraMusicalContent.hpp"
#include "io/AudioFileReader.hpp"

/**
 * @file AraHostControllers.hpp
 * @brief The host interfaces an ARA document controller calls back into (#1579).
 *
 * Every callback here may arrive on a plugin thread, so each one either touches state of its
 * own (a reader, an archive) or reads a snapshot under a lock.
 */

namespace magda::daw::audio::ara {

/** @brief What the host knows about one audio source; its address is the ARA host ref. */
struct AraSourceRecord {
    std::string path;
    double sampleRate = 0.0;
    std::int64_t lengthInSamples = 0;
    int numChannels = 0;
};

using AraSourceConverter =
    juce::ARAHostModel::ConversionFunctions<AraSourceRecord*, ARA::ARAAudioSourceHostRef>;

/**
 * @brief Source samples for the plugin, read through the engine's file readers.
 *
 * One AudioFileReader per ARA reader: ARA never uses a reader from two threads at once, and the
 * engine's readers are single-consumer, so neither side needs a lock.
 */
class AraAudioAccess final : public ARA::Host::AudioAccessControllerInterface {
  public:
    explicit AraAudioAccess(engine::AudioFileReaderFactory& files);

    ARA::ARAAudioReaderHostRef createAudioReaderForSource(ARA::ARAAudioSourceHostRef source,
                                                          bool use64BitSamples) noexcept override;
    bool readAudioSamples(ARA::ARAAudioReaderHostRef reader, ARA::ARASamplePosition position,
                          ARA::ARASampleCount samplesPerChannel,
                          void* const* buffers) noexcept override;
    void destroyAudioReader(ARA::ARAAudioReaderHostRef reader) noexcept override;

  private:
    struct Reader {
        std::unique_ptr<engine::AudioFileReader> file;
        int numChannels = 0;
    };

    using ReaderConverter =
        juce::ARAHostModel::ConversionFunctions<Reader*, ARA::ARAAudioReaderHostRef>;

    engine::AudioFileReaderFactory& files_;
    std::mutex mutex_;
    std::map<Reader*, std::unique_ptr<Reader>> readers_;
};

/** @brief An archive being restored: its bytes and the archive ID they were written under. */
struct AraArchiveIn {
    juce::MemoryBlock data;
    std::string archiveId;
};

using AraArchiveInConverter =
    juce::ARAHostModel::ConversionFunctions<AraArchiveIn*, ARA::ARAArchiveReaderHostRef>;
using AraArchiveOutConverter =
    juce::ARAHostModel::ConversionFunctions<juce::MemoryOutputStream*,
                                            ARA::ARAArchiveWriterHostRef>;

/** @brief Archives as memory; where the bytes live on disk is the caller's business. */
class AraArchiving final : public ARA::Host::ArchivingControllerInterface {
  public:
    ARA::ARASize getArchiveSize(ARA::ARAArchiveReaderHostRef reader) noexcept override;
    bool readBytesFromArchive(ARA::ARAArchiveReaderHostRef reader, ARA::ARASize position,
                              ARA::ARASize length, ARA::ARAByte* buffer) noexcept override;
    bool writeBytesToArchive(ARA::ARAArchiveWriterHostRef writer, ARA::ARASize position,
                             ARA::ARASize length, const ARA::ARAByte* buffer) noexcept override;
    void notifyDocumentArchivingProgress(float) noexcept override {}
    void notifyDocumentUnarchivingProgress(float) noexcept override {}
    ARA::ARAPersistentID getDocumentArchiveID(
        ARA::ARAArchiveReaderHostRef reader) noexcept override;
};

/** @brief The project's tempo and bar grid, the one musical content the host provides. */
class AraContentAccess final : public ARA::Host::ContentAccessControllerInterface {
  public:
    /** @brief Replace what new readers see; a reader already open keeps its own copy. */
    void setMusicalContent(std::vector<AraTempoEntry> tempo, std::vector<AraBarSignature> bars);

    bool isMusicalContextContentAvailable(ARA::ARAMusicalContextHostRef,
                                          ARA::ARAContentType type) noexcept override;
    ARA::ARAContentGrade getMusicalContextContentGrade(ARA::ARAMusicalContextHostRef,
                                                       ARA::ARAContentType) noexcept override;
    ARA::ARAContentReaderHostRef createMusicalContextContentReader(
        ARA::ARAMusicalContextHostRef, ARA::ARAContentType type,
        const ARA::ARAContentTimeRange* range) noexcept override;

    bool isAudioSourceContentAvailable(ARA::ARAAudioSourceHostRef,
                                       ARA::ARAContentType) noexcept override {
        return false;
    }
    ARA::ARAContentGrade getAudioSourceContentGrade(ARA::ARAAudioSourceHostRef,
                                                    ARA::ARAContentType) noexcept override {
        return ARA::kARAContentGradeInitial;
    }
    ARA::ARAContentReaderHostRef createAudioSourceContentReader(
        ARA::ARAAudioSourceHostRef, ARA::ARAContentType,
        const ARA::ARAContentTimeRange*) noexcept override {
        return nullptr;
    }

    ARA::ARAInt32 getContentReaderEventCount(ARA::ARAContentReaderHostRef reader) noexcept override;
    const void* getContentReaderDataForEvent(ARA::ARAContentReaderHostRef reader,
                                             ARA::ARAInt32 eventIndex) noexcept override;
    void destroyContentReader(ARA::ARAContentReaderHostRef reader) noexcept override;

  private:
    struct Reader {
        std::vector<ARA::ARAContentTempoEntry> tempo;
        std::vector<ARA::ARAContentBarSignature> bars;
        bool isTempo = true;
    };

    using ReaderConverter =
        juce::ARAHostModel::ConversionFunctions<Reader*, ARA::ARAContentReaderHostRef>;

    std::mutex mutex_;
    std::vector<AraTempoEntry> tempo_;
    std::vector<AraBarSignature> bars_{AraBarSignature{}};
    std::map<Reader*, std::unique_ptr<Reader>> readers_;
};

/** @brief What the plugin reports back; each hook may be empty. */
struct AraModelEvents {
    std::function<void(AraSourceRecord&, ARA::ARAAnalysisProgressState, float)> analysisProgress;
    std::function<void(ARA::ARAAudioModificationHostRef)> modificationContentChanged;
    std::function<void(ARA::ARAPlaybackRegionHostRef)> regionContentChanged;
    std::function<void()> documentDataChanged;
};

/** @brief Forwards the plugin's model notifications to @ref AraModelEvents. */
class AraModelUpdates final : public ARA::Host::ModelUpdateControllerInterface {
  public:
    explicit AraModelUpdates(const AraModelEvents& events) : events_(events) {}

    void notifyAudioSourceAnalysisProgress(ARA::ARAAudioSourceHostRef source,
                                           ARA::ARAAnalysisProgressState state,
                                           float value) noexcept override;
    void notifyAudioSourceContentChanged(ARA::ARAAudioSourceHostRef,
                                         const ARA::ARAContentTimeRange*,
                                         ARA::ContentUpdateScopes) noexcept override {}
    void notifyAudioModificationContentChanged(ARA::ARAAudioModificationHostRef modification,
                                               const ARA::ARAContentTimeRange*,
                                               ARA::ContentUpdateScopes) noexcept override;
    void notifyPlaybackRegionContentChanged(ARA::ARAPlaybackRegionHostRef region,
                                            const ARA::ARAContentTimeRange*,
                                            ARA::ContentUpdateScopes) noexcept override;
    void notifyDocumentDataChanged() noexcept override;

  private:
    const AraModelEvents& events_;
};

/** @brief The plugin asking the host's transport to move; each hook may be empty. */
struct AraTransportRequests {
    std::function<void()> start;
    std::function<void()> stop;
    std::function<void(double seconds)> setPosition;
    std::function<void(double startSeconds, double durationSeconds)> setCycleRange;
    std::function<void(bool)> enableCycle;
};

/** @brief Forwards the plugin's transport requests to @ref AraTransportRequests. */
class AraPlaybackRequests final : public ARA::Host::PlaybackControllerInterface {
  public:
    explicit AraPlaybackRequests(const AraTransportRequests& requests) : requests_(requests) {}

    void requestStartPlayback() noexcept override;
    void requestStopPlayback() noexcept override;
    void requestSetPlaybackPosition(ARA::ARATimePosition seconds) noexcept override;
    void requestSetCycleRange(ARA::ARATimePosition start,
                              ARA::ARATimeDuration duration) noexcept override;
    void requestEnableCycle(bool enable) noexcept override;

  private:
    const AraTransportRequests& requests_;
};

}  // namespace magda::daw::audio::ara
