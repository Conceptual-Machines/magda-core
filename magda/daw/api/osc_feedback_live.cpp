#include "osc_feedback_live.hpp"

#include <algorithm>
#include <cmath>

#include "../audio/osc/OscRouter.hpp"
#include "../core/AutomationInfo.hpp"
#include "../core/Config.hpp"
#include "../core/ControlTarget.hpp"
#include "../core/MixerStripOrder.hpp"
#include "../core/ParameterUtils.hpp"
#include "../core/TrackInfo.hpp"
#include "../core/aliases/AliasRegistry.hpp"
#include "../core/aliases/ChainContext.hpp"
#include "../core/aliases/ResolverRegistry.hpp"
#include "../core/aliases/TargetResolver.hpp"
#include "../core/controllers/BindingRegistry.hpp"
#include "../core/controllers/BindingTransform.hpp"
#include "focused_api.hpp"
#include "magda_api.hpp"
#include "project_api.hpp"
#include "track_api.hpp"
#include "transport_api.hpp"

namespace magda {

using osc::OscCommand;
using osc::OscCommandKind;
using osc::OscPeerId;

namespace {

/// A bound address is not a slot, so it carries its own change test. The same
/// tolerance the slot table uses, and for the same reason: a normalized value
/// that has been through a parameter's real range and back does not land on the
/// bit pattern it started from.
constexpr float kBoundEpsilon = 1.0e-4f;

OscCommand unindexed(OscCommandKind kind) {
    return OscCommand{kind, 0, 0};
}

}  // namespace

// ============================================================================
// Tick and ControllerSink
// ============================================================================

/// The 30 Hz heartbeat, owned rather than inherited so that a MAGDA with OSC off
/// — and a headless one, which has no message loop to drive a timer anyway —
/// never brings JUCE's timer thread into existence.
class OscFeedbackProjector::Tick : public juce::Timer {
  public:
    Tick(OscFeedbackProjector& owner, int intervalMs) : owner_(owner) {
        startTimer(intervalMs);
    }

    ~Tick() override {
        stopTimer();
    }

    void timerCallback() override {
        owner_.tick();
    }

  private:
    OscFeedbackProjector& owner_;
};

/**
 * The `ControllerFeedbackSink` half. It carries a value, and deliberately
 * ignores it: what a bound address should show is what its *target* holds now,
 * and a `FeedbackEvent` names a binding rather than a target, so two OSC
 * bindings on one parameter would each need looking up anyway. Marking the
 * bindings dirty and re-reading on the next tick answers both, at the tick rate
 * a surface can consume rather than at the rate a knob produces.
 */
class OscFeedbackProjector::ControllerSink : public ControllerFeedbackSink {
  public:
    ControllerSink(OscFeedbackProjector& owner, std::shared_ptr<std::atomic<bool>> alive)
        : owner_(owner), alive_(std::move(alive)) {}

    void send(const FeedbackEvent&) override {
        if (alive_->load())
            owner_.bindingsDirty_ = true;
    }

