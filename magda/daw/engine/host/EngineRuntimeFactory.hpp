#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "EngineTrace.hpp"
#include "ExternalPluginLoader.hpp"
#include "LiveMidiSources.hpp"
#include "clip/ClipSnapshotFeed.hpp"
#include "clip/ClipStreamFeed.hpp"
#include "core/DeviceInfo.hpp"
#include "core/TrackInfo.hpp"
#include "exec/RuntimeStateStore.hpp"
#include "io/AudioFileReader.hpp"
#include "io/LiveInput.hpp"
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
    /// The feeds every source it makes will read, and the registry a live
    /// route resolves through. Once, before the first publish; all of them
    /// outlive every source bound into any plan.
    void attach(engine::ClipSnapshotFeed& clips, engine::ClipStreamFeed& streams,
                engine::LaunchHandleFeed& handles, const engine::LiveInputFeed& liveInputs,
                LiveMidiSources& sources);

    /**
     * @brief What the model holds now, for the publish about to happen.
     *
     * Devices are asked for by key during publish(), and the key alone says
     * nothing about which plugin it is. Refreshed rather than looked up live so
     * that every device a publish creates comes from one reading of the model.
     */
    void setModel(const std::vector<TrackInfo>& tracks, const TrackInfo& master);

    /// Devices the model names that no catalog could build, by display name.
    /// Read after a publish. External plugins are not among them: they are not
    /// built from a catalog at all (#2566).
    const std::vector<juce::String>& unbuilt() const {
        return unbuilt_;
    }

    /// Where external plugins come from. Set before the first publish; without
    /// one every external device in the project stays unbound, which the
    /// executor renders as a pass-through.
    void loadExternalsWith(ExternalPluginLoader& loader) {
        externals_ = &loader;
    }

    /// External plugins this publish is still waiting on. A plan with unbound
    /// device ops and none of these is a plan whose plugins are not coming.
    std::size_t loadingExternals() const {
        return externals_ != nullptr ? externals_->loading() : 0;
    }

    /// Record what reaches every device this makes from now on (#2568). Set
    /// before the first publish, and only when the trace is switched on: a
    /// device already made is not wrapped retrospectively.
    void traceInto(EngineTrace& trace) {
        trace_ = &trace;
    }

    /// Nothing the store holds belongs to the model any more: the project was
    /// cleared and DeviceIds start from 1 again (#2572). Called at the
    /// teardown, which the next publish is too late to see.
    void forgetBuiltDevices();

    std::unique_ptr<engine::EngineDevice> createDevice(engine::DeviceKey key) override;
    std::set<engine::DeviceKey> devicesToRebuild() override;
    std::unique_ptr<engine::LevelTap> createMeter(const engine::OpKey& key) override;
    std::unique_ptr<engine::EngineAudioSource> createClipAudioSource(TrackId trackId) override;
    std::unique_ptr<engine::EngineMidiSource> createClipMidiSource(TrackId trackId) override;
    std::unique_ptr<engine::EngineAudioSource> createSessionAudioSource(TrackId trackId) override;
    std::unique_ptr<engine::EngineMidiSource> createSessionMidiSource(TrackId trackId) override;

    /// The track's audition, and the devices its route names. createAudioInput
    /// is deliberately left unoverridden: live audio is #2553's.
    std::unique_ptr<engine::EngineMidiSource> createMidiInput(TrackId trackId) override;

    /**
     * @brief A track's routed device sources, as a table the publisher rewrites.
     *
     * The store builds a track's input once and keeps it across every later
     * publish, so a monitor or route change after that has to reach the input
     * some other way: this is written on the publishing thread and read on the
     * audio thread, count last so a reader sees whole entries.
     */
    struct MidiRouteTable {
        static constexpr int kMaxSources = 32;
        std::array<std::atomic<engine::LiveMidiSourceId>, kMaxSources> sources{};
        std::atomic<int> count{0};

        /// A source has left the table since the last block. Read and cleared
        /// by the input, which turns it into the panic its instrument needs to
        /// release the notes that source will never send an off for. Mutable
        /// because the input holds the table as const and consuming this is
        /// the one thing it writes.
        mutable std::atomic<bool> dropped{false};

        void set(const std::vector<engine::LiveMidiSourceId>& ids);
    };

    /// Every track's routed sources from the model as it is now. setModel
    /// calls it; the host calls it again on a values publish, which is where
    /// a monitor change arrives.
    void refreshMidiRoutes(const std::vector<TrackInfo>& tracks);

  private:
    /// What a track's MIDI input op resolves through.
    struct MidiRoute {
        juce::String device;
        bool monitors = false;
    };

    /// The device sources @p route names, resolved here rather than in
    /// render(): this runs on the publishing thread.
    std::vector<engine::LiveMidiSourceId> routedSources(const MidiRoute& route);

    std::shared_ptr<MidiRouteTable> routeTableFor(TrackId trackId);

    /// Every stored route into its table. Needs the registry, so it runs from
    /// whichever of setModel and attach comes second.
    void resolveRouteTables();

    std::unique_ptr<engine::EngineDevice> handOver(engine::DeviceKey key, const DeviceInfo& model,
                                                   std::unique_ptr<engine::EngineDevice> device);
    std::unique_ptr<engine::EngineAudioSource> audioSource(TrackId trackId,
                                                           engine::Section section);
    std::unique_ptr<engine::EngineMidiSource> midiSource(TrackId trackId, engine::Section section);

    engine::ClipSnapshotFeed* clips_ = nullptr;
    engine::ClipStreamFeed* streams_ = nullptr;
    engine::LaunchHandleFeed* handles_ = nullptr;
    const engine::LiveInputFeed* liveInputs_ = nullptr;
    LiveMidiSources* sources_ = nullptr;

    /// The model's routes as of the last setModel or refresh.
    std::map<TrackId, MidiRoute> midiRoutes_;

    /// Shared with the input that reads it, so a table outlives the store's
    /// eviction of that input and the factory's copy is never dangling.
    std::map<TrackId, std::shared_ptr<MidiRouteTable>> routeTables_;

    std::map<engine::DeviceKey, DeviceInfo> devices_;

    /// Which device each key's live instance was built from. Kept for a key
    /// the model drops, since only a publish that succeeded evicts one; that
    /// is an entry and a short string per DeviceKey ever realised.
    std::map<engine::DeviceKey, juce::String> built_;

    /// What the next publish must rebuild.
    std::set<engine::DeviceKey> rebuild_;

    std::vector<juce::String> unbuilt_;
    EngineTrace* trace_ = nullptr;
    ExternalPluginLoader* externals_ = nullptr;
};

}  // namespace magda::daw::engine_host
