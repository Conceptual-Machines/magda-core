#include "HardwareMidiOutput.hpp"

#include <algorithm>
#include <limits>

#include "../../audio/midi/MidiDeviceMatch.hpp"

namespace magda::daw::engine_host {

namespace {

/// How long the dispatch thread sleeps between looks at the queues.
constexpr int kDispatchIntervalMs = 1;

}  // namespace

HardwareMidiPort::HardwareMidiPort(std::unique_ptr<juce::MidiOutput> output,
                                   const HardwareMidiClock& clock,
                                   std::function<void(const juce::MidiMessage&)> sink)
    : output_(std::move(output)), clock_(clock), sink_(std::move(sink)) {}

void HardwareMidiPort::send(int sample, const std::uint8_t* data, int size) {
    if (size < 1 || size > 3) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const auto scope = fifo_.write(1);
    if (scope.blockSize1 + scope.blockSize2 < 1) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    auto& queued = queue_[static_cast<std::size_t>(scope.blockSize1 > 0 ? scope.startIndex1
                                                                        : scope.startIndex2)];
    queued.dueMs = clock_.dueMs(sample);
    queued.size = static_cast<std::uint8_t>(size);
    std::copy_n(data, size, queued.bytes.begin());

    if (queued.dueMs > lastDueMs_.load(std::memory_order_relaxed))
        lastDueMs_.store(queued.dueMs, std::memory_order_relaxed);
}

void HardwareMidiPort::sendAfterQueued(const juce::MidiMessage& message) {
    const std::scoped_lock lock(releasesLock_);
    releases_.push_back({lastDueMs_.load(std::memory_order_relaxed), message});
}

void HardwareMidiPort::dispatchDue(double nowMs) {
    while (fifo_.getNumReady() > 0) {
        int start1 = 0;
        int size1 = 0;
        int start2 = 0;
        int size2 = 0;
        fifo_.prepareToRead(1, start1, size1, start2, size2);

        const auto& queued = queue_[static_cast<std::size_t>(size1 > 0 ? start1 : start2)];
        if (queued.dueMs > nowMs)
            break;

        deliver(juce::MidiMessage(queued.bytes.data(), queued.size));
        fifo_.finishedRead(1);
    }

    // After the queue: a release is due no earlier than anything queued before it, so a
    // head still waiting was queued after every release that is due now.
    const std::scoped_lock lock(releasesLock_);
    const auto due =
        std::stable_partition(releases_.begin(), releases_.end(),
                              [nowMs](const Release& release) { return release.dueMs <= nowMs; });
    for (auto release = releases_.begin(); release != due; ++release)
        deliver(release->message);
    releases_.erase(releases_.begin(), due);
}

void HardwareMidiPort::deliver(const juce::MidiMessage& message) {
    if (output_ != nullptr)
        output_->sendMessageNow(message);
    else if (sink_)
        sink_(message);
}

HardwareMidiOutputs::HardwareMidiOutputs() : juce::Thread("MAGDA insert MIDI") {}

HardwareMidiOutputs::~HardwareMidiOutputs() {
    stopThread(1000);

    // Whatever is still queued goes now, releases last, so no note outlives the app.
    for (auto& [name, port] : ports_)
        port->dispatchDue(std::numeric_limits<double>::infinity());
}

HardwareMidiPort* HardwareMidiOutputs::open(const juce::String& name) {
    const std::scoped_lock lock(portsLock_);
    if (const auto found = ports_.find(name); found != ports_.end())
        return found->second.get();

    const auto device = midi::resolve(juce::MidiOutput::getAvailableDevices(), name);
    if (!device.has_value())
        return nullptr;

    auto output = juce::MidiOutput::openDevice(device->identifier);
    if (output == nullptr)
        return nullptr;

    auto port = std::make_unique<HardwareMidiPort>(std::move(output), clock_);
    auto* opened = ports_.emplace(name, std::move(port)).first->second.get();

    // Only once a port exists, so a session with no MIDI insert runs no thread for it.
    if (!isThreadRunning())
        startRealtimeThread(juce::Thread::RealtimeOptions{}.withPriority(8));
    return opened;
}

void HardwareMidiOutputs::run() {
    while (!threadShouldExit()) {
        {
            const std::scoped_lock lock(portsLock_);
            const auto now = juce::Time::getMillisecondCounterHiRes();
            for (auto& [name, port] : ports_)
                port->dispatchDue(now);
        }
        wait(kDispatchIntervalMs);
    }
}

}  // namespace magda::daw::engine_host
