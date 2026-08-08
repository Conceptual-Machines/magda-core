#pragma once

#include <memory>

#include "../audio/osc/OscRouter.hpp"
#include "../core/TypeIds.hpp"

namespace magda {

class ControllerParamWriter;
class MagdaApi;
struct ControlTarget;

/**
 * @brief Applies the OSC fixed namespace to the running DAW (#1757).
 *
 * The `_live` half of the OSC stack, in the same sense as the rest of this
 * directory: the routing above it is pure and testable, and this is where it
 * meets real tracks. Called on the message thread, once per address per drain.
 *
 * ## Two paths, deliberately
 *
 * Levels — track and master volume and pan, sends — go through
 * `ControllerParamWriter`, the same writer the MIDI control surfaces use. That
 * is not reuse for its own sake: the writer maps a normalized value through the
 * parameter's real range, so an OSC fader, a MIDI fader and an automation curve
 * at the same position produce the same gain, and its host-write path keeps
 * working when an LFO or macro is already driving the target.
 *
 * Everything else — transport, tempo, mute, solo, focused macros — goes through
 * `MagdaApi`, because these are states rather than parameter values and the
 * facade already routes them the way the on-screen controls do. Play in
 * particular dispatches through the same playhead-aware path as the Play
 * button, so a tablet and the transport bar cannot disagree about what Play
 * means.
 *
 * ## Addressing
 *
 * `/magda/track/{n}` is the nth strip visible in the mixer, and
 * `/magda/track/{n}/send/{m}` its mth send, both 1-based. Positions rather than
 * IDs, because IDs are sparse and a template's eight strips are not: deleting a
 * track renumbers what follows it, which is exactly what a surface showing
 * eight faders expects. A position with no track behind it is ignored — a
 * sixteen-strip template pointed at an eight-track project drives eight faders
 * rather than failing.
 */
class OscCommandSinkLive : public osc::OscCommandSink {
  public:
    /**
     * @param api     The facade. Must outlive this sink.
     * @param writer  Where level writes go. Owned, because the sink is
     *                constructed once alongside the OSC service and a borrowed
     *                writer would tie its lifetime to the MIDI router's.
     */
    OscCommandSinkLive(MagdaApi& api, std::unique_ptr<ControllerParamWriter> writer);
    ~OscCommandSinkLive() override;

    OscCommandSinkLive(const OscCommandSinkLive&) = delete;
    OscCommandSinkLive& operator=(const OscCommandSinkLive&) = delete;

    void apply(const osc::OscCommand& command, float value) override;

  private:
    /// The TrackId at a 1-based mixer position, or INVALID_TRACK_ID when the
    /// project has no strip there.
    TrackId trackAtPosition(int position) const;

    /// The aux bus a track's 1-based nth send uses, or -1 when it has none.
    /// Sends are addressed by position for the same reason tracks are; bus
    /// indices are what the model writes through.
    int sendBusForPosition(TrackId trackId, int position) const;

    /// `value` as a boolean state: a set 0/1, or `current` inverted when the
    /// surface asked for a flip by sending no argument at all.
    static bool resolveToggle(float value, bool current);

    void writeLevel(const ControlTarget& target, float value);

    MagdaApi& api_;
    std::unique_ptr<ControllerParamWriter> writer_;
};

}  // namespace magda
