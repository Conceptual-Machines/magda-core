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
 * @brief A VST3, AU or LV2 plugin, as the native engine's executor sees it
 *        (#2241).
 *
 * The third adapter, and the one with no MAGDA device behind it. EngineMagdaDevice
 * hosts a device this repository wrote and can ask anything of; this hosts a
 * binary somebody else shipped, whose only contract is juce::AudioPluginInstance.
 * What it owes the corpus is the same thing: given one project, render what the
 * fork renders.
 *
 * That makes this file a transcription rather than a design. Everything it does
 * between receiving a block and handing it back is what te::ExternalPlugin does
 * with the same block, in the same order, because a difference here is a
 * difference in every project that hosts a plugin and there is no third opinion
 * to appeal to. Each piece is written out below beside the fork's own reason for
 * it, so that the day the fork changes one, the divergence is a diff rather than
 * an archaeology exercise:
 *
 * - **Channel adaptation.** A plugin is processed at
 *   `max(max(1, inputs), outputs)` channels whatever the chain around it is, and
 *   a mismatch between that and the block is bridged by the fork's own rules: a
 *   mono block feeding a stereo input duplicates, a stereo block feeding a mono
 *   input averages at half gain, and a mono output is spread back over a stereo
 *   block so the next device has two channels to read.
 * - **Wet/dry.** The fork gives every external plugin a slot-level dry/wet pair
 *   the plugin never declared, defaulting to fully wet, and mixes with it after
 *   processing. MAGDA persists both (DeviceInfo::wrapperParameters), so a
 *   project can carry a plugin that is 40% wet and the two engines have to agree
 *   about what that means.
 * - **Parameter identity.** The fork's automatable list is dry, then wet, then
 *   the plugin's own automatable parameters in plugin order. Those positions are
 *   what MAGDA saved as ParameterInfo::paramIndex and what the plan's value
 *   layer resolves against, so the mapping back onto the live instance has to
 *   reproduce that list rather than invent one.
 * - **Which of the two saved records wins** is settled before this class
 *   exists. A project saves a plugin twice, as a chunk and as an array of
 *   parameter values, and where they disagree the chunk is right; the
 *   restoration applies both in that order and hands the corrected values back
 *   for the model to record (ExternalPluginState.hpp). By the time a device is
 *   rendering, the model is the answer and this writes what the plan resolves
 *   from it.
 * - **Further output pairs.** A multi-out sampler's drum outs are the channels
 *   past its main pair, and the plan opens a port for each one the model
 *   recorded. They are copied out by the same mapping the current engine wires
 *   its rack pins by, rather than by a second guess at how a plugin lays its
 *   outputs out.
 * - **Latency.** Read once from the instance after prepareToPlay and reported to
 *   the executor, which compensates it the same way it compensates any device.
 * - **The playhead.** A plugin asks where the transport is, and a tempo-synced
 *   delay that is told nothing renders a different project. The block already
 *   carries the answer.
 *
 * Three differences are known and named here rather than found later:
 *
 * - **The all-notes-off flag.** The fork's MIDI container carries one beside the
 *   events, and turns it into per-channel note-offs, sustain-pedal releases and
 *   an MPE reset before the plugin sees the block. juce::MidiBuffer has no such
 *   flag, so the engine has nowhere to read it from; the same gap
 *   EngineMagdaDevice declares, and it closes in one place for both of them.
 * - **Denormal sanitising.** The fork clamps a plugin's output on Intel when the
 *   CPU's denormal flag was raised during the call. The native engine has no
 *   denormal handling at all yet, anywhere, so this adapter is not the place to
 *   add one: it is an executor-wide question (#2240) and it is a no-op on Apple
 *   silicon either way.
 * - **The transport's own state.** The fork's playhead reports whether the edit
 *   is recording and where its loop points are. A device under the plan is
 *   handed a stretch of timeline and not a transport, so those two fields are
 *   left unset until the recording and launcher subsystems give the block
 *   something to read them off (#1894, #1895).
 */

namespace magda::daw::audio::engine_adapter {

/**
 * @brief One external plugin instance bound to one Device op.
 *
 * Owns the instance. Everything the audio thread touches is sized in prepare():
 * the channel pointer array, the processing scratch, the dry copy the wet/dry
 * mix reads, and the MIDI buffer.
 *
 * The instance arrives created and with its buses enabled; what the project
 * saved for it is applied here, because the order of that is bound up with the
 * parameter list this class builds. Instantiation itself is a message-thread
 * job with a plugin format manager behind it, and it is not this class's (see
 * EngineDeviceFactory.hpp).
 */
class EngineExternalDevice final : public magda::engine::EngineDevice {
  public:
    /**
     * @brief Binds @p instance, whose parameters @p device describes.
     *
     * @p device is read for its parameter metadata and nothing else: the value
     * the plan resolves for a slot is in that parameter's own units, and its
     * ParameterInfo is what converts it back to the normalised position the
     * plugin takes. The mapping from a slot to a live parameter comes from the
     * instance rather than from the model, because that is where the fork's
     * comes from and a stale model would otherwise write a project's values
     * onto whatever the plugin's parameters are called this version.
     *
     * @p offlineRender says which kind of render this instance is being built
     * for. It is the flag the fork sets on every external plugin before a
     * bounce and clears afterwards, and a plugin is entitled to behave
     * differently under it: an analogue-modelled saturator may oversample where
     * it would not in a callback.
     */
    EngineExternalDevice(std::unique_ptr<juce::AudioPluginInstance> instance,
                         const magda::DeviceInfo& device, bool offlineRender);
    ~EngineExternalDevice() override;

    void prepare(const magda::engine::RenderContext& context) override;
    void reset() override;
    void setMidiInputBoundBytes(int bytes) override;
    void setMidiOutputBoundBytes(int bytes) override;
    int latencySamples() const override;

    /// One block through the plugin: its parameters written, its audio
    /// processed, its MIDI carried.
    ///
    /// Does none of that while a host holds the plugin for a state read or
    /// write, and passes the block through instead. That is the one thing in
    /// this class which is not the fork's arrangement transcribed: the fork
    /// waits on its own processMutex where this declines to block the audio
    /// thread. The two can only differ while a save is in flight, and a corpus
    /// render never takes one.
    void process(magda::engine::DeviceBlock& block) override;

    /**
     * @brief What the plugin holds now: chunk, parameter values, VST3 records.
     *
     * The control half of this device, and the reason it is here rather than
     * anywhere with a pointer to the instance (#2270). Reading a plugin and
     * rendering through it are two things that must not overlap, and the rule
     * saying so has to have one owner: this object holds the instance, honours
     * the suspension in process(), and is therefore the only thing that can
     * promise the two are serialised. A caller handed the instance instead
     * would be holding half of a rule and no way to keep it.
     *
     * Nullopt when the plugin threw describing itself, which is a plugin whose
     * records would not agree with each other (ExternalPluginState.hpp).
     *
     * Called on the control executor, which is where every control operation on
     * a device runs and what serialises them against each other
     * (ControlExecutor.hpp). It matters here more than it looks: the read
     * suspends the plugin and resumes it afterwards, so two overlapping readers
     * would have the first to finish resuming the plugin underneath the second
     * and letting a block back in mid-capture. This device does not check --
     * a lock here would be a second answer to a question the executor already
     * answers for the editor and the preset load as well.
     *
     * The audio thread is not party to any of it. process() is gated by the
     * plugin's own callback lock and its suspended flag, which it tries rather
     * than waits on, so a capture in flight costs a block its passthrough and
     * never the deadline.
     *
     * It reads and does not write. What to do with a snapshot -- and whether
     * the device it was read from is still the device the model means -- is the
     * caller's, over in the control plane (DeviceControl.hpp).
     */
    std::optional<magda::ExternalPluginSnapshot> captureState();

  private:
    class PlayHead;

    void writeParameters(const magda::engine::DeviceParams& params);

    /// The plugin over one buffer, wet/dry mixed. @p audio is the buffer the
    /// plugin processes in place, at the width it asked for.
    void processPluginBlock(juce::AudioBuffer<float>& audio);

    /// The block through a buffer of the plugin's own width, for a plugin whose
    /// width is not the chain's or which has a sidechain bus to fill.
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
     * Indexed by plan slot, which is the fork's automatable-parameter index:
     * zero is the dry level, one is the wet level, and the rest are the
     * plugin's automatable parameters in plugin order. A slot with no live
     * parameter behind it -- a project saved against a plugin that has since
     * dropped one -- carries a null and is skipped, which leaves the plugin
     * wherever its own state put it.
     */
    struct ParameterMapping {
        juce::AudioProcessorParameter* parameter = nullptr;
        magda::WrapperRole role = magda::WrapperRole::None;

        /// The model's description of this parameter, or none. What it is for
        /// is the conversion out of the plan's units, which is the same
        /// conversion the value went in through.
        std::optional<magda::ParameterInfo> info;
    };

    std::vector<ParameterMapping> parameters_;

    /// The wet/dry pair, held rather than written through: they are the host's
    /// own numbers and the plugin has never heard of them.
    float dryGain_ = 0.0f;
    float wetGain_ = 1.0f;

    /// The width the plugin is processed at, which is its own rather than the
    /// chain's: max(max(1, inputs), outputs).
    int processChannels_ = 0;

    /// How many of those channels the plugin writes. Not the same number
    /// whenever it has more inputs than outputs, and it is this one the chain
    /// is filled from: the channels between the two carry input.
    int outputChannels_ = 0;

    /**
     * @brief Where each further output pair starts in the plugin's own output.
     *
     * Entry k is the pair the plan's `extraOutputs[k]` stands for, which is
     * pair k + 1: pair 0 is the main output and is the buffer the chain carries
     * on from. The channel it starts at is what the model recorded when it read
     * the plugin's output buses (MultiOutOutputPair::firstPin, one-based), so
     * this is the same mapping the current engine wires its rack pins by rather
     * than a second guess at how a plugin lays its outputs out.
     *
     * Empty for every plugin that is not multi-out, which is almost all of
     * them.
     */
    struct OutputPair {
        int firstChannel = 0;
        int numChannels = 0;
    };

    std::vector<OutputPair> extraOutputPairs_;

    /// Input channels on the plugin's main bus, and how many it has after them.
    /// The second is where a sidechain key lands, which is how every dynamics
    /// plugin takes one.
    int mainInputChannels_ = 0;
    int sidechainInputChannels_ = 0;

    /// Non-owning view over the block, for the case where the plugin's width
    /// and the block's already agree and nothing needs copying.
    std::vector<float*> channels_;

    /// The plugin's buffer when they do not agree, and the dry copy the mix
    /// reads. Both sized in prepare() and never resized on the audio thread.
    juce::AudioBuffer<float> scratch_;
    juce::AudioBuffer<float> dryScratch_;

    juce::MidiBuffer midi_;

    double sampleRate_ = 44100.0;

    /// What the instance was last prepared at, so a second prepare at the same
    /// settings does not prepare it again. See prepare().
    int preparedBlockSize_ = 0;

    int latencySamples_ = 0;
    int midiInputBoundBytes_ = magda::engine::kMaxMidiBytesPerPort;

    /// What the executor reserved on the output port: this device's input
    /// carried through plus a producer's worth of its own. A plugin handed a
    /// merged input hands one back, and the flat constant would cut it (#2341).
    int midiOutputBoundBytes_ = magda::engine::kMaxMidiBytesPerPort;
    bool offlineRender_ = false;
    bool prepared_ = false;
};

}  // namespace magda::daw::audio::engine_adapter
