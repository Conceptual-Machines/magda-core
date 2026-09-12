#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "core/DeviceInfo.hpp"
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

    // ===== The plugin's own window (#2580). Call on the control executor. =====

    /// False for a plugin with no editor of its own.
    bool showEditor();

    void hideEditor();

    bool isEditorOpen() const;

    /**
     * @brief Report a parameter the plugin moved itself, on the message thread.
     *
     * @p sink gets the plan slot and the normalised value, coalesced per slot
     * to one call per flush. The host's own writes are not reported.
     */
    void listenForPluginEdits(std::function<void(int slot, float normalised)> sink);

  private:
    class PlayHead;
    class EditorWindow;
    class PluginListener;

    void writeParameters(const magda::engine::DeviceParams& params);

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

    /// Set around the host's own setValue, so the listener can tell its echo
    /// from an edit the plugin made.
    std::atomic<bool> writing_{false};

    /// Plan slot per plugin parameter index, or -1.
    std::vector<int> slotOfParameter_;

    /// The plugin's own edits, recorded on any thread and flushed on the
    /// message thread. Shared so a flush queued before the device died is a
    /// no-op rather than a use-after-free.
    struct PluginEdits {
        explicit PluginEdits(std::size_t slots) : pending(slots), dirty(slots) {}

        std::vector<std::atomic<float>> pending;
        std::vector<std::atomic<bool>> dirty;
        std::atomic<bool> flushQueued{false};
        std::function<void(int, float)> sink;
    };

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
