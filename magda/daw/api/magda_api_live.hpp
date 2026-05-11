#pragma once

#include "alias_api_live.hpp"
#include "automation_api_live.hpp"
#include "clip_api_live.hpp"
#include "focused_api_live.hpp"
#include "magda_api.hpp"
#include "midi_api_live.hpp"
#include "project_api_live.hpp"
#include "selection_api_live.hpp"
#include "session_api_live.hpp"
#include "track_api_live.hpp"
#include "transport_api_live.hpp"
#include "undo_api_live.hpp"

namespace magda {

class MidiBridge;

/// Live implementation: every sub-interface forwards to the matching MAGDA singleton.
class MagdaApiLive : public MagdaApi {
  public:
    SelectionApi& selection() override {
        return selection_;
    }
    AutomationApi& automation() override {
        return automation_;
    }
    AliasApi& aliases() override {
        return aliases_;
    }
    TrackApi& tracks() override {
        return tracks_;
    }
    ClipApi& clips() override {
        return clips_;
    }
    SessionApi& session() override {
        return session_;
    }
    ProjectApi& project() override {
        return project_;
    }
    UndoApi& undo() override {
        return undo_;
    }
    MidiApi& midi() override {
        return midi_;
    }
    TransportApi& transport() override {
        return transport_;
    }
    FocusedApi& focused() override {
        return focused_;
    }

    /** Wire the MidiBridge into the live MidiApi. Owned externally by the
     *  engine wrapper; safe to leave unset in headless tests. */
    void setMidiBridge(MidiBridge* bridge) {
        midi_.setMidiBridge(bridge);
    }

    void setDefaultMidiOutputPort(const juce::String& port) {
        midi_.setDefaultOutputPort(port);
    }

    /** Wire the current-Edit accessor into the live TransportApi. */
    void setEditAccessor(TransportApiLive::EditGetter g) {
        transport_.setEditGetter(std::move(g));
    }

    /** Wire play / stop dispatchers so script-driven transport calls
     *  flow through TimelineController (matching the on-screen
     *  buttons). With no dispatcher set, calls go straight to Tracktion. */
    void setTransportPlayDispatcher(TransportApiLive::TransportFn fn) {
        transport_.setPlayDispatcher(std::move(fn));
    }
    void setTransportStopDispatcher(TransportApiLive::TransportFn fn) {
        transport_.setStopDispatcher(std::move(fn));
    }
    void setTransportLoopDispatcher(std::function<void(bool)> fn) {
        transport_.setLoopDispatcher(std::move(fn));
    }

  private:
    SelectionApiLive selection_;
    AutomationApiLive automation_;
    AliasApiLive aliases_;
    TrackApiLive tracks_;
    ClipApiLive clips_;
    SessionApiLive session_;
    ProjectApiLive project_;
    UndoApiLive undo_;
    MidiApiLive midi_;
    TransportApiLive transport_;
    FocusedApiLive focused_;
};

}  // namespace magda
