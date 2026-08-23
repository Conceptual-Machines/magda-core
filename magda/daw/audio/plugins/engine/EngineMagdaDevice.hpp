#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>
#include <vector>

#include "core/ParameterInfo.hpp"
#include "exec/EngineDevice.hpp"
#include "plugins/MagdaDevice.hpp"

/**
 * @file EngineMagdaDevice.hpp
 * @brief A MAGDA device, as the native engine's executor sees it (#2174).
 *
 * The twin of TracktionMagdaDevicePlugin, and deliberately its twin: one device
 * implementation, two hosts, and the corpus comparing what the two hosts do
 * with it. A device written twice would only ever prove that a device can be
 * written twice, which is the thing the null-diff corpus has refused since
 * #2040 and the reason the gain in tests/NullDiffGain.hpp was as far as it
 * could go.
 *
 * What the two adapters owe each other is the reading of a parameter, and they
 * pay it the same way. The fork settles an AutomatableParameter at a block
 * boundary and holds it for the block; this reads ParamValues::value(), which
 * is the value at the block's first sample, and writes it once before
 * process(). Neither is a choice about precision -- a device is free to ask for
 * segment accuracy (ParamSpec::segmentAccurate) and nothing does during the
 * port, because a device resolved per sample against a curve the fork reads
 * once would differ from it by however much the curve moves across a block, on
 * every automated parameter, in every project.
 *
 * Three things the device SDK does not carry yet, named here rather than
 * silently dropped, because each is a divergence the day a device wants it:
 *
 * - **Sidechain audio.** DeviceProcessContext has no sidechain buffer, so
 *   DeviceBlock::sidechain stops here. No MagdaDevice reads one today (the
 *   sidechain fleet is still Tracktion-native), and the SDK has to grow the
 *   input before one can.
 * - **Further output pairs.** A MagdaDevice declares no channel layout beyond
 *   what it writes into the buffer it is handed, so DeviceBlock::extraOutputs
 *   is left cleared. Multi-out is a te::RackType wrapper in the incumbent and
 *   an op with extra ports in the plan; a device that owned pairs of its own
 *   would need the SDK to say so.
 * - **MidiMessageArray::isAllNotesOff.** The fork's MIDI container carries that
 *   flag beside the events and the engine's juce::MidiBuffer does not. A device
 *   that sets it is writing to something nothing downstream reads here.
 *
 * The tempo map goes the other way. The fork's adapter passes null because
 * TempoSequence queries are not guaranteed real-time safe; the engine's map is
 * an immutable snapshot the transport already holds for the length of the
 * callback, so this passes it. Nothing reads it yet in either host, which is
 * why that is a difference in what is offered rather than a divergence in what
 * is rendered.
 */

namespace magda::daw::audio::engine_adapter {

/**
 * @brief One MagdaDevice bound to one Device op.
 *
 * Owns the device. Everything the audio thread touches is sized in prepare():
 * the channel pointer array the audio view is built from, the MIDI scratch the
 * SDK's mutable buffer needs, and the parameter map.
 */
class EngineMagdaDevice final : public magda::engine::EngineDevice {
  public:
    /**
     * @brief Binds @p device, for a render that is @p offlineRender or is not.
     *
     * The flag is what DeviceProcessContext::isRendering carries, and it is a
     * property of the render rather than of the block: a bounce is prepared
     * once and is a bounce throughout. It has to be told, because the engine
     * pulls a bounce through the same executor as a callback (exec/
     * OfflineRender.hpp) and there is nothing in a block to read it off.
     *
     * What turns on it is a device declining to do live-only work -- an
     * analysis tap that would otherwise make the scope twitch to a render's
     * audio, an insert capture that would read a file on the audio thread.
     * False is the default because live is the case a device may not get
     * wrong: a bounce that fed the scope is a cosmetic bug, and a callback that
     * read a file is a dropout.
     */
    EngineMagdaDevice(std::unique_ptr<MagdaDevice> device, bool offlineRender);
    ~EngineMagdaDevice() override;

    void prepare(const magda::engine::RenderContext& context) override;
    void reset() override;
    int latencySamples() const override;
    void process(magda::engine::DeviceBlock& block) override;

    /// The device this stands for. For a host that has to reach past the
    /// adapter -- a custom UI, a telemetry surface -- never for rendering.
    MagdaDevice& device() const {
        return *device_;
    }

    const DeviceProperties& properties() const {
        return properties_;
    }

  private:
    void writeParameters(const magda::engine::DeviceParams& params);

    std::unique_ptr<MagdaDevice> device_;
    DeviceProperties properties_;

    /// One entry per parameter the device declared, in its own slot order.
    /// `plan` is the index the plan addresses that slot by, which is what
    /// ParameterInfo::paramIndex says and what the parameter table was sized
    /// from; `info` is what converts the resolved value back to the normalised
    /// position the SDK takes.
    struct ParameterMapping {
        int plan = 0;
        magda::ParameterInfo info;
    };

    std::vector<ParameterMapping> parameters_;
    std::vector<float*> channels_;
    std::vector<DeviceMidiEvent> midiScratch_;

    double sampleRate_ = 44100.0;
    int latencySamples_ = 0;
    bool offlineRender_ = false;
    bool prepared_ = false;
};

}  // namespace magda::daw::audio::engine_adapter
