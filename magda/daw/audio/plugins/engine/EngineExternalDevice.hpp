#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "core/DeviceInfo.hpp"
#include "core/HostedParameterEdit.hpp"
#include "core/ParameterInfo.hpp"
#include "exec/EngineDevice.hpp"
#include "plugin_manager/ExternalPluginState.hpp"

/**
 * @file EngineExternalDevice.hpp
 * @brief A VST3, AU or LV2 plugin, as the native engine's executor sees it (#2241).
 *
 * Processed at max(max(1, inputs), outputs) channels: mono feeding stereo
 * duplicates, stereo feeding mono averages at half gain, a mono output is
 * spread over a stereo block. Slot 0 is dry, slot 1 wet
 * (DeviceInfo::wrapperParameters), then the plugin's automatable parameters
 * in plugin order. Transport recording state and loop points stay unset
 * (#1894, #1895).
 */

namespace magda::daw::audio::engine_adapter {

/**
 * @brief One external plugin instance bound to one Device op.
 *
 * Owns the instance, which arrives created with its buses enabled
 * (EngineDeviceFactory.hpp). Everything the audio thread touches is sized in
 * prepare().
 */
class EngineExternalDevice final : public magda::engine::EngineDevice {
  public:
    /**
     * @brief Binds @p instance, whose parameters @p device describes.
     *
     * The slot-to-parameter mapping comes from the instance, not the model.
     * @p offlineRender is passed to setNonRealtime() before prepareToPlay().
     */
    EngineExternalDevice(std::unique_ptr<juce::AudioPluginInstance> instance,
                         const magda::DeviceInfo& device, bool offlineRender);
    ~EngineExternalDevice() override;

    void prepare(const magda::engine::RenderContext& context) override;
    void reset() override;
    void setMidiInputBoundBytes(int bytes) override;
    void setMidiOutputBoundBytes(int bytes) override;
    int latencySamples() const override;

    /**
     * @brief One block through the plugin: parameters written, audio
     * processed, MIDI carried.
     *
     * Passes the block through untouched while a control-side state read or
     * write holds the plugin.
     */
    void process(magda::engine::DeviceBlock& block) override;

    /**
     * @brief What the plugin holds now: chunk, parameter values, VST3 records.
     *
     * Call on the control executor (#2270): the read suspends the plugin, and
     * process() honours that. Nullopt when the plugin throws describing itself.
     */
    std::optional<magda::ExternalPluginSnapshot> captureState();

    /**
     * @brief Write @p saved into the plugin: parameter array, then state chunk.
     *
     * Call on the control executor (#2573). A Failed return means the plugin
     * threw partway through and must not be read back.
     */
    magda::SavedStateOutcome applyState(const magda::DeviceInfo& saved);

    /**
     * @brief The plugin's own text for @p normalised at plan slot @p slot (#2600).
     *
     * Message thread, not the control executor: it is read inside a paint.
     * Empty for a slot with no live parameter and for the wrapper pair.
     */
    juce::String parameterText(int slot, float normalised) const;

    /**
     * @brief Every parameter the instance reports, at the values it holds now.
     *
     * Message thread, on the same terms as parameterText(): a query that
     * suspends nothing. What the model mirrors is a subset of this (#2629).
     * The wrapper pair carries its defaults, since the model owns those values
     * and this device's own copies are written on the audio thread. Names and
     * ids are read once, and again only after the plugin reports they changed.
     */
    magda::HostParameters describeParameters() const;

    // ===== The plugin's own window (#2580). Call on the control executor. =====

    /// False for a plugin with no editor of its own.
    bool showEditor();

    void hideEditor();

    bool isEditorOpen() const;

    /**
     * @brief What the plugin reports one of its parameters now holds.
     *
     * An observation, not a command
     * (docs/specs/hosted-plugin-parameter-control.md).
     */
    struct Observation {
        int slot = -1;
        float normalised = 0.0f;
        magda::ObservationSource source = magda::ObservationSource::Readback;
    };

  private:
    struct PluginEdits;

  public:
    /// A device's unread plugin reports, held weakly so a drain after the device
    /// has gone does nothing (#2632).
    class PluginEditSource {
      public:
        PluginEditSource() = default;

        /**
         * @brief Hand @p sink every slot reported since the last drain. Message thread.
         *
         * Coalesced per slot, a gesture ahead of any later report that overwrote
         * it. Nothing is filtered; only the reader knows what may reach a
         * document. False once the device is gone.
         */
        bool drain(const std::function<void(Observation)>& sink) const;

      private:
        friend class EngineExternalDevice;
        explicit PluginEditSource(std::weak_ptr<PluginEdits> edits) : edits_(std::move(edits)) {}

        std::weak_ptr<PluginEdits> edits_;
    };

    /**
     * @brief Call @p wake when the plugin reports, once per burst until the next drain.
     *
     * From whatever thread the plugin reports on, so @p wake must neither
     * allocate nor lock. Set once; it is called straight away for anything
     * reported before.
     */
    void listenForPluginEdits(std::function<void()> wake);

    PluginEditSource pluginEdits() const;

