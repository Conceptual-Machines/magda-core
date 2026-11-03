// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>

#include "transport/TransportClock.hpp"
#include "transport/TransportState.hpp"

/**
 * @file MidiClockOut.hpp
 * @brief MIDI clock leaving with the transport (#3190).
 */

namespace magda::engine {

/**
 * @brief 24 clocks a beat to each clock port while the timeline rolls, and Start, Continue, Song
 *        Position and Stop as it starts, jumps and stops.
 *
 * A port is placed on a sixteenth, the unit Song Position counts in: after a start or a jump it is
 * sent nothing until the next one, where its position and its first clock leave together. Outside
 * the plan, like the metronome. Audio thread.
 */
class MidiClockOut {
  public:
    static constexpr int kTicksPerBeat = 24;

    /// Send what @p segment owes @p outputs, once the plan has rendered it.
    void render(const ClockCore::Segment& segment, const ClockOutputs& outputs);

  private:
    struct Port {
        LiveMidiOutput* output = nullptr;
        /// Sent Start or Continue, and no Stop since.
        bool running = false;
        /// Told where the clocks it is sent count from.
        bool placed = false;
    };

    /// Take @p outputs as the ports, keeping the state of those already here. A port that left is
    /// the host's to stop.
    void adopt(const ClockOutputs& outputs);

    void tick(std::int64_t tick, int sample);
    void place(Port& port, std::int64_t tick, int sample);
    static void send(const Port& port, int sample, std::initializer_list<std::uint8_t> bytes);

    std::array<Port, ClockOutputs::kMaxPorts> ports_{};
    int portCount_ = 0;

    /// The next clock due, in 24ths of a beat on the timeline.
    std::int64_t nextTick_ = 0;
};

}  // namespace magda::engine
