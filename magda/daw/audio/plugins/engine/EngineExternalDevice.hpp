#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

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
 * The one adapter with no MAGDA device behind it: it hosts a binary somebody
 * else shipped, whose only contract is juce::AudioPluginInstance, and owes
 * the corpus the same thing every device does -- given one project, render
 * what the fork renders. That makes this a transcription of what
 * te::ExternalPlugin does with a block, in the same order, rather than a
 * design: a difference here is a difference in every project hosting a
 * plugin, with no third opinion to appeal to. Each parity point is named
 * beside the fork's reason for it, so a future fork change shows up as a
 * diff here rather than an archaeology exercise:
 *
 * - **Channel adaptation.** Processed at `max(max(1, inputs), outputs)`
 *   channels; a mismatch with the block is bridged by the fork's rules: mono
 *   feeding stereo duplicates, stereo feeding mono averages at half gain, a
 *   mono output is spread back over a stereo block.
 * - **Wet/dry.** Every external plugin gets a slot-level dry/wet pair it
 *   never declared, defaulting fully wet, mixed in after processing and
 *   persisted (DeviceInfo::wrapperParameters) so both engines agree on what
 *   "40% wet" means.
 * - **Parameter identity.** The automatable list is dry, then wet, then the
 *   plugin's own parameters in plugin order -- the same positions MAGDA saved
 *   as ParameterInfo::paramIndex, so the live mapping must reproduce that
 *   list rather than invent one.
 * - **Chunk vs. parameter array.** A project saves a plugin both ways; where
 *   they disagree the chunk wins. Resolved before this class exists
 *   (ExternalPluginState.hpp) -- by render time the model is the answer and
 *   this only writes what the plan resolves from it.
 * - **Further output pairs.** A multi-out sampler's extra channels get a plan
 *   port each, copied out by the same mapping the engine wires its rack pins
 *   by, not a second guess at plugin output layout.
 * - **Latency.** Read once from the instance after prepareToPlay, reported to
 *   the executor, compensated like any device.
 * - **The playhead.** A tempo-synced plugin told nothing about the transport
 *   renders a different project; the block already carries the answer.
 *
 * Three known, named differences:
 *
 * - **All-notes-off flag.** The fork's MIDI container carries one and expands
 *   it into per-channel note-offs, sustain releases and an MPE reset before
 *   the plugin sees the block. juce::MidiBuffer has no such flag -- the same
 *   gap EngineMagdaDevice has, closed in one place for both.
 * - **Denormal sanitising.** The fork clamps plugin output on Intel when the
 *   CPU's denormal flag was raised. The native engine has none yet anywhere,
 *   so this isn't the place to add it -- an executor-wide question (#2240),
 *   and a no-op on Apple silicon regardless.
 * - **Transport state.** The fork's playhead reports recording state and loop
 *   points; a device under the plan gets a stretch of timeline, not a
 *   transport, so those fields stay unset until the recording/launcher
 *   subsystems can supply them (#1894, #1895).
 */

namespace magda::daw::audio::engine_adapter {

/**
 * @brief One external plugin instance bound to one Device op.
 *
 * Owns the instance. Everything the audio thread touches is sized in
 * prepare(): the channel pointer array, processing scratch, the dry copy the
 * wet/dry mix reads, and the MIDI buffer.
 *
 * The instance arrives created with buses already enabled; the project's
 * saved state is applied here since that order is bound up with the
 * parameter list this class builds. Instantiation itself is a message-thread
 * job elsewhere (EngineDeviceFactory.hpp).
 */
class EngineExternalDevice final : public magda::engine::EngineDevice {
  public:
    /**
     * @brief Binds @p instance, whose parameters @p device describes.
     *
     * @p device is read only for parameter metadata: the plan resolves a
     * slot's value in that parameter's own units, and its ParameterInfo
     * converts it to the normalised position the plugin takes. The
     * slot-to-live-parameter mapping comes from the instance, not the model
     * -- a stale model would otherwise write a project's values onto
     * whatever the plugin's parameters are called this version.
     *
     * @p offlineRender is the flag the fork sets on every external plugin
     * before a bounce and clears after; a plugin may legitimately behave
     * differently under it (e.g. oversampling only when bouncing).
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
     * write holds the plugin, instead of blocking the audio thread the way
     * the fork's processMutex does -- the one place this class doesn't
     * transcribe the fork verbatim. The two can only differ mid-save, and a
     * corpus render never triggers one.
     */
    void process(magda::engine::DeviceBlock& block) override;

    /**
     * @brief What the plugin holds now: chunk, parameter values, VST3 records.
     *
     * Lives here rather than behind a raw instance pointer (#2270) because
     * reading a plugin and rendering through it must not overlap, and this
     * object -- which holds the instance and honours the suspension in
     * process() -- is the only thing that can enforce that.
     *
     * Nullopt when the plugin throws describing itself.
     *
     * Called on the control executor, which serialises it against every
     * other control operation on this device (ControlExecutor.hpp) -- needed
     * because the read suspends and resumes the plugin, and two overlapping
     * readers would race that. process() itself is gated by the plugin's own
     * callback lock and suspended flag, tried rather than waited on, so a
     * capture in flight costs a block its passthrough, never the deadline.
     *
     * Read-only: what to do with a snapshot, and whether the device it came
     * from is still the one the model means, is the caller's job in the
     * control plane (DeviceControl.hpp).
     */
    std::optional<magda::ExternalPluginSnapshot> captureState();

  private:
    class PlayHead;

    void writeParameters(const magda::engine::DeviceParams& params);

    /// The plugin over one buffer, wet/dry mixed. @p audio is the buffer the
    /// plugin processes in place, at the width it asked for.
    void processPluginBlock(juce::AudioBuffer<float>& audio);

    /// The block through a buffer of the plugin's own width, for a plugin
    /// whose width differs from the chain's or has a sidechain bus to fill.
    void processThroughScratch(magda::engine::DeviceBlock& block, int numSamples, int destChannels);

    void readMidiIn(const juce::MidiBuffer& in);
    void writeMidiOut(juce::MidiBuffer& out, int numSamples) const;

    /// The plugin's further output pairs, onto the ports the plan opened for
    /// them. @p processed is the buffer the plugin just wrote, at its own width.
    void writeExtraOutputs(magda::engine::DeviceBlock& block,
                           const juce::AudioBuffer<float>& processed, int numSamples) const;

    std::unique_ptr<juce::AudioPluginInstance> instance_;
    std::unique_ptr<PlayHead> playHead_;

    /**
     * @brief What the plan's parameter slot at this index addresses.
     *
     * Indexed by the fork's automatable-parameter index: zero is dry, one is
     * wet, the rest are the plugin's own parameters in plugin order. A slot
     * with no live parameter behind it (a plugin that has since dropped one)
     * carries a null and is skipped, leaving the plugin at its own state.
     */
    struct ParameterMapping {
        juce::AudioProcessorParameter* parameter = nullptr;
        magda::WrapperRole role = magda::WrapperRole::None;

        /// The model's description of this parameter, if any -- used to
        /// convert a value out of the plan's units the same way it went in.
        std::optional<magda::ParameterInfo> info;
    };

    std::vector<ParameterMapping> parameters_;

    /// The wet/dry pair, held rather than written through: the host's own
    /// numbers, which the plugin has never heard of.
    float dryGain_ = 0.0f;
    float wetGain_ = 1.0f;

    /// The width the plugin is processed at: max(max(1, inputs), outputs).
    int processChannels_ = 0;

    /// How many of those channels the plugin writes -- not the same as
    /// processChannels_ whenever it has more inputs than outputs; this is
    /// the count the chain is filled from.
    int outputChannels_ = 0;

    /**
     * @brief Where each further output pair starts in the plugin's own output.
     *
     * Entry k is the plan's `extraOutputs[k]`, i.e. pair k + 1 (pair 0 is the
     * main output the chain carries on from). The start channel is what the
     * model recorded reading the plugin's output buses
     * (MultiOutOutputPair::firstPin, one-based) -- the same mapping the
     * engine wires its rack pins by. Empty for the (near-universal) case of
     * a non-multi-out plugin.
     */
    struct OutputPair {
        int firstChannel = 0;
        int numChannels = 0;
    };

    std::vector<OutputPair> extraOutputPairs_;

    /// Input channels on the plugin's main bus, and how many follow -- the
    /// latter is where a sidechain key lands, how every dynamics plugin takes one.
    int mainInputChannels_ = 0;
    int sidechainInputChannels_ = 0;

    /// Non-owning view over the block, used when the plugin's width and the
    /// block's already agree and nothing needs copying.
    std::vector<float*> channels_;

    /// The plugin's buffer when they don't agree, and the dry copy the mix
    /// reads. Both sized in prepare(), never resized on the audio thread.
    juce::AudioBuffer<float> scratch_;
    juce::AudioBuffer<float> dryScratch_;

    juce::MidiBuffer midi_;

    double sampleRate_ = 44100.0;

    /// What the instance was last prepared at, so a repeat prepare() at the
    /// same settings is a no-op.
    int preparedBlockSize_ = 0;

    int latencySamples_ = 0;
    int midiInputBoundBytes_ = magda::engine::kMaxMidiBytesPerPort;

    /// What the executor reserved on the output port (one producer's worth --
    /// every JUCE format replaces the host's MIDI-out buffer with what the
    /// plugin declared, #2345). Read from the executor rather than the
    /// constant because the figure is the port's (#2341).
    int midiOutputBoundBytes_ = magda::engine::kMaxMidiBytesPerPort;
    bool offlineRender_ = false;
    bool prepared_ = false;
};

}  // namespace magda::daw::audio::engine_adapter
