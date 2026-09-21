#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "io/LiveOutput.hpp"

/**
 * @file HardwareMidiOutput.hpp
 * @brief MIDI a hardware insert sends, leaving when the audio beside it is heard (#2279).
 *
 * The audio thread queues each message with the time it is due, and a thread of
 * this service's own sends it then. Due is the callback's start plus the event's
 * offset plus the device's output latency, so a note reaches an external
 * instrument when the block it was played in reaches the speakers, and its round
 * trip is the same figure an audio send's is.
 */

namespace magda::daw::engine_host {

/** @brief When the block being rendered started, as each queued message is timed from. */
struct HardwareMidiClock {
    /// juce::Time::getMillisecondCounterHiRes() at the start of the process() call.
    double startMs = 0.0;
    double sampleRate = 0.0;
    int outputLatencySamples = 0;

    /// When @p sample from the start of the process() call leaves the machine.
    double dueMs(int sample) const {
        if (!(sampleRate > 0.0))
            return startMs;
        return startMs + 1000.0 * static_cast<double>(sample + outputLatencySamples) / sampleRate;
    }
};

/** @brief One open port. Messages of up to three bytes; a longer one is dropped and counted. */
class HardwareMidiPort final : public engine::LiveMidiOutput {
  public:
    /// @p output is null in a test, which queues and dispatches to @p sink instead.
    HardwareMidiPort(std::unique_ptr<juce::MidiOutput> output, const HardwareMidiClock& clock,
                     std::function<void(const juce::MidiMessage&)> sink = {});

    void send(int sample, const std::uint8_t* data, int size) override;
    void sendAfterQueued(const juce::MidiMessage& message) override;

    /// Send everything due by @p nowMs, queued messages before later releases. The
    /// dispatch thread's.
    void dispatchDue(double nowMs);

    /// Messages dropped: longer than three bytes, or past the queue's room.
    std::uint32_t dropped() const {
        return dropped_.load(std::memory_order_relaxed);
    }

  private:
    struct Queued {
        double dueMs = 0.0;
        std::uint8_t size = 0;
        std::array<std::uint8_t, 3> bytes{};
    };

    static constexpr int kCapacity = 4096;

    void deliver(const juce::MidiMessage& message);

    std::unique_ptr<juce::MidiOutput> output_;
    const HardwareMidiClock& clock_;
    std::function<void(const juce::MidiMessage&)> sink_;

    juce::AbstractFifo fifo_{kCapacity};
    std::array<Queued, kCapacity> queue_{};
    std::atomic<std::uint32_t> dropped_{0};

    /// The latest due time the audio thread has queued, which a release waits for.
    std::atomic<double> lastDueMs_{0.0};

    struct Release {
        double dueMs = 0.0;
        juce::MidiMessage message;
    };
    std::mutex releasesLock_;
    std::vector<Release> releases_;
};

/**
 * @brief Every port an insert has opened, and the thread that sends what they queue.
 *
 * Ports stay open until this goes, so a republish that rebuilds an insert does not
 * close and reopen the port under a held note. The thread starts with the first port.
 */
class HardwareMidiOutputs final : private juce::Thread {
  public:
    HardwareMidiOutputs();
    ~HardwareMidiOutputs() override;

    /// The port named @p name, opened if this has not yet, or null for one the system
    /// does not list. Off the audio thread.
    HardwareMidiPort* open(const juce::String& name);

    /// Audio thread, per process() call, before anything renders.
    void beginBlock(double startMs, double sampleRate, int outputLatencySamples) {
        clock_ = {startMs, sampleRate, outputLatencySamples};
    }

  private:
    void run() override;

    HardwareMidiClock clock_;
    std::mutex portsLock_;
    std::map<juce::String, std::unique_ptr<HardwareMidiPort>> ports_;
};

}  // namespace magda::daw::engine_host
