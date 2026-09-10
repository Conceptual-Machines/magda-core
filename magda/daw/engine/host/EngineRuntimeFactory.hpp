#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <map>
#include <memory>
#include <vector>

#include "EngineTrace.hpp"
#include "clip/ClipSnapshotFeed.hpp"
#include "clip/ClipStreamFeed.hpp"
#include "core/DeviceInfo.hpp"
#include "core/TrackInfo.hpp"
#include "exec/RuntimeStateStore.hpp"
#include "io/AudioFileReader.hpp"
#include "launch/SessionLauncher.hpp"

/**
 * @file EngineRuntimeFactory.hpp
 * @brief What the app answers when the engine asks for a runtime object.
 *
 * The engine knows a section-aware DeviceKey and a TrackId; which plugin that
 * key is, and which formats this build can decode, are the host's business
 * (RuntimeStateStore.hpp). This is the host's side of that split, and the only
 * place in the app that knows both halves.
 *
 * Everything here runs on the publishing thread, inside a publish.
 */

namespace magda::daw::engine_host {

/// Opens the files a snapshot names. The one thing the engine leaves entirely
/// to the host: a snapshot carries paths because that is what the model has.
class EngineFileReaders final : public engine::AudioFileReaderFactory {
  public:
    EngineFileReaders();

    std::unique_ptr<engine::AudioFileReader> open(const std::string& path) override;

  private:
    juce::AudioFormatManager formats_;
};

/**
 * @brief The runtime objects behind a live plan's leaf ops.
 *
 * Built before the session it serves, because the session takes it by
 * reference, so the feeds it hands its sources arrive afterwards through
 * @ref attach. Asking for a source before that returns null, which the executor
 * reports as an unbound op rather than treating as silence.
 */
class EngineRuntimeFactory final : public engine::RuntimeStateFactory {
  public:
    /// The feeds every source it makes will read. Once, before the first
    /// publish; all three outlive every source bound into any plan.
    void attach(engine::ClipSnapshotFeed& clips, engine::ClipStreamFeed& streams,
                engine::LaunchHandleFeed& handles);

    /**
     * @brief What the model holds now, for the publish about to happen.
     *
     * Devices are asked for by key during publish(), and the key alone says
     * nothing about which plugin it is. Refreshed rather than looked up live so
     * that every device a publish creates comes from one reading of the model.
     */
    void setModel(const std::vector<TrackInfo>& tracks, const TrackInfo& master);

    /// Devices the model names that no catalog could build, by display name.
    /// Read after a publish; external plugins are all of them for now (#2566).
    const std::vector<juce::String>& unbuilt() const {
        return unbuilt_;
    }

    /// Record what reaches every device this makes from now on (#2568). Set
    /// before the first publish, and only when the trace is switched on: a
    /// device already made is not wrapped retrospectively.
    void traceInto(EngineTrace& trace) {
        trace_ = &trace;
    }

    std::unique_ptr<engine::EngineDevice> createDevice(engine::DeviceKey key) override;
    std::unique_ptr<engine::EngineAudioSource> createClipAudioSource(TrackId trackId) override;
    std::unique_ptr<engine::EngineMidiSource> createClipMidiSource(TrackId trackId) override;
    std::unique_ptr<engine::EngineAudioSource> createSessionAudioSource(TrackId trackId) override;
    std::unique_ptr<engine::EngineMidiSource> createSessionMidiSource(TrackId trackId) override;

  private:
    std::unique_ptr<engine::EngineAudioSource> audioSource(TrackId trackId,
                                                           engine::Section section);
    std::unique_ptr<engine::EngineMidiSource> midiSource(TrackId trackId, engine::Section section);

    engine::ClipSnapshotFeed* clips_ = nullptr;
    engine::ClipStreamFeed* streams_ = nullptr;
    engine::LaunchHandleFeed* handles_ = nullptr;

    std::map<engine::DeviceKey, DeviceInfo> devices_;
    std::vector<juce::String> unbuilt_;
    EngineTrace* trace_ = nullptr;
};

}  // namespace magda::daw::engine_host
