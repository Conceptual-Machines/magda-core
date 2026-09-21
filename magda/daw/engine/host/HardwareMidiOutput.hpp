#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
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

/// What a port delivers a message through: the hardware endpoint it has open.
using MidiSink = std::function<void(const juce::MidiMessage&)>;

/** @brief One open port. Messages of up to three bytes; a longer one is dropped and counted. */
class HardwareMidiPort final : public engine::LiveMidiOutput {
  public:
    HardwareMidiPort(const HardwareMidiClock& clock, MidiSink sink);

    void send(int sample, const std::uint8_t* data, int size) override;
    void sendAfterQueued(const juce::MidiMessage& message) override;

    /// Send everything due by @p nowMs, queued messages before later releases. The
    /// dispatch thread's.
    void dispatchDue(double nowMs);

    /// Messages dropped: longer than three bytes, or past the queue's room.
    std::uint32_t dropped() const {
        return dropped_.load(std::memory_order_relaxed);
    }

    /// Deliver through @p sink from now on, keeping what is queued. Only where nothing
    /// dispatches at the same time.
    void reopen(MidiSink sink) {
        sink_ = std::move(sink);
    }

  private:
    struct Queued {
        double dueMs = 0.0;
        std::uint8_t size = 0;
        std::array<std::uint8_t, 3> bytes{};
    };

    static constexpr int kCapacity = 4096;

    void deliver(const juce::MidiMessage& message);

    const HardwareMidiClock& clock_;
    MidiSink sink_;

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

/** @brief Which device a port name means now, and how to open it. */
struct MidiEndpoints {
    /// The identifier of the device @p name names, or nothing when it is not listed.
    std::function<std::optional<juce::String>(const juce::String& name)> find;

    /// A sink onto the device @p identifier, or an empty one when it cannot be opened.
    std::function<MidiSink(const juce::String& identifier)> open;
};

/** @brief The system's MIDI outputs, through JUCE. */
MidiEndpoints systemMidiEndpoints();

/**
 * @brief Every port an insert has opened, and the thread that sends what they queue.
 *
 * A port object lives until this goes, so an insert never holds one that has been
 * destroyed; after a device change its endpoint is opened again at its next use. The
 * thread starts with the first port.
 */
class HardwareMidiOutputs final : private juce::Thread {
  public:
    explicit HardwareMidiOutputs(MidiEndpoints endpoints = systemMidiEndpoints());
    ~HardwareMidiOutputs() override;

    /// The port named @p name, opened or reopened as needed, or null for one the system
    /// does not list now. Off the audio thread.
    HardwareMidiPort* open(const juce::String& name);

    /// MIDI devices came or went: a port's endpoint may be one that has gone, so each is
    /// opened again the next time it is asked for. Message thread.
    void devicesChanged();

    /// Send everything due by @p nowMs on every port.
    void dispatchDue(double nowMs);

    /// Audio thread, per process() call, before anything renders.
    void beginBlock(double startMs, double sampleRate, int outputLatencySamples) {
        clock_ = {startMs, sampleRate, outputLatencySamples};
    }

  private:
    struct Port {
        std::unique_ptr<HardwareMidiPort> port;
        std::uint64_t generation = 0;
    };

    void run() override;

    MidiEndpoints endpoints_;
    HardwareMidiClock clock_;
    std::mutex portsLock_;
    std::map<juce::String, Port> ports_;
    std::uint64_t generation_ = 0;
};

}  // namespace magda::daw::engine_host
