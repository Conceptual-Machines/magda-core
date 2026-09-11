#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <memory>

#include "exec/EngineDevice.hpp"

/**
 * @file EngineTrace.hpp
 * @brief What reached an instrument, and when the model moved under it (#2568).
 *
 * A note that sounds twice is a race between a publish and the block that was
 * already rendering, and neither end of it is visible from a log line on the
 * message thread: by the time anything could be printed the block has gone and
 * the snapshot that produced it has been replaced.
 *
 * So both ends are recorded on the thread that sees them -- every note-on and
 * note-off as it reaches the device, every publish as it happens -- into one
 * stream, stamped with where the transport was. Reading them in order is what
 * says whether a note sounded before or after the edit that moved it.
 *
 * Off unless MAGDA_ENGINE_TRACE_MIDI is set, and nothing is allocated, locked
 * or logged on the audio thread when it is: a writer takes a slot and fills it.
 */

namespace magda::daw::engine_host {

class EngineTrace {
  public:
    /// Whether the environment asked for this. Read once.
    static bool enabled();

    /**
     * @brief One line of it, in front of whoever switched it on.
     *
     * Deliberately not juce::Logger: the app installs a FileLogger at startup,
     * so everything written through it lands in magda.log and nothing reaches
     * the terminal the trace was asked for from. A trace is a console tool.
     */
    static void print(const juce::String& line);

    enum class Kind : std::uint8_t { NoteOn, NoteOff, Publish, Swap };

    struct Entry {
        Kind kind = Kind::NoteOn;
        int note = 0;
        int velocity = 0;
        int sample = 0;
        double beat = 0.0;
        std::uint64_t block = 0;
    };

    /// From the audio thread, once per event. Drops rather than blocks when the
    /// reader has fallen behind, and says how many it dropped.
    void write(Entry entry);

    /// From the message thread. Everything written since the last call, in
    /// order, as lines for the log.
    juce::StringArray drain();

  private:
    /// A second of dense playing at a small block size, rounded up. The reader
    /// runs on a timer, so this only has to cover the gap between two of them.
    static constexpr std::size_t kCapacity = 4096;

    std::array<Entry, kCapacity> entries_{};
    std::atomic<std::uint64_t> written_{0};
    std::uint64_t read_ = 0;
    std::atomic<std::uint64_t> dropped_{0};
};

/**
 * @brief A device with its MIDI input recorded on the way in.
 *
 * Wrapping rather than reaching inside the device: what is worth knowing is
 * what the chain delivered, and a device that decided to ignore a note still
 * received it. Every other call forwards untouched.
 */
class TracingDevice final : public magda::engine::EngineDevice {
  public:
    TracingDevice(std::unique_ptr<EngineDevice> device, EngineTrace& trace)
        : device_(std::move(device)), trace_(trace) {}

    void prepare(const magda::engine::RenderContext& context) override {
        device_->prepare(context);
    }
    void reset() override {
        device_->reset();
    }
    void setMidiInputBoundBytes(int bytes) override {
        device_->setMidiInputBoundBytes(bytes);
    }
    void setMidiOutputBoundBytes(int bytes) override {
        device_->setMidiOutputBoundBytes(bytes);
    }
    bool forwardsMidiInput() const override {
        return device_->forwardsMidiInput();
    }
    int latencySamples() const override {
        return device_->latencySamples();
    }

    void process(magda::engine::DeviceBlock& block) override;

    /** @brief The device this wraps, for callers that must reach past the trace. */
    EngineDevice& wrapped() const {
        return *device_;
    }

  private:
    std::unique_ptr<EngineDevice> device_;
    EngineTrace& trace_;
    std::uint64_t blocks_ = 0;
};

}  // namespace magda::daw::engine_host
