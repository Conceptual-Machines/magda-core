#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "core/ParameterInfo.hpp"
#include "core/ParameterUtils.hpp"
#include "exec/EngineDevice.hpp"
#include "plugins/DeviceTiming.hpp"
#include "plugins/MagdaDevice.hpp"

/**
 * @file EngineMagdaDevice.hpp
 * @brief A MAGDA device, as the native engine's executor sees it (#2174).
 *
 * A parameter is read once per block: ParamValues::value(), the value at the block's
 * first sample, written before process(). A device may ask for segment accuracy
 * (ParamSpec::segmentAccurate), which nothing does yet.
 *
 * One thing the device SDK does not carry yet:
 *
 * - **Further output pairs.** An sdk::Device declares no channel layout beyond
 *   what it writes into the buffer it is handed, so DeviceBlock::extraOutputs
 *   is left cleared. A device that owned pairs of its own would need the SDK
 *   to say so.
 *
 * The all-notes-off panic flag is carried beside the MIDI port, since a
 * juce::MidiBuffer has no room for it: DeviceBlock::midiInAllNotesOff on the
 * way in, DeviceBlock::midiOutAllNotesOff on the way out (#2418).
 *
 * Sidechain audio is neither: DeviceBlock::sidechain is the plan's own slot and
 * DeviceProcessContext::sidechain is the SDK's, so the key crosses here as a
 * port on both sides and no adapter decides where in a buffer it belongs
 * (#2192, #2329).
 */

namespace magda::daw::audio::engine_adapter {

/**
 * @brief One sdk::Device bound to one Device op.
 *
 * Owns the device and is the host it talks to (sdk::DeviceHost). Everything the
 * audio thread touches is sized in prepare(): the channel pointer array the
 * audio view is built from, the two MIDI scratches the SDK's input and output
 * views read and write, and the parameter map.
 */
class EngineMagdaDevice final : public magda::engine::EngineDevice, private sdk::DeviceHost {
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
    void setOfflineRender(bool offline) override;
    void setMidiInputBoundBytes(int bytes) override;
    void setMidiOutputBoundBytes(int bytes) override;
    bool forwardsMidiInput() const override;
    int latencySamples() const override;
    double tailSeconds() const override;
    void process(magda::engine::DeviceBlock& block) override;

    /// The device this stands for. For a host that has to reach past the
    /// adapter -- a custom UI, a telemetry surface -- never for rendering.
    MagdaDevice& device() const {
        return *device_;
    }

    /**
     * @brief Where the device's own state goes: called on the control thread with state the
     *        device produced and the host's document does not hold.
     */
    void setStateReporter(std::function<void(sdk::StateNode)> reporter) {
        stateReporter_ = std::move(reporter);
    }

    /// Called on the control thread when the device asks for its properties to be re-read.
    void setRebuildRequestHandler(std::function<void()> handler) {
        rebuildHandler_ = std::move(handler);
    }

    const char* profileName() const override {
        return properties_.pluginId.c_str();
    }

    const DeviceProperties& properties() const {
        return properties_;
    }

    /// Makes the next block write every slot again. Any thread; for a host that
    /// restored the device's state live, which resets its parameters without a prepare().
    void invalidateParameterWrites() {
        parametersStale_.store(true, std::memory_order_release);
    }

  private:
    void stateChanged(sdk::StateNode state) override;
    void rebuildRequired() override;

    void writeParameters(const magda::engine::DeviceParams& params);
    void sizeMidiScratch();

    std::unique_ptr<MagdaDevice> device_;
    std::function<void(sdk::StateNode)> stateReporter_;
    std::function<void()> rebuildHandler_;
    DeviceProperties properties_;
    /// What the device's own process() is timed under, beside the adapter's (DeviceTiming.hpp).
    std::string dspTimingName_;

    /// One entry per parameter the device declared, in its own slot order.
    /// `plan` is the index the plan addresses that slot by, which is what
    /// ParameterInfo::paramIndex says and what the parameter table was sized
    /// from; `info` is what converts the resolved value back to the normalised
    /// position the SDK takes.
    struct ParameterMapping {
        int plan = 0;
        magda::ParameterInfo info;

        /// The last position and domain the table gave, and the value they converted to. Most
        /// parameters hold still, and the round trip through their units is the dear part.
        float position = std::numeric_limits<float>::quiet_NaN();
        magda::ParameterUtils::ParameterDomain domain;
        float normalized = 0.0f;
        /// What the device was last handed; NaN until the first write after a prepare().
        float written = std::numeric_limits<float>::quiet_NaN();
    };

    std::vector<ParameterMapping> parameters_;
    std::atomic<bool> parametersStale_{false};
    std::vector<float*> channels_;
    /// The key's channel pointers, sized in prepare() to what the device
    /// declared. Read-only: a device reads its key and never writes it.
    std::vector<const float*> sidechainChannels_;

    /// What the device reads this block and what it wrote, one vector each,
    /// reserved to its own port's bound and never grown past it (#2347). A
    /// sysex written out is copied into @ref midiOutBytes_, reserved to the
    /// same bound.
    std::vector<sdk::MidiEvent> midiInScratch_;
    std::vector<sdk::MidiEvent> midiOutScratch_;
    std::vector<std::uint8_t> midiOutBytes_;

    /// Which of its pitch each input note-on is, for its fraction (#2741).
    magda::engine::NoteOccurrences occurrences_;

    int midiInCapacity_ = 0;
    int midiOutCapacity_ = 0;

    /// What the executor said can reach the input port, which is the sum
    /// through the MIDI graph rather than one producer's budget. The constant
    /// until it says otherwise, so a host that never tells us is no worse off
    /// than before it could.
    int midiInputBoundBytes_ = magda::engine::kMaxMidiBytesPerPort;

    /// What the executor reserved on the output port: one producer's worth,
    /// plus the input's when the device forwards part of it (#2417). A device
    /// emits what it made, and MIDI thru is the plan's merge behind it
    /// (#2345, #2347). The constant until it says otherwise, for the reason
    /// above.
    int midiOutputBoundBytes_ = magda::engine::kMaxMidiBytesPerPort;

    double sampleRate_ = 44100.0;
    int latencySamples_ = 0;
    bool offlineRender_ = false;
    bool prepared_ = false;
};

}  // namespace magda::daw::audio::engine_adapter
