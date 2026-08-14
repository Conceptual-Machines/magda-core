#include "osc_command_sink_live.hpp"

#include <cmath>

#include "../audio/controllers/ControllerParamWriter.hpp"
#include "../core/ControlTarget.hpp"
#include "../core/MixerStripOrder.hpp"
#include "../core/TrackInfo.hpp"
#include "../core/aliases/AliasRegistry.hpp"
#include "../core/aliases/ChainContext.hpp"
#include "../core/aliases/ResolverRegistry.hpp"
#include "../core/aliases/TargetResolver.hpp"
#include "focused_api.hpp"
#include "magda_api.hpp"
#include "project_api.hpp"
#include "track_api.hpp"
#include "transport_api.hpp"

namespace magda {

using osc::OscCommand;
using osc::OscCommandKind;

OscCommandSinkLive::OscCommandSinkLive(MagdaApi& api, std::unique_ptr<ControllerParamWriter> writer)
    : api_(api), writer_(std::move(writer)) {
    jassert(writer_ != nullptr);
}

OscCommandSinkLive::~OscCommandSinkLive() = default;

// ============================================================================
// Addressing
// ============================================================================

TrackId OscCommandSinkLive::trackAtPosition(int position) const {
    // The mixer's own rule for what a strip is, not a second approximation of
    // it: a hidden track, an aux return, or the child of a collapsed group all
    // take no position, and getting any of them wrong lands a fader on the
    // strip beside the one the user is looking at.
    //
    // Always ViewMode::Mix, even when the user is looking at another view. A
    // surface's fader 3 must not become a different track because someone
    // switched to the arrangement — the numbering a template was built against
    // is the mixer's.
    return mixerStripAtPosition(api_.tracks().getTracks(), ViewMode::Mix, position);
}

int OscCommandSinkLive::sendBusForPosition(TrackId trackId, int position) const {
    const auto* track = api_.tracks().getTrack(trackId);
    if (track == nullptr || position < 1 || position > static_cast<int>(track->sends.size()))
        return -1;
    return track->sends[static_cast<size_t>(position - 1)].busIndex;
}

bool OscCommandSinkLive::resolveToggle(float value, bool current) {
    return value == osc::kOscToggleRequest ? !current : value != 0.0f;
}

void OscCommandSinkLive::writeLevel(const ControlTarget& target, float value) {
    // The writer takes a resolved target; these are already concrete, so there
    // is nothing for the alias/resolver machinery to do first.
    ResolveResult resolved;
    resolved.target = target;
    resolved.resolved = true;
    writer_->write(resolved, value);
}

// ============================================================================
// Applying
// ============================================================================

void OscCommandSinkLive::apply(const OscCommand& command, float value) {
    switch (command.kind) {
        case OscCommandKind::TransportPlay:
            api_.transport().play();
            return;
        case OscCommandKind::TransportStop:
            api_.transport().stop();
            return;
        case OscCommandKind::TransportRecord:
            api_.transport().setRecording(resolveToggle(value, api_.transport().isRecording()));
            return;
        case OscCommandKind::TransportLoop:
            api_.transport().setLoopEnabled(resolveToggle(value, api_.transport().isLoopEnabled()));
            return;
        case OscCommandKind::TransportTempo:
            // The facade clamps against the project's real tempo range; a
            // surface sending 0 or 5000 is not this layer's to reinterpret.
            api_.project().setTempo(value);
            return;
        case OscCommandKind::TransportPosition:
            api_.transport().setPositionBeats(value);
            return;
        case OscCommandKind::TransportSeekBeats:
            api_.transport().seekBeats(value);
            return;
        case OscCommandKind::TransportSeekBars:
            // Rounded rather than truncated, so a surface that sends its
            // integers as floats and lands on 0.999999 still moves a bar.
            api_.transport().seekBars(static_cast<int>(std::lround(value)));
            return;

        case OscCommandKind::MasterVolume:
            writeLevel(ControlTarget::trackVolume(MASTER_TRACK_ID), value);
            return;
        case OscCommandKind::MasterPan:
            writeLevel(ControlTarget::trackPan(MASTER_TRACK_ID), value);
            return;

        case OscCommandKind::FocusedMacro:
            // OSC numbers macros from 1, the macro array from 0. With nothing
            // focused this is a no-op inside the facade, which is the right
            // answer for a knob the user has not pointed at anything yet.
            api_.focused().setMacroValue(command.index - 1, value);
            return;

        case OscCommandKind::TrackVolume:
        case OscCommandKind::TrackPan:
        case OscCommandKind::TrackMute:
        case OscCommandKind::TrackSolo:
        case OscCommandKind::TrackSend:
            break;  // handled below, once the position has a track behind it
    }

    const TrackId trackId = trackAtPosition(command.index);
    if (trackId == INVALID_TRACK_ID)
        return;  // a template with more strips than the project has tracks

    switch (command.kind) {
        case OscCommandKind::TrackVolume:
            writeLevel(ControlTarget::trackVolume(trackId), value);
            return;
        case OscCommandKind::TrackPan:
            writeLevel(ControlTarget::trackPan(trackId), value);
            return;
        case OscCommandKind::TrackMute:
            if (const auto* track = api_.tracks().getTrack(trackId))
                api_.tracks().setTrackMuted(trackId, resolveToggle(value, track->muted));
            return;
        case OscCommandKind::TrackSolo:
            if (const auto* track = api_.tracks().getTrack(trackId))
                api_.tracks().setTrackSoloed(trackId, resolveToggle(value, track->soloed));
            return;
        case OscCommandKind::TrackSend: {
            const int bus = sendBusForPosition(trackId, command.subIndex);
            if (bus >= 0)
                writeLevel(ControlTarget::sendLevel(trackId, bus), value);
            return;
        }
        default:
            jassertfalse;  // the first switch returned for every other kind
            return;
    }
}

// ============================================================================
// OscBindingSinkLive
// ============================================================================

OscBindingSinkLive::OscBindingSinkLive(std::unique_ptr<ControllerParamWriter> writer)
    : writer_(std::move(writer)) {
    jassert(writer_ != nullptr);
}

OscBindingSinkLive::~OscBindingSinkLive() = default;

void OscBindingSinkLive::apply(const Binding& binding, float value) {
    if (writer_ == nullptr)
        return;

    auto& state = toggleState_[binding.id.toDashedString()];

    // The value arrives already normalized, so it takes the float entry point
    // rather than being quantised through the 7-bit one.
    const auto out = applyModeNormalized(binding.mode, value, state);
    const float curved = applyCurve(binding.range.curve, out.value);
    const float finalValue = applyRange(binding.range, curved);

    DefaultChainContext ctx;
    TargetResolver resolver{AliasRegistry::getInstance(), ResolverRegistry::getInstance(), ctx};
    const auto resolved = resolver.resolve(binding.target);
    if (!resolved.ok()) {
        // An alias or resolver that names nothing right now — a device that has
        // been removed, a focused-device target with nothing focused. Not an
        // error: the binding is still valid and will resolve again.
        return;
    }

    writer_->write(resolved, finalValue);
}

}  // namespace magda