    /// What queueing an edit did: its sequence, and the sequence of an earlier
    /// edit to the slot it replaced before any block took it, or zero (#2651).
    struct QueuedEdit {
        std::uint32_t sequence = 0;
        std::uint32_t superseded = 0;
    };

    /**
     * @brief Accept @p normalised for slot @p slot, applied at the next block.
     *
     * Into a mailbox of one entry per slot, so a burst keeps only its latest
     * value. A device the host has not marked rendered is applied here, under
     * the callback lock. Control executor. Absent for the wrapper pair, a slot
     * with no live parameter, and a position outside [0, 1].
     */
    std::optional<QueuedEdit> queueParameterEdit(int slot, float normalised);

    /// Take back edit @p sequence if no block has taken it yet. Control executor.
    bool withdrawParameterEdit(int slot, std::uint32_t sequence);

    /// What an apply did with the edit it took.
    struct EditOutcome {
        int slot = -1;
        std::uint32_t sequence = 0;
        bool refused = false;
    };

    /// Outcomes are kept until read, up to this many.
    static constexpr std::size_t kEditOutcomeCapacity = 2048;

    /**
     * @brief Hand @p each every outcome recorded since the last call. Control executor.
     *
     * @return false if an outcome was dropped for want of room, so the reader
     *         cannot tell every edit it waits on from one never taken.
     */
    bool takeEditOutcomes(const std::function<void(EditOutcome)>& each);

    /**
     * @brief Apply what the mailbox holds from the control side, under the callback lock.
     *
     * For a device no block reaches. A block arriving meanwhile passes through,
     * as it does during a capture. Control executor.
     */
    void pumpParameterEdits();

    /// Take every queued edit without applying it, recorded as refused, for a
    /// state about to replace the patch. Control executor.
    void discardParameterEdits();

    /// Whether a live plan renders this device and the audio device is running.
    /// Set by the host; an unrendered device is pumped from the control side.
    void setRendered(bool rendered);
    bool isRendered() const;

    /// Whether every accepted edit has been applied and processed. Any thread.
    bool editsFenced() const;

    /**
     * @brief Make a capture carry every edit accepted before it.
     *
     * Applies what the mailbox holds and, if the plugin has not processed an
     * applied edit, runs it over one silent block nobody hears: some formats
     * hand a set value to their processor only on a process call. False when
     * the plugin cannot be run, so the capture fails rather than saves stale
     * state. Control executor.
     */
    bool fenceParameterEdits();

    /// What the live parameter at @p slot holds now. Absent for the wrapper
    /// pair and a slot with no live parameter.
    std::optional<float> readParameter(int slot) const;

    /**
     * @brief Forget what was driving this device, which a new plan decides again.
     *
     * A device the plan dropped renders no block, so nothing would otherwise
     * clear the driver state its last block left behind. Message thread.
     */
    void forgetDriverState();

  private:
    class PlayHead;
    class EditorWindow;
    class PluginListener;

    void writeParameters(const magda::engine::DeviceParams& params);

    /// Apply every slot the mailbox holds. Audio thread, or the control side
    /// holding the callback lock. True if any was applied or refused.
    bool applyParameterEdits();

    /// Whether @ref parameters_ has a row for @p slot.
    bool mapsSlot(int slot) const {
        return slot >= 0 && slot < static_cast<int>(parameters_.size());
    }

    /**
     * @brief Whether the table's value for @p slot moved, recording it if so.
     *
     * Comparing against the plugin's own value instead would revert every edit
     * made in its editor.
     */
    bool hostValueMoved(int slot, float normalised) {
        auto& last = lastTable_[static_cast<std::size_t>(slot)];
        if (last == normalised)
            return false;

        last = normalised;
        return true;
    }

    /// The plugin over one buffer in place, wet/dry mixed.
    void processPluginBlock(juce::AudioBuffer<float>& audio);

    /// For a plugin whose width differs from the chain's or has a sidechain.
    void processThroughScratch(magda::engine::DeviceBlock& block, int numSamples, int destChannels);

    void readMidiIn(const juce::MidiBuffer& in, bool allNotesOff);
    void writeMidiOut(juce::MidiBuffer& out, int numSamples) const;

    /// The plugin's further output pairs, onto the ports the plan opened.
    void writeExtraOutputs(magda::engine::DeviceBlock& block,
                           const juce::AudioBuffer<float>& processed, int numSamples) const;

    std::unique_ptr<juce::AudioPluginInstance> instance_;
    std::unique_ptr<PlayHead> playHead_;

    /// Null while the window is not open; its close button resets it.
    std::unique_ptr<EditorWindow> editor_;

    /**
     * @brief What the plan's parameter slot at this index addresses.
     *
     * A slot with no live parameter behind it carries a null and is skipped.
     */
    struct ParameterMapping {
        juce::AudioProcessorParameter* parameter = nullptr;
        magda::WrapperRole role = magda::WrapperRole::None;

        /// The model's description of this parameter, if any.
        std::optional<magda::ParameterInfo> info;
    };

    std::vector<ParameterMapping> parameters_;