  private:
    OscFeedbackProjector& owner_;
    std::shared_ptr<std::atomic<bool>> alive_;
};

// ============================================================================
// Lifecycle
// ============================================================================

OscFeedbackProjector::OscFeedbackProjector(MagdaApi& api, remote::ChangeSource& changes,
                                           osc::OscRouter& router,
                                           std::unique_ptr<ControllerParamReader> reader,
                                           SinkFactory factory)
    : api_(api),
      changes_(changes),
      router_(router),
      reader_(std::move(reader)),
      factory_(std::move(factory)) {
    if (!factory_)
        factory_ = [](const juce::String& host, int port) -> std::unique_ptr<osc::OscMessageSink> {
            auto sender = std::make_unique<osc::OscSenderSink>();
            if (!sender->connect(host, port))
                return nullptr;
            return sender;
        };

    feedbackPort_ = Config::getInstance().getOscFeedbackPort();

    // The router's drain is on the message thread, same as the tick, so both of
    // these reach straight into the tables with nothing in between.
    router_.setFeedbackTap([this](OscPeerId peer, int slot, float value) {
        if (auto* surface = surfaceFor(peer)) {
            surface->feedback->noteReceived(slot, value);
            // The model listener that will normally dirty these projections is
            // delivered by another timer. Make this drain's next feedback tick
            // project the received value while its echo marker is still live.
            mixerDirty_ = true;
            macrosDirty_ = true;
        }
    });
    router_.setBindingFeedbackTap([this](OscPeerId peer, const juce::String& address, float value) {
        if (auto* surface = surfaceFor(peer)) {
            auto& entry = boundEntryFor(*surface, address);
            entry.lastReceived = value;
            entry.echoed = true;
            bindingsDirty_ = true;
        }
    });

    changeToken_ = changes_.addListener(
        [this](const std::vector<remote::ChangeSource::Change>& changed) { onChanges(changed); });

    Config::getInstance().addListener(this);
    BindingRegistry::getInstance().addListener(this);
}

OscFeedbackProjector::~OscFeedbackProjector() {
    alive_->store(false);
    BindingRegistry::getInstance().removeListener(this);
    Config::getInstance().removeListener(this);
    tick_.reset();
    changes_.removeListener(changeToken_);
    router_.setFeedbackTap({});
    router_.setBindingFeedbackTap({});
}

void OscFeedbackProjector::configChanged() {
    applyConfig();
}

void OscFeedbackProjector::bindingRegistryChanged(BindingScope /*scope*/) {
    // Both scopes feed one pass, so which one moved does not matter, and
    // dropping the remembered values is what keeps a re-pointed address from
    // being diffed against the target it used to name.
    for (auto& surface : surfaces_)
        surface.bound.clear();
    bindingsDirty_ = true;
}

std::unique_ptr<ControllerFeedbackSink> OscFeedbackProjector::makeControllerFeedbackSink() {
    return std::make_unique<ControllerSink>(*this, alive_);
}

// ============================================================================
// Configuration
// ============================================================================

bool OscFeedbackProjector::applyConfig() {
    auto& config = Config::getInstance();

    const int port = config.getOscFeedbackPort();
    if (port != feedbackPort_) {
        // Every surface is being answered somewhere else now. Rebuilt rather
        // than re-pointed, because a sender aimed somewhere new has told its new
        // destination nothing.
        feedbackPort_ = port;
        surfaces_.clear();
        surfacesStale_ = true;
    }

    // There is no feedback enable: a peer exists only because a socket is bound,
    // and a socket is bound only when OSC is on. What the toggle controls here
    // is whether the tick exists at all.
    //
    // The surfaces go with it rather than waiting for a tick to notice the peer
    // table emptying, because with the tick gone there is no next tick — and a
    // surface left behind is a UDP socket held open for a listener that has
    // been switched off.
    if (!config.getOscEnabled() || port <= 0 || port > 65535) {
        tick_.reset();
        surfaces_.clear();
        surfacesStale_ = true;
        return false;
    }

    if (tick_ == nullptr)
        tick_ = std::make_unique<Tick>(*this, kTickIntervalMs);
    return true;
}

std::uint64_t OscFeedbackProjector::sentTo(osc::OscPeerId peer) const {
    for (const auto& surface : surfaces_)
        if (surface.peer == peer)
            return surface.feedback->sentMessageCount();
    return 0;
}

void OscFeedbackProjector::requestSnapshot() {
    for (auto& surface : surfaces_) {
        surface.feedback->requestSnapshot();
        for (auto& entry : surface.bound)
            entry.hasSent = false;
    }
    mixerDirty_ = macrosDirty_ = bindingsDirty_ = true;
}

// ============================================================================
// Surfaces
// ============================================================================

void OscFeedbackProjector::syncSurfaces() {
    const auto generation = router_.peers().generation();
    if (generation == peersGeneration_ && !surfacesStale_)
        return;
    peersGeneration_ = generation;
    surfacesStale_ = false;

    if (feedbackPort_ <= 0 || feedbackPort_ > 65535) {
        surfaces_.clear();
        return;
    }

    const auto peers = router_.peers().snapshot();

    const auto stillThere = [&peers](const Surface& surface) {
        return std::any_of(peers.begin(), peers.end(), [&surface](const osc::OscPeers::Peer& peer) {
            return peer.answerable && peer.id == surface.peer && peer.host == surface.host;
        });
    };

    // The host is compared as well as the id. The id alone is enough, since ids
    // are never reused, so this is the assertion that they are not: a surface
    // whose id still matches under a different host would mean the whole echo
    // story had come apart.
    for (auto it = surfaces_.begin(); it != surfaces_.end();) {
        if (stillThere(*it)) {
            ++it;
            continue;
        }
        it = surfaces_.erase(it);
    }

    for (const auto& peer : peers) {
        // A peer that has only ever sent noise is not answered. See `OscPeers`:
        // opening a sender and streaming a project at whoever put four bytes on
        // the port would make this a UDP reflector.
        if (!peer.answerable)
            continue;

        auto known =
            std::find_if(surfaces_.begin(), surfaces_.end(), [&peer](const Surface& surface) {
                return surface.peer == peer.id && surface.host == peer.host;
            });
        if (known != surfaces_.end()) {
            if (peer.resumptions != known->resumptions) {
                known->feedback->requestSnapshot();
                for (auto& entry : known->bound)
                    entry.hasSent = false;
                mixerDirty_ = macrosDirty_ = bindingsDirty_ = true;
            }
            known->resumptions = peer.resumptions;
            continue;
        }

        auto sink = factory_(peer.host, feedbackPort_);
        if (sink == nullptr) {
            // The socket would not open. Staying stale is what makes the next
            // tick try again: the peer set has not changed, so the generation
            // fast path above would otherwise never look at this peer again and
            // one transient failure would disable feedback for that surface for
            // the rest of the session.
            surfacesStale_ = true;
            continue;
        }

        Surface surface;
        surface.peer = peer.id;
        surface.host = peer.host;
        surface.resumptions = peer.resumptions;
        surface.feedback = std::make_unique<osc::OscFeedback>(std::move(sink));
        // A surface that has just been heard from has been told nothing, and
        // this is the event #2091 could only guess at.
        surface.feedback->requestSnapshot();
        surfaces_.push_back(std::move(surface));

        // A snapshot can only send what has been published, and the published
        // half is only re-read when something moved. For a surface that just
        // arrived nothing has, so this tick has to read it all again.
        mixerDirty_ = macrosDirty_ = bindingsDirty_ = true;
    }
}

OscFeedbackProjector::Surface* OscFeedbackProjector::surfaceFor(osc::OscPeerId peer) {
    for (auto& surface : surfaces_)
        if (surface.peer == peer)
            return &surface;

    // The first drain of a first gesture lands before the tick that would have
    // built the surface. See the header: syncing here is what keeps that
    // gesture's echo bit.
    syncSurfaces();

    for (auto& surface : surfaces_)
        if (surface.peer == peer)
            return &surface;
    return nullptr;
}

void OscFeedbackProjector::publishAll(const OscCommand& command, float value) {
    const int slot = oscSlotIndex(command);
    for (auto& surface : surfaces_)
        surface.feedback->publish(slot, value);
}

void OscFeedbackProjector::retireAll(const OscCommand& command) {
    const int slot = oscSlotIndex(command);
    for (auto& surface : surfaces_)
        surface.feedback->retire(slot);
}

// ============================================================================
// The tick
// ============================================================================

void OscFeedbackProjector::onChanges(const std::vector<remote::ChangeSource::Change>& changed) {
    for (const auto& change : changed) {
        switch (change.topic) {
            case remote::Topic::Tracks:
                mixerDirty_ = true;
                // A bound target can be a track level, and a track that has gone
                // away takes its bindings' resolutions with it.
                bindingsDirty_ = true;
                break;
            case remote::Topic::Devices:
                macrosDirty_ = true;
                bindingsDirty_ = true;
                break;
            case remote::Topic::Selection:
                // Which device is focused is a selection, and the focused macros
                // are addressed by focus rather than by identity.
                macrosDirty_ = true;
                bindingsDirty_ = true;
                break;
            case remote::Topic::Project:
                // A project swap and a project save arrive on one topic, and
                // nothing distinguishes them. Marking everything dirty is right
                // for both: a swap changes the values and the diff sends them,
                // a save changes nothing and the diff sends nothing. Resetting
                // here instead would resend the whole namespace every time the
                // user pressed save.
                mixerDirty_ = macrosDirty_ = bindingsDirty_ = true;
                break;
            case remote::Topic::Automation:
                // A curve moving a plain plugin parameter reaches nothing else.
                // `AutomationPlaybackEngine::currentValueChanged` writes track
                // levels, macros and mod rates back through `TrackManager` — so
                // those arrive as Tracks or Devices — but a `PluginParam` has no
                // such branch, and only the lane is notified. Without this a
                // bound fader never follows an automated parameter. The tick's
                // own cadence is the coalescing, so a curve running at block
                // rate still costs one binding pass per tick.
                bindingsDirty_ = true;
                break;
            case remote::Topic::Transport:
                // Nothing to mark dirty, and not an oversight. The transport is
                // the sampled half: every tick reads it, because nothing marks
                // this topic when the user presses Play in MAGDA — only a remote
                // API request named `transport.*` does. Acting on it would be a
                // second path to the same five reads the tick has already done
                // by the time this arrives.
            case remote::Topic::Clips:
            case remote::Topic::Session:
            case remote::Topic::Meters:
            case remote::Topic::Playhead:
                break;
        }
    }
}

void OscFeedbackProjector::tick() {
    // One atomic read when nothing has changed, which is what makes it
    // affordable to ask on every tick of a MAGDA nobody is talking to.
    syncSurfaces();
    if (surfaces_.empty())
        return;

    if (mixerDirty_) {
        mixerDirty_ = false;
        projectMixer();
    }
    if (macrosDirty_) {
        macrosDirty_ = false;
        projectMacros();
    }
    if (bindingsDirty_) {
        bindingsDirty_ = false;
        projectBindings();
    }

    // Always. Nothing marks the transport, and the playhead moves without
    // telling anyone.
    projectTransport();

    for (auto& surface : surfaces_)
        surface.feedback->flush();
}

// ============================================================================
// Projections
// ============================================================================

void OscFeedbackProjector::projectMixer() {
    auto& tracks = api_.tracks();

    // The mixer's own rule for what a strip is and where it sits, which is the
    // rule the input path addresses by. Always ViewMode::Mix: a surface's fader
    // 3 must not become another track because someone switched view.
    const auto order = mixerStripOrder(tracks.getTracks(), ViewMode::Mix);

    const int count = std::min(static_cast<int>(order.size()), osc::kMaxTrackNumber);
    for (int position = 1; position <= count; ++position)
        projectStrip(position, order[static_cast<size_t>(position - 1)]);

    retirePositionsFrom(count + 1);
    highestPosition_ = count;

    // The master is not in the strip order — it has its own place in the mixer
    // and its own addresses for the same reason.
    if (const auto* master = tracks.getTrack(MASTER_TRACK_ID)) {
        const auto volumeInfo =
            getParameterInfoForTarget(ControlTarget::trackVolume(MASTER_TRACK_ID));
        const auto panInfo = getParameterInfoForTarget(ControlTarget::trackPan(MASTER_TRACK_ID));
        publishAll(unindexed(OscCommandKind::MasterVolume),
                   ParameterUtils::normalizedFromGain(master->volume, volumeInfo));
        publishAll(unindexed(OscCommandKind::MasterPan),
                   ParameterUtils::realToNormalized(master->pan, panInfo));
    }
}

void OscFeedbackProjector::projectStrip(int position, TrackId trackId) {
    const auto* track = api_.tracks().getTrack(trackId);
    if (track == nullptr) {
        // A position in the order with nothing behind it should not exist, but
        // if it does the surface is better told nothing than told zero.
        retireAll(OscCommand{OscCommandKind::TrackVolume, position, 0});
        retireAll(OscCommand{OscCommandKind::TrackPan, position, 0});
        retireAll(OscCommand{OscCommandKind::TrackMute, position, 0});
        retireAll(OscCommand{OscCommandKind::TrackSolo, position, 0});
        for (int send = 1; send <= osc::kMaxSendNumber; ++send)
            retireAll(OscCommand{OscCommandKind::TrackSend, position, send});
        return;
    }

    const auto volumeInfo = getParameterInfoForTarget(ControlTarget::trackVolume(trackId));
    const auto panInfo = getParameterInfoForTarget(ControlTarget::trackPan(trackId));

    publishAll(OscCommand{OscCommandKind::TrackVolume, position, 0},
               ParameterUtils::normalizedFromGain(track->volume, volumeInfo));
    publishAll(OscCommand{OscCommandKind::TrackPan, position, 0},
               ParameterUtils::realToNormalized(track->pan, panInfo));
    publishAll(OscCommand{OscCommandKind::TrackMute, position, 0}, track->muted ? 1.0f : 0.0f);
    publishAll(OscCommand{OscCommandKind::TrackSolo, position, 0}, track->soloed ? 1.0f : 0.0f);

    // Sends are addressed by position on the track, the way the input path
    // resolves them, so send 1 is the first one the track has whatever bus it
    // uses.
    const int sendCount = std::min(static_cast<int>(track->sends.size()), osc::kMaxSendNumber);
    for (int send = 1; send <= osc::kMaxSendNumber; ++send) {
        const OscCommand command{OscCommandKind::TrackSend, position, send};
        if (send > sendCount) {
            retireAll(command);
            continue;
        }
        const auto& info = track->sends[static_cast<size_t>(send - 1)];
        publishAll(command, ParameterUtils::normalizedFromGain(
                                info.level, getParameterInfoForTarget(
                                                ControlTarget::sendLevel(trackId, info.busIndex))));
    }
}

void OscFeedbackProjector::retirePositionsFrom(int firstUnused) {
    for (int position = firstUnused; position <= highestPosition_; ++position) {
        retireAll(OscCommand{OscCommandKind::TrackVolume, position, 0});
        retireAll(OscCommand{OscCommandKind::TrackPan, position, 0});
        retireAll(OscCommand{OscCommandKind::TrackMute, position, 0});
        retireAll(OscCommand{OscCommandKind::TrackSolo, position, 0});
        for (int send = 1; send <= osc::kMaxSendNumber; ++send)
            retireAll(OscCommand{OscCommandKind::TrackSend, position, send});
    }
}

void OscFeedbackProjector::projectMacros() {
    auto& focused = api_.focused();
    const bool hasFocus = focused.hasFocus();

    for (int macro = 1; macro <= osc::kMaxMacroNumber; ++macro) {
        const OscCommand command{OscCommandKind::FocusedMacro, macro, 0};
        if (hasFocus)
            publishAll(command, focused.getMacroValue(macro - 1));
        else
            retireAll(command);
    }
}

void OscFeedbackProjector::projectTransport() {
    auto& transport = api_.transport();
    const bool playing = transport.isPlaying();

    // Play and stop are two addresses over one state. A surface with a lit Play
    // button and a lit Stop button expects exactly one of them on, so stop is
    // the complement rather than a second thing to track.
    publishAll(unindexed(OscCommandKind::TransportPlay), playing ? 1.0f : 0.0f);
    publishAll(unindexed(OscCommandKind::TransportStop), playing ? 0.0f : 1.0f);
    publishAll(unindexed(OscCommandKind::TransportRecord), transport.isRecording() ? 1.0f : 0.0f);
    publishAll(unindexed(OscCommandKind::TransportLoop), transport.isLoopEnabled() ? 1.0f : 0.0f);
    publishAll(unindexed(OscCommandKind::TransportTempo),
               static_cast<float>(api_.project().getCurrentProjectInfo().tempo));
    publishAll(unindexed(OscCommandKind::TransportPosition),
               static_cast<float>(transport.getPositionBeats()));

    // The two seek addresses are deltas. There is no state behind them to send,
    // which is why nothing publishes into their slots.
}

// ============================================================================
// Bindings
// ============================================================================

void OscFeedbackProjector::projectBindings() {
    if (reader_ == nullptr)
        return;

    auto& registry = BindingRegistry::getInstance();
    auto bindings = registry.bindings(BindingScope::Global);
    auto projectScoped = registry.bindings(BindingScope::Project);
    bindings.insert(bindings.end(), projectScoped.begin(), projectScoped.end());

    DefaultChainContext ctx;
    TargetResolver resolver{AliasRegistry::getInstance(), ResolverRegistry::getInstance(), ctx};

    // Resolved once for every surface. The alias lookup and the parameter read
    // are what this pass costs; the diff below is an array compare.
    //
    // One address, one value. Several bindings may share an address — that is a
    // supported shape on the way in, where one fader drives three parameters —
    // but on the way out they would each want the address at their own target's
    // position, and sending all three every tick is a permanent stream of
    // contradictions. The first that resolves is the one the surface is shown,
    // which is the same one the surface's own value is measured against.
    boundValues_.clear();

    for (const auto& binding : bindings) {
        if (!binding.source.isOsc() || binding.source.oscAddress.isEmpty())
            continue;

        const bool seen = std::any_of(boundValues_.begin(), boundValues_.end(),
                                      [&binding](const BoundValue& candidate) {
                                          return candidate.address == binding.source.oscAddress;
                                      });
        if (seen)
            continue;

        const auto resolved = resolver.resolve(binding.target);
        if (!resolved.ok())
            continue;  // an alias naming nothing right now; it will resolve again

        const auto value = reader_->read(resolved);
        if (!value)
            continue;  // a target with no reading — see ControllerParamReader

        // Back out through the binding's own shape, in the reverse of the order
        // the input path applied it: range, then curve. What is left is where
        // the surface's control has to sit for a move to produce the value the
        // target already holds.
        const float curved = invertRange(binding.range, *value);
        boundValues_.push_back(
            BoundValue{binding.source.oscAddress, invertCurve(binding.range.curve, curved)});
    }

    for (auto& surface : surfaces_) {
        for (const auto& bound : boundValues_)
            publishBinding(surface, bound.address, bound.position);

        // The echo bits belong to the pass that has just read them, exactly as
        // the slot table's do. A drag that keeps sending keeps setting them; a
        // finger that lifts leaves the next pass free to send the value once,
        // which is what tells the surface what MAGDA rounded it to.
        for (auto& entry : surface.bound)
            entry.echoed = false;
    }
}

OscFeedbackProjector::BoundAddress& OscFeedbackProjector::boundEntryFor(
    Surface& surface, const juce::String& address) {
    for (auto& candidate : surface.bound)
        if (candidate.address == address)
            return candidate;

    surface.bound.push_back(BoundAddress{address, 0.0f, 0.0f, false, false});
    return surface.bound.back();
}

void OscFeedbackProjector::publishBinding(Surface& surface, const juce::String& address,
                                          float position) {
    auto& entry = boundEntryFor(surface, address);

    // The bound half of the echo rule, and the same rule: a value the surface
    // just sent is a value it already shows. Without this, a bound fader under a
    // finger would have its own position sent back to it on every tick of the
    // drag, which is the chase in its purest form. Per surface, so the fader
    // under a finger on one tablet still reaches the other.
    if (entry.echoed && std::abs(entry.lastReceived - position) <= kBoundEpsilon) {
        entry.lastSent = position;
        entry.hasSent = true;
        return;
    }

    if (entry.hasSent && std::abs(entry.lastSent - position) <= kBoundEpsilon)
        return;

    if (!surface.feedback->sink().send(address, position))
        return;

    entry.lastSent = position;
    entry.hasSent = true;
}

}  // namespace magda
