// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "transport/MidiClockOut.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <span>

#include "io/LiveOutput.hpp"

namespace magda::engine {
namespace {

constexpr std::uint8_t kClock = 0xf8;
constexpr std::uint8_t kStart = 0xfa;
constexpr std::uint8_t kContinue = 0xfb;
constexpr std::uint8_t kStop = 0xfc;
constexpr std::uint8_t kSongPosition = 0xf2;

constexpr int kTicksPerSixteenth = MidiClockOut::kTicksPerBeat / 4;

/// Song Position's 14 bits, in sixteenths.
constexpr std::int64_t kMaxSongPosition = 0x3fff;

/// In ticks: a clock this close after a block's start is still the block's own.
constexpr double kTickEpsilon = 1.0e-6;

}  // namespace

void MidiClockOut::render(const ClockCore::Segment& segment, const ClockOutputs& outputs) {
    adopt(outputs);
    const auto& block = segment.block;
    const auto ports = std::span(ports_.data(), static_cast<std::size_t>(portCount_));

    // Stopped, counting in or held for the phase: the timeline stands, and so do the ports.
    if (!block.playing) {
        for (auto& port : ports) {
            if (port.running)
                send(port, segment.startSample, {kStop});
            port.running = false;
            port.placed = false;
        }
        return;
    }

    const auto first =
        static_cast<std::int64_t>(std::ceil((block.beats.start * kTicksPerBeat) - kTickEpsilon));
    if (!block.continuous || nextTick_ < first) {
        for (auto& port : ports)
            port.placed = false;
        nextTick_ = first;
    }

    for (; static_cast<double>(nextTick_) / kTicksPerBeat < block.beats.end; ++nextTick_) {
        const auto beat =
            std::max(static_cast<double>(nextTick_) / kTicksPerBeat, block.beats.start);
        tick(nextTick_, segment.startSample + block.eventForBeat(beat).value);
    }
}

void MidiClockOut::adopt(const ClockOutputs& outputs) {
    const auto count = std::clamp(outputs.count, 0, ClockOutputs::kMaxPorts);
    const auto held = std::span(ports_.data(), static_cast<std::size_t>(portCount_));

    std::array<Port, ClockOutputs::kMaxPorts> adopted{};
    for (auto index = 0; index < count; ++index) {
        auto* output = outputs.ports[static_cast<std::size_t>(index)];
        const auto kept = std::ranges::find(held, output, &Port::output);
        adopted[static_cast<std::size_t>(index)] = kept != held.end() ? *kept : Port{output};
    }
    ports_ = adopted;
    portCount_ = count;
}

void MidiClockOut::tick(std::int64_t tick, int sample) {
    const auto onSixteenth = tick >= 0 && tick % kTicksPerSixteenth == 0;
    for (auto& port : std::span(ports_.data(), static_cast<std::size_t>(portCount_))) {
        if (!port.placed) {
            if (!onSixteenth)
                continue;
            place(port, tick, sample);
        }
        send(port, sample, {kClock});
    }
}

void MidiClockOut::place(Port& port, std::int64_t tick, int sample) {
    const auto sixteenth = tick / kTicksPerSixteenth;
    if (sixteenth == 0 && !port.running) {
        send(port, sample, {kStart});
    } else {
        // Past what Song Position can say, the port is only told to carry on.
        if (sixteenth <= kMaxSongPosition)
            send(port, sample,
                 {kSongPosition, static_cast<std::uint8_t>(sixteenth & 0x7f),
                  static_cast<std::uint8_t>((sixteenth >> 7) & 0x7f)});
        if (!port.running)
            send(port, sample, {kContinue});
    }
    port.running = true;
    port.placed = true;
}

void MidiClockOut::send(const Port& port, int sample, std::initializer_list<std::uint8_t> bytes) {
    if (port.output != nullptr)
        port.output->send(sample, std::data(bytes), static_cast<int>(bytes.size()));
}

}  // namespace magda::engine
