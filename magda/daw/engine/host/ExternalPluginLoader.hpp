#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "../../audio/plugins/engine/EngineDeviceFactory.hpp"

/**
 * @file ExternalPluginLoader.hpp
 * @brief The plugins a live plan names, loaded while the project keeps playing (#2566).
 *
 * A Device op is an identity, and for an external plugin turning it into
 * something that renders means finding a file on this machine and asking a
 * third party to open it, which takes seconds. A session cannot wait for that:
 * the publish that asks runs on the message thread, and a project of ten
 * plugins opened one after another is a frozen window.
 *
 * So a load is started and the op is left unbound, which the executor already
 * renders as a pass-through. When the instance arrives, the model is corrected
 * from what the plugin turned out to be and the plan is compiled again. That is
 * the same two-pass shape the corpus runs in one go
 * (tests/NullDiffNativeLeg.cpp), spread over as many message-loop turns as the
 * plugins take.
 *
 * Message thread only, which is where a publish and JUCE's own load completion
 * both run.
 */

namespace magda::daw::engine_host {

class ExternalPluginLoader final {
  public:
    /**
     * @brief What an instance turned out to be, for the model to record.
     *
     * @p resolved is the device as the live plugin describes it -- bus widths,
     * MIDI capability, role -- and @p restored what it holds after its own
     * saved state went back on. The host writes both and publishes a plan
     * compiled from the result.
     */
    using Loaded = std::function<void(engine::DeviceKey key, const DeviceInfo& resolved,
                                      const std::vector<magda::RestoredParameter>& restored)>;

    /// @p currentDevice is read at completion rather than captured at request
    /// time, so a slow load cannot clobber an edit made while it ran.
    ExternalPluginLoader(audio::engine_adapter::CurrentDeviceLookup currentDevice, Loaded loaded);

    ExternalPluginLoader(const ExternalPluginLoader&) = delete;
    ExternalPluginLoader& operator=(const ExternalPluginLoader&) = delete;

    /// Where plugins are found and what can open them. Both are the fork's,
    /// which owns the scan; without them nothing external loads at all.
    void setServices(juce::AudioPluginFormatManager* formats,
                     const juce::KnownPluginList* knownPlugins);

    /// What an instance is created at. The session's, and it changes only with
    /// the audio device.
    void setContext(const engine::RenderContext& context);

    /**
     * @brief Register what @p devices says is live, before anything is asked for.
     *
     * The identity boundary's own bookkeeping (PluginAssignments.hpp): a slot
     * still holding the plugin it held keeps its assignment, so a load in
     * flight stays wanted; a slot now naming a different plugin gets a new one,
     * which expires every load outstanding against the old.
     *
     * Once per publish, from EngineRuntimeFactory::setModel.
     */
    void syncAssignments(const std::map<engine::DeviceKey, DeviceInfo>& devices);

    /**
     * @brief The instance for @p key, or nothing yet.
     *
     * Starts a load for a slot that has none and returns null, which the store
     * asks about again at the next publish. Null is also the answer while a
     * load is running and for one that has already failed.
     */
    std::unique_ptr<engine::EngineDevice> device(engine::DeviceKey key, const DeviceInfo& model);

    /// How many plugins are loaded and waiting to be bound. For tests.
    std::size_t held() const;

    /// How many are still opening. What tells a plan whose plugins have not
    /// arrived yet from one whose plugins are never coming.
    std::size_t loading() const;

  private:
    /// One slot's whole state: which plugin it asks for, which assignment that
    /// is, and where its load has got to.
    struct Slot {
        juce::String identity;
        std::uint64_t generation = 0;

        bool pending = false;

        /// The load ran and did not produce an instance, so it is not run
        /// again: a plugin that failed to open takes seconds to fail again, and
        /// a publish per edit would spend them for the rest of the session.
        bool spent = false;

        /// Whether the failure has been said. Kept across a retry so a plugin
        /// the scan cannot find is named once rather than once per publish.
        bool reported = false;

        std::unique_ptr<engine::EngineDevice> instance;
    };

    void complete(engine::DeviceKey key, std::uint64_t generation,
                  audio::engine_adapter::ExternalDeviceResult result);

    audio::engine_adapter::CurrentDeviceLookup currentDevice_;
    Loaded loaded_;

    audio::engine_adapter::ExternalPluginServices services_;
    audio::engine_adapter::PluginAssignments assignments_;

    std::map<engine::DeviceKey, Slot> slots_;

    /// Never reused, so a completion can tell the assignment it was started
    /// against from the one holding the key now -- including a key released and
    /// registered again, which starts a fresh slot.
    std::uint64_t generation_ = 0;
};

}  // namespace magda::daw::engine_host