    /// What the table delivered per slot last block. A write happens only when
    /// this moves, so an edit made in the plugin's own editor is not reverted.
    std::vector<float> lastTable_;

    /// Plan slot per plugin parameter index, or -1.
    std::vector<int> slotOfParameter_;

    /// The plugin's own edits, recorded on any thread and drained on the
    /// message thread. Shared so a drain after the device died is a no-op.
    struct PluginEdits {
        explicit PluginEdits(std::size_t slots)
            : reported(slots),
              dirty(slots),
              gestured(slots),
              gestureDirty(slots),
              driven(slots),
              gesturing(slots) {}

        /// The value's bits and its source in one word, so a flush never pairs
        /// one report's value with another's source.
        static std::uint64_t pack(float normalised, magda::ObservationSource source);
        static Observation unpack(int slot, std::uint64_t packed);

        std::vector<std::atomic<std::uint64_t>> reported;
        std::vector<std::atomic<bool>> dirty;

        /// The latest editor-gesture value, flushed ahead of any later report
        /// that overwrote it in @ref reported.
        std::vector<std::atomic<float>> gestured;
        std::vector<std::atomic<bool>> gestureDirty;

        /// What the host is driving, as the last block's table said. Asserted
        /// by a block and cleared by a plan, so a device that renders none is
        /// driving nothing.
        std::vector<std::atomic<bool>> driven;

        /// Between the plugin's begin and end of a gesture in its own editor.
        std::vector<std::atomic<bool>> gesturing;

        /// Set before @ref listening and never again, so a reporting thread
        /// reads it only once it is whole.
        std::function<void()> wake;
        std::atomic<bool> listening{false};
        std::atomic<bool> wakeQueued{false};

        /// Once per burst until the next drain; nothing before listening. Any thread.
        void wakeHost() {
            if (listening.load(std::memory_order_acquire) &&
                !wakeQueued.exchange(true, std::memory_order_acq_rel))
                wake();
        }
    };

    /// One accepted edit per slot, its sequence and value's bits in one word, or
    /// zero. Both sides take it by exchange, so an edit is applied or replaced,
    /// never both.
    std::vector<std::atomic<std::uint64_t>> mailbox_;
    std::atomic<bool> anyEditQueued_{false};

    /// Record what an apply did. Only ever under the callback lock, which makes
    /// its writer single.
    void recordEditOutcome(std::size_t slot, std::uint32_t sequence, bool refused);

    /// Outcomes, written under the callback lock and read on the control executor.
    std::vector<std::uint64_t> outcomes_;
    std::atomic<std::size_t> outcomesWritten_{0};
    std::atomic<std::size_t> outcomesRead_{0};
    std::atomic<bool> outcomesLost_{false};

    /// Control executor only. Never zero once used, which marks an empty slot.
    std::uint32_t nextEditSequence_ = 0;

    std::atomic<bool> rendered_{false};

    /// An edit was applied and the plugin has not processed a block since.
    std::atomic<bool> awaitingBlock_{false};

    /// What describeParameters() last read off the instance, values aside.
    mutable std::optional<magda::HostParameters> catalog_;

    /// Set from any thread when the plugin reports its parameter info changed.
    mutable std::atomic<bool> catalogStale_{false};

    std::shared_ptr<PluginEdits> edits_;
    std::unique_ptr<PluginListener> listener_;

    /// The wet/dry pair: the host's own numbers, never written to the plugin.
    float dryGain_ = 0.0f;
    float wetGain_ = 1.0f;

    /// The width the plugin is processed at: max(max(1, inputs), outputs).
    int processChannels_ = 0;

    /// How many of those channels the plugin writes; the chain is filled from
    /// these, not from processChannels_.
    int outputChannels_ = 0;

    /**
     * @brief Where each further output pair starts in the plugin's own output.
     *
     * Entry k is the plan's extraOutputs[k], pair k + 1; firstChannel is
     * MultiOutOutputPair::firstPin made zero-based.
     */
    struct OutputPair {
        int firstChannel = 0;
        int numChannels = 0;
    };

    std::vector<OutputPair> extraOutputPairs_;

    /// Input channels on the plugin's main bus, and how many follow it, which
    /// is where a sidechain key lands.
    int mainInputChannels_ = 0;
    int sidechainInputChannels_ = 0;

    /// Non-owning view over the block for the in-place path.
    std::vector<float*> channels_;

    /// The plugin's buffer when widths differ, and the dry copy the mix reads.
    juce::AudioBuffer<float> scratch_;
    juce::AudioBuffer<float> dryScratch_;

    juce::MidiBuffer midi_;

    double sampleRate_ = 44100.0;

    /// What the instance was last prepared at; a repeat at the same settings
    /// is a no-op.
    int preparedBlockSize_ = 0;

    int latencySamples_ = 0;
    int midiInputBoundBytes_ = magda::engine::kMaxMidiBytesPerPort;

    /// What the executor reserved on the output port (#2341, #2345).
    int midiOutputBoundBytes_ = magda::engine::kMaxMidiBytesPerPort;
    bool offlineRender_ = false;
    bool prepared_ = false;
};

}  // namespace magda::daw::audio::engine_adapter
