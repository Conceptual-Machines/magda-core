#include "MainView.hpp"

#include <BinaryData.h>

#include <cmath>
#include <set>

#include "../components/automation/AutomationMenu.hpp"
#include "../components/automation/MasterAutomationLanes.hpp"
#include "../components/common/SideColumn.hpp"
#include "../components/mixer/ClickableLabel.hpp"
#include "../components/mixer/LevelMeter.hpp"
#include "../components/navigation/SongNavigatorPanel.hpp"
#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"
#include "Config.hpp"
#include "audio/AudioBridge.hpp"
#include "audio/controllers/ControllerParamWriter.hpp"
#include "audio/controllers/ControllerRouter.hpp"
#include "core/ClipCommands.hpp"
#include "core/ClipInfo.hpp"
#include "core/ClipManager.hpp"
#include "core/LinkModeManager.hpp"
#include "core/SelectionManager.hpp"
#include "core/StringTable.hpp"
#include "core/TechnicalText.hpp"
#include "core/TrackCommands.hpp"
#include "core/TrackManager.hpp"
#include "core/TrackPropertyCommands.hpp"
#include "core/UndoManager.hpp"
#include "core/aliases/AliasRegistry.hpp"
#include "core/aliases/CuratedAliasLoader.hpp"
#include "core/controllers/BindingRegistry.hpp"
#include "core/controllers/ControllerRegistry.hpp"
#include "core/controllers/MidiLearnCoordinator.hpp"
#include "engine/TracktionEngineWrapper.hpp"
#include "project/ProjectManager.hpp"

namespace magda {

// dB conversion helpers for meters
namespace {
constexpr float MIN_DB = -60.0f;

float gainToDb(float gain) {
    if (gain <= 0.0f)
        return MIN_DB;
    return 20.0f * std::log10(gain);
}

float dbToGain(float db) {
    if (db <= MIN_DB)
        return 0.0f;
    return std::pow(10.0f, db / 20.0f);
}

juce::String formatDbValue(float db) {
    if (db <= MIN_DB)
        return "-inf";
    if (std::abs(db) < 0.05f)
        db = 0.0f;
    return juce::String(db, 1);
}

// Route through the position-aware tempo facade when wired (message thread);
// bpm fallback only before injection.
double timelineStartSeconds(const ClipInfo& clip, double bpm) {
    if (auto* tc = TimelineController::getCurrent(); tc && tc->tempoMap())
        return clip.getTimelineStart(*tc->tempoMap());
    return clip.getTimelineStart(bpm);
}

double timelineEndSeconds(const ClipInfo& clip, double bpm) {
    if (auto* tc = TimelineController::getCurrent(); tc && tc->tempoMap())
        return clip.getTimelineEnd(*tc->tempoMap());
    return clip.getTimelineEnd(bpm);
}

}  // namespace

MainView::MainView(AudioEngine* audioEngine)
    : horizontalZoom(10.0),
      playheadPosition(0.0),
      initialZoomSet(false),
      audioEngine_(audioEngine) {
    // Load configuration
    auto& config = magda::Config::getInstance();
    config.load();

    // Load parameter alias layers
    CuratedAliasLoader::loadFromBinary();
    AliasRegistry::getInstance().loadUserGlobal(config.getParamAliases());

    // Load controller + binding state and start the MIDI dispatch router
    ControllerRegistry::getInstance().loadFromConfig(config.getControllers());
    BindingRegistry::getInstance().loadGlobal(config.getGlobalBindings());

    // Enforce one-enabled-controller-per-port. Multiple rows on the same
    // hardware port are fine (different profiles for the same device), but
    // only one should be firing at a time. Older configs may have ended up
    // with bindings registered for several rows on the same port; walk
    // newest-to-oldest and silence (drop bindings of) every row whose port
    // already has an enabled neighbour.
    {
        auto& cReg = ControllerRegistry::getInstance();
        auto& bReg = BindingRegistry::getInstance();
        auto rows = cReg.all();
        std::set<juce::String> portWithEnabled;
        bool changed = false;
        for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
            if (it->inputPort.isEmpty())
                continue;
            const bool hasBindings = bReg.hasAnyBindingForController(it->id);
            if (!hasBindings)
                continue;
            if (!portWithEnabled.insert(it->inputPort).second) {
                bReg.removeAllForController(BindingScope::Global, it->id);
                bReg.removeAllForController(BindingScope::Project, it->id);
                changed = true;
            }
        }
        if (changed) {
            config.setGlobalBindings(bReg.saveGlobal());
            config.save();
        }
    }

    ControllerRouter::getInstance().reconfigure();
    if (auto* audioBridge = audioEngine_->getAudioBridge())
        ControllerRouter::getInstance().setParamWriter(
            std::make_unique<DefaultControllerParamWriter>(*audioBridge));
    if (auto* midiBridge = audioEngine_->getMidiBridge())
        ControllerRouter::getInstance().setMidiBridge(midiBridge);

    // Attach MIDI Learn coordinator to the router and seed scope from config
    magda::MidiLearnCoordinator::getInstance().attach(ControllerRouter::getInstance());
    magda::MidiLearnCoordinator::getInstance().setScope(
        static_cast<magda::BindingScope>(config.getMidiLearnDefaultScopeRaw()));

    // Apply language from config (overrides the en.json auto-loaded by StringTable constructor)
    {
        auto lang = juce::String(config.getLanguage());
        if (lang != "en")
            StringTable::getInstance().loadLanguage(lang);
    }

    DBG("CONFIG: Timeline length=" << config.getDefaultTimelineLengthBars() << " bars");
    DBG("CONFIG: Default zoom view=" << config.getDefaultZoomViewBars() << " bars");

    // Apply auto-save settings from config
    magda::ProjectManager::getInstance().setAutoSaveEnabled(config.getAutoSaveEnabled(),
                                                            config.getAutoSaveIntervalSeconds());

    // Make this component focusable to receive keyboard events
    setWantsKeyboardFocus(true);

    // Set up the centralized timeline controller
    setupTimelineController();

    // Set up UI components
    setupComponents();

    // Set up callbacks
    setupCallbacks();

    // Set up timeline zoom/scroll callbacks
    setupTimelineCallbacks();

    // Register as TrackManager and ViewModeController listener
    TrackManager::getInstance().addListener(this);
    ViewModeController::getInstance().addListener(this);
    currentViewMode_ = ViewModeController::getInstance().getViewMode();

    // Initialize master visibility
    const auto& master = TrackManager::getInstance().getMasterChannel();
    masterVisible_ = master.isVisibleIn(currentViewMode_);

    // Start timer for metering + scrollbar fade animation (60 FPS for smooth fades)
    startTimerHz(60);
}

void MainView::setupTimelineController() {
    timelineController = std::make_unique<TimelineController>();
    timelineController->addListener(this);

    // Sync initial state from controller
    syncStateFromController();
}

void MainView::syncStateFromController() {
    const auto& state = timelineController->getState();

    // Update cached values
    horizontalZoom = state.zoom.horizontalZoom;
    timelineLength = state.timelineLength;
    playheadPosition = state.playhead.getPosition();

    // Update selection and loop caches
    timeSelection = state.selection;
    loopRegion = state.loop;
}

void MainView::setupComponents() {
    // Create marker lane viewport
    markerLaneViewport = std::make_unique<juce::Viewport>();
    markerLane = std::make_unique<MarkerLaneComponent>();
    markerLane->setController(timelineController.get());
    markerLaneViewport->setViewedComponent(markerLane.get(), false);
    markerLaneViewport->setScrollBarsShown(false, false);
    addAndMakeVisible(*markerLaneViewport);

    // Create timeline viewport
    timelineViewport = std::make_unique<juce::Viewport>();
    timeline = std::make_unique<TimelineComponent>();
    timeline->setController(timelineController.get());  // Connect to centralized state
    timelineViewport->setViewedComponent(timeline.get(), false);
    timelineViewport->setScrollBarsShown(false, false);
    addAndMakeVisible(*timelineViewport);

    // Create track headers viewport and panel (vertical scroll synced with content viewport)
    trackHeadersViewport = std::make_unique<juce::Viewport>();
    trackHeadersViewport->setScrollBarsShown(false, false);  // No scrollbars - synced externally
    trackHeadersPanel = std::make_unique<TrackHeadersPanel>(audioEngine_);
    trackHeadersViewport->setViewedComponent(trackHeadersPanel.get(),
                                             false);  // false = don't delete
    addAndMakeVisible(*trackHeadersViewport);

    // Create track content viewport — also set as scroll target for headers panel
    // (wired after content viewport creation below)
    trackContentViewport = std::make_unique<juce::Viewport>();
    trackContentViewport->setWantsKeyboardFocus(
        false);  // Let TrackContentPanel handle keyboard focus
    trackContentPanel = std::make_unique<TrackContentPanel>();
    trackContentPanel->setController(timelineController.get());  // Connect to centralized state
    trackContentPanel->setAudioEngine(audioEngine_);
    trackContentViewport->setViewedComponent(trackContentPanel.get(), false);
    trackContentViewport->setScrollBarsShown(false, false, true, true);
    addAndMakeVisible(*trackContentViewport);

    // Wire track headers scroll to content viewport
    trackHeadersPanel->setScrollTarget(trackContentViewport.get());

    // Wire file-drag ghost-header previews from content panel → headers panel.
    trackContentPanel->onGhostHeadersChanged = [this](const juce::StringArray& labels) {
        if (trackHeadersPanel)
            trackHeadersPanel->setGhostHeaders(labels);
    };

    // Create grid overlay component (vertical time grid lines - below selection and playhead)
    gridOverlay = std::make_unique<GridOverlayComponent>();
    gridOverlay->setController(timelineController.get());
    addAndMakeVisible(*gridOverlay);

    // Create selection overlay component (below playhead)
    selectionOverlay = std::make_unique<SelectionOverlayComponent>(*this);
    addAndMakeVisible(*selectionOverlay);

    // Create playhead component (always on top)
    playheadComponent = std::make_unique<PlayheadComponent>(*this);
    addAndMakeVisible(*playheadComponent);
    playheadComponent->toFront(false);

    // Create fixed aux track section (above master)
    auxHeadersPanel = std::make_unique<AuxHeadersPanel>();
    addAndMakeVisible(*auxHeadersPanel);
    auxHeadersPanel->setVisible(false);
    auxContentPanel = std::make_unique<AuxContentPanel>();
    addAndMakeVisible(*auxContentPanel);
    auxContentPanel->setVisible(false);

    // Create fixed master track row at bottom (matching track panel style)
    masterHeaderPanel = std::make_unique<MasterHeaderPanel>();
    addAndMakeVisible(*masterHeaderPanel);
    masterContentPanel = std::make_unique<SongNavigatorPanel>();
    masterContentPanel->setController(timelineController.get());
    addAndMakeVisible(*masterContentPanel);

    // Master automation band (above the master strip): fixed header column +
    // a content viewport scroll-synced to the arrangement.
    masterAutomationHeaderPanel = std::make_unique<MasterAutomationHeaderPanel>();
    addAndMakeVisible(*masterAutomationHeaderPanel);
    masterAutomationViewport = std::make_unique<juce::Viewport>();
    masterAutomationContentPanel = std::make_unique<MasterAutomationContentPanel>();
    masterAutomationViewport->setViewedComponent(masterAutomationContentPanel.get(), false);
    masterAutomationViewport->setScrollBarsShown(false, false);
    addAndMakeVisible(*masterAutomationViewport);
    // The time grid and the playhead line both extend down through this band, so
    // re-raise them above the band components (created after them). Both are
    // click-through, so they do not block lane editing. Grid first, playhead on
    // top of it.
    gridOverlay->toFront(false);
    playheadComponent->toFront(false);
    // A lane added / removed / resized changes the band height: re-run the
    // arrangement layout so the band and the tracks above it resize.
    masterAutomationContentPanel->onBandHeightChanged = [this]() {
        resized();
        repaint();
    };

    // Create horizontal zoom scroll bar (at bottom)
    horizontalZoomScrollBar =
        std::make_unique<ZoomScrollBar>(ZoomScrollBar::Orientation::Horizontal);
    horizontalZoomScrollBar->onRangeChanged = [this](double start, double end) {
        revealHorizontalArrangementScrollbar();

        // Convert range to zoom and scroll
        double rangeWidth = end - start;
        const auto& st = timelineController->getState();
        double totalBeats = st.timelineLengthBeats;
        if (rangeWidth > 0 && totalBeats > 0) {
            // Calculate zoom: smaller range = higher zoom
            int viewportWidth = trackContentViewport->getWidth();
            double newZoom = static_cast<double>(viewportWidth) / (rangeWidth * totalBeats);

            // Calculate scroll position (in beats, then to pixels)
            double scrollBeats = start * totalBeats;
            int scrollX = static_cast<int>(scrollBeats * newZoom);

            // Dispatch to TimelineController
            timelineController->dispatch(SetZoomEvent{newZoom});
            timelineController->dispatch(
                SetScrollPositionEvent{scrollX, trackContentViewport->getViewPositionY()});
        }
    };
    addAndMakeVisible(*horizontalZoomScrollBar);
    horizontalZoomScrollBar->setVisible(false);
    horizontalZoomScrollBar->setAlpha(horizontalScrollbarRevealProgress);

    // Create vertical zoom scroll bar (on left)
    verticalZoomScrollBar = std::make_unique<ZoomScrollBar>(ZoomScrollBar::Orientation::Vertical);
    verticalZoomScrollBar->onRangeChanged = [this](double start, double end) {
        revealVerticalArrangementScrollbar();

        double rangeHeight = end - start;
        if (rangeHeight > 0) {
            // Guard to prevent feedback loop: setViewPosition triggers scrollBarMoved
            // which would call updateVerticalZoomScrollBar and fight the user's drag
            isUpdatingFromVerticalZoomScrollBar = true;

            // Calculate vertical zoom: bigger range = higher zoom (taller tracks)
            // rangeHeight 0->1 maps to zoom 0.5->3.0
            double newVerticalZoom = juce::jlimit(0.5, 3.0, 0.5 + rangeHeight * 2.5);
            verticalZoom = newVerticalZoom;

            // Update track heights FIRST so getTotalTracksHeight reflects the
            // new zoom. Then compute scroll position from the scaled total.
            trackContentPanel->setVerticalZoom(verticalZoom);
            trackHeadersPanel->setVerticalZoom(verticalZoom);

            // getTotalTracksHeight already incorporates verticalZoom per track,
            // so no extra multiplication here. No jmax with viewport height —
            // the two panels must end up at the exact same content size to
            // stay in scroll sync (otherwise one viewport can scroll past the
            // other and they visually drift on first scroll-down).
            int scaledHeight = trackHeadersPanel->getTotalTracksHeight();
            int scrollY = static_cast<int>(start * scaledHeight);

            int contentWidth = trackContentPanel->getWidth();
            trackContentPanel->setSize(contentWidth, scaledHeight);
            trackHeadersPanel->setSize(trackHeaderWidth, scaledHeight);

            trackContentViewport->setViewPosition(trackContentViewport->getViewPositionX(),
                                                  scrollY);
            trackHeadersViewport->setViewPosition(0, scrollY);
            playheadComponent->repaint();

            isUpdatingFromVerticalZoomScrollBar = false;
        }
    };
    addAndMakeVisible(*verticalZoomScrollBar);
    verticalZoomScrollBar->setVisible(false);
    verticalZoomScrollBar->setAlpha(verticalScrollbarRevealProgress);

    // Corner toolbar buttons (above track headers)
    // Zoom icon buttons
    auto setupCornerButton = [this](std::unique_ptr<SvgButton>& btn, const juce::String& name,
                                    const char* svgData, size_t svgSize) {
        btn = std::make_unique<SvgButton>(name, svgData, svgSize);
        btn->setOriginalColor(juce::Colour(0xFFB3B3B3));
        btn->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        btn->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        btn->setPressedColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        btn->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
        btn->setBorderThickness(1.0f);
        btn->setWantsKeyboardFocus(false);
        addAndMakeVisible(*btn);
    };

    setupCornerButton(zoomFitButton, "ZoomFit", BinaryData::zoom_out_map_svg,
                      BinaryData::zoom_out_map_svgSize);
    zoomFitButton->onClick = [this]() { resetZoomToFitTimeline(); };
    zoomFitButton->setTooltip("Zoom to fit timeline");

    setupCornerButton(zoomSelButton, "ZoomSel", BinaryData::fit_width_svg,
                      BinaryData::fit_width_svgSize);
    zoomSelButton->onClick = [this]() { zoomToSelection(); };
    zoomSelButton->setTooltip("Zoom to selection");

    setupCornerButton(markerLaneToggleButton, "MarkerLaneToggle", BinaryData::location_svg,
                      BinaryData::location_svgSize);
    markerLaneToggleButton->setClickingTogglesState(true);
    markerLaneToggleButton->setToggleState(markerLaneVisible_, juce::dontSendNotification);
    markerLaneToggleButton->onClick = [this]() {
        markerLaneVisible_ = markerLaneToggleButton->getToggleState();
        markerLaneToggleButton->setTooltip(markerLaneVisible_ ? "Hide marker lane"
                                                              : "Show marker lane");
        markerLaneViewport->setVisible(markerLaneVisible_);
        timeline->setMarkerLaneVisible(markerLaneVisible_);
        resized();
    };
    markerLaneToggleButton->setTooltip("Hide marker lane");

    setupCornerButton(secondsRulerToggleButton, "SecondsRulerToggle", BinaryData::clock_svg,
                      BinaryData::clock_svgSize);
    secondsRulerToggleButton->setClickingTogglesState(true);
    secondsRulerToggleButton->setToggleState(secondsRulerVisible_, juce::dontSendNotification);
    secondsRulerToggleButton->onClick = [this]() {
        secondsRulerVisible_ = secondsRulerToggleButton->getToggleState();
        secondsRulerToggleButton->setTooltip(secondsRulerVisible_ ? "Hide seconds ruler"
                                                                  : "Show seconds ruler");
        timeline->setSecondsRulerVisible(secondsRulerVisible_);
    };
    secondsRulerToggleButton->setTooltip("Show seconds ruler");

    setupCornerButton(zoomLoopButton, "ZoomLoop", BinaryData::fit_loop_svg,
                      BinaryData::fit_loop_svgSize);
    zoomLoopButton->onClick = [this]() {
        const auto& loop = timelineController->getState().loop;
        if (loop.isValid()) {
            timelineController->dispatch(ZoomToFitBeatsEvent{loop.startBeats, loop.endBeats, 0.05});
        }
    };
    zoomLoopButton->setTooltip("Zoom to loop region");

    // S = density_small.svg (4 rows = compact), M = density_medium.svg (3 rows), L =
    // density_large.svg (2 rows = spacious)
    setupCornerButton(trackSmallButton, "TrackSmall", BinaryData::density_small_svg,
                      BinaryData::density_small_svgSize);
    trackSmallButton->onClick = [this]() { setAllTrackHeights(47); };
    trackSmallButton->setTooltip("Compact track height");

    setupCornerButton(trackMediumButton, "TrackMedium", BinaryData::density_medium_svg,
                      BinaryData::density_medium_svgSize);
    trackMediumButton->onClick = [this]() { setAllTrackHeights(78); };
    trackMediumButton->setTooltip("Medium track height");

    setupCornerButton(trackLargeButton, "TrackLarge", BinaryData::density_large_svg,
                      BinaryData::density_large_svgSize);
    trackLargeButton->onClick = [this]() { setAllTrackHeights(140); };
    trackLargeButton->setTooltip("Large track height");

    setupCornerButton(ioToggleButton, "IOToggle", BinaryData::inputoutput_svg,
                      BinaryData::inputoutput_svgSize);
    ioToggleButton->onClick = [this]() {
        trackHeadersPanel->toggleIORouting();
        // Update button appearance to reflect state
        if (trackHeadersPanel->isIORoutingVisible()) {
            ioToggleButton->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        } else {
            ioToggleButton->setNormalColor(
                DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).withAlpha(0.3f));
        }
        updateContentSizes();
    };
    ioToggleButton->setTooltip("Toggle I/O routing");
    if (!trackHeadersPanel->isIORoutingVisible()) {
        ioToggleButton->setNormalColor(
            DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).withAlpha(0.3f));
    }

    setupCornerButton(addTrackButton, "AddTrack", BinaryData::add_svg, BinaryData::add_svgSize);
    addTrackButton->onClick = []() {
        UndoManager::getInstance().executeCommand(
            std::make_unique<CreateTrackCommand>(TrackType::Audio));
    };
    addTrackButton->setTooltip("Add track");

    // Footer corner toggle for the master track: hide icon when it's visible,
    // show icon when it's hidden (the hide control lives here, not in the
    // master header).
    setupCornerButton(showMasterButton, "ToggleMasterTrack", BinaryData::bottom_open_svg,
                      BinaryData::bottom_open_svgSize);
    showMasterButton->setTooltip("Show master track");
    showMasterButton->setOriginalColor(juce::Colour(0xFFB3B3B3));
    showMasterButton->setNormalColor(juce::Colour(0xFFB3B3B3));
    showMasterButton->onClick = [this]() {
        TrackManager::getInstance().setMasterVisible(
            ViewModeController::getInstance().getViewMode(), !masterVisible_);
    };

    // Axis label icons (non-interactive)
    setupCornerButton(hAxisIcon, "HAxis", BinaryData::horizontal_svg,
                      BinaryData::horizontal_svgSize);
    hAxisIcon->setInterceptsMouseClicks(false, false);
    // Faint watermark rather than a solid grey glyph.
    hAxisIcon->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).withAlpha(0.28f));
    hAxisIcon->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).withAlpha(0.28f));
    hAxisIcon->setBorderThickness(0.0f);

    setupCornerButton(vAxisIcon, "VAxis", BinaryData::vertical_svg, BinaryData::vertical_svgSize);
    vAxisIcon->setInterceptsMouseClicks(false, false);
    vAxisIcon->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).withAlpha(0.28f));
    vAxisIcon->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).withAlpha(0.28f));
    vAxisIcon->setBorderThickness(0.0f);

    // Set up scroll synchronization
    trackContentViewport->getHorizontalScrollBar().addListener(this);
    trackContentViewport->getVerticalScrollBar().addListener(this);

    // Set up track synchronization between headers and content
    setupTrackSynchronization();
}

void MainView::setupCallbacks() {
    // Apply any user gesture-binding overrides saved in config.json on top of
    // the code-defined defaults (#22). Safe to call here: Config is loaded from
    // disk before the UI is built.
    GestureRouter::getInstance().loadFromConfig();

    // Set up timeline callbacks
    timeline->onPlayheadPositionBeatsChanged = [this](double positionBeats, bool bypassSnap) {
        dispatchUserPlayheadPositionBeats(positionBeats, bypassSnap);
    };

    // Mouse-wheel gestures over the arrangement (ruler + track content) resolve
    // through GestureRouter (#21) and dispatch here (#26). This is the first
    // real consumer of the gesture system and supersedes the per-platform wheel
    // handling that left a plain wheel dead over the arrangement on Linux.
    timeline->onArrangementGesture = [this](const ResolvedGesture& g) {
        dispatchArrangementGesture(g);
    };
    trackContentPanel->onArrangementGesture = [this](const ResolvedGesture& g) {
        dispatchArrangementGesture(g);
    };

    // Handle time selection from timeline ruler
    timeline->onTimeSelectionBeatsChanged = [this](double startBeats, double endBeats) {
        if (startBeats < 0 || endBeats < 0) {
            timelineController->dispatch(ClearTimeSelectionEvent{});
        } else {
            timelineController->dispatch(SetTimeSelectionBeatsEvent{startBeats, endBeats, {}});
            // Move playhead to follow the left side of selection
            timelineController->dispatch(SetPlayheadPositionBeatsEvent{startBeats});
        }
    };

    // Set up selection and loop callbacks
    setupSelectionCallbacks();
}

void MainView::dispatchArrangementGesture(const ResolvedGesture& gesture) {
    // Magnitude is the wheel delta already scaled by the binding's sensitivity
    // (and sign-corrected for invert / natural-scroll). The sign convention
    // here matches the old hand-rolled handler: a positive magnitude scrolls
    // the content the same way the wheel was pushed.
    switch (gesture.type) {
        case GestureActionType::ScrollHorizontal: {
            revealHorizontalArrangementScrollbar();
            timelineController->dispatch(
                ScrollByDeltaEvent{-static_cast<int>(gesture.magnitude), 0});
            break;
        }
        case GestureActionType::ScrollVertical: {
            // Vertical scroll is driven straight through the viewport, not the
            // controller (timelineStateChanged intentionally ignores scrollY).
            // The vertical scrollbar listener syncs the track headers.
            revealVerticalArrangementScrollbar();
            const auto pos = trackContentViewport->getViewPosition();
            trackContentViewport->setViewPosition(pos.x,
                                                  pos.y - static_cast<int>(gesture.magnitude));
            break;
        }
        case GestureActionType::ZoomHorizontal: {
            revealHorizontalArrangementScrollbar();
            const auto& state = timelineController->getState();
            const double currentZoom = state.zoom.horizontalZoom;
            if (currentZoom <= 0.0)
                break;

            // Multiplicative (exponential) zoom: magnitude is the power-of-two
            // exponent (already scaled by the binding's zoom sensitivity, which
            // is the configurable per-tick feel). Positive zooms in.
            double newZoom = currentZoom * std::pow(2.0, gesture.magnitude);

            auto& config = magda::Config::getInstance();
            newZoom = juce::jlimit(config.getMinZoomLevel(), config.getMaxZoomLevel(), newZoom);

            // Keep the beat under the cursor pinned. anchor.x is content-space
            // (the panels are the viewed components), so the beat comes from the
            // current zoom and the viewport-relative X is anchor.x - scrollX.
            const double anchorBeats = juce::jlimit(
                0.0, state.timelineLengthBeats,
                static_cast<double>(gesture.anchor.x - LayoutConfig::TIMELINE_LEFT_PADDING) /
                    currentZoom);
            const int anchorViewportX = gesture.anchor.x - state.zoom.scrollX;

            timelineController->dispatch(
                SetZoomAnchoredBeatsEvent{newZoom, anchorBeats, anchorViewportX});
            break;
        }
        case GestureActionType::ZoomVertical: {
            revealVerticalArrangementScrollbar();
            applyVerticalZoom(verticalZoom * std::pow(2.0, gesture.magnitude));
            break;
        }
        case GestureActionType::Pan:
        case GestureActionType::None:
            break;
    }
}

void MainView::applyVerticalZoom(double newVerticalZoom) {
    verticalZoom = juce::jlimit(0.5, 3.0, newVerticalZoom);

    trackContentPanel->setVerticalZoom(verticalZoom);
    trackHeadersPanel->setVerticalZoom(verticalZoom);

    // Both panels must end at the exact same content height to stay in scroll
    // sync (see the vertical zoom scrollbar handler for the rationale).
    const int scaledHeight = trackHeadersPanel->getTotalTracksHeight();
    trackContentPanel->setSize(trackContentPanel->getWidth(), scaledHeight);
    trackHeadersPanel->setSize(trackHeaderWidth, scaledHeight);

    updateVerticalZoomScrollBar();
    playheadComponent->repaint();
}

MainView::~MainView() {
    // Stop metering timer
    stopTimer();

    // Remove listener before destruction
    if (timelineController) {
        timelineController->removeListener(this);
    }

    // Unregister from TrackManager and ViewModeController
    TrackManager::getInstance().removeListener(this);
    ViewModeController::getInstance().removeListener(this);

    // Save configuration on shutdown
    auto& config = magda::Config::getInstance();
    config.save();
}

// ===== Timer Implementation (for metering) =====

void MainView::timerCallback() {
    updateArrangementScrollbarVisibility();

    // Update master metering from audio engine
    if (!audioEngine_ || !masterHeaderPanel)
        return;

    auto* teWrapper = dynamic_cast<TracktionEngineWrapper*>(audioEngine_);
    if (!teWrapper)
        return;

    auto* bridge = teWrapper->getAudioBridge();
    if (!bridge)
        return;

    // Update master header panel with real levels
    float masterPeakL = bridge->getMasterPeakL();
    float masterPeakR = bridge->getMasterPeakR();
    masterHeaderPanel->setPeakLevels(masterPeakL, masterPeakR);

    // Update aux section metering
    if (auxHeadersPanel && auxVisible_) {
        auxHeadersPanel->updateMetering(audioEngine_);
    }
}

// ===== TimelineStateListener Implementation =====

void MainView::timelineStateChanged(const TimelineState& state, ChangeFlags changes) {
    // Timeline length changes: the ruler and track content cache their own
    // length, so push the new value to them (e.g. from Project Settings). The
    // Zoom flag that accompanies a length change handles the resize/scrollbars.
    if (hasFlag(changes, ChangeFlags::Timeline)) {
        timeline->setTimelineLength(state.timelineLength);
        trackContentPanel->setTimelineLength(state.timelineLength);
    }

    // Zoom/scroll changes
    if (hasFlag(changes, ChangeFlags::Zoom) || hasFlag(changes, ChangeFlags::Scroll)) {
        if (hasFlag(changes, ChangeFlags::Zoom) || hasFlag(changes, ChangeFlags::Scroll))
            revealHorizontalArrangementScrollbar();

        horizontalZoom = state.zoom.horizontalZoom;

        timeline->setZoom(horizontalZoom);
        if (markerLane)
            markerLane->repaint();
        trackContentPanel->setZoom(horizontalZoom);
        trackContentPanel->setVerticalZoom(verticalZoom);

        markerLaneViewport->setViewPosition(state.zoom.scrollX, 0);
        timelineViewport->setViewPosition(state.zoom.scrollX, 0);
        if (masterAutomationViewport)
            masterAutomationViewport->setViewPosition(state.zoom.scrollX, 0);
        // Preserve current vertical scroll — state.zoom.scrollY may be stale
        // since vertical scrolling doesn't always dispatch to the controller
        int currentScrollY = trackContentViewport->getViewPositionY();
        trackContentViewport->setViewPosition(state.zoom.scrollX, currentScrollY);

        updateContentSizes();
        updateHorizontalZoomScrollBar();
        updateVerticalZoomScrollBar();
        updateGridDivisionDisplay();

        // Only repaint the scrollbar area, not the entire view.
        // Child components (timeline, trackContentPanel, gridOverlay, playhead)
        // handle their own repaints via their own timelineStateChanged listeners.
        // SelectionOverlayComponent is NOT a listener — it has to be repainted
        // here so the loop/selection/recording lines re-derive their X positions
        // against the new zoom/scroll, otherwise they stay frozen at the old
        // pixel column and visibly drift away from the ruler markers.
        if (selectionOverlay) {
            selectionOverlay->repaint();
        }
    }

    // Playhead changes
    if (hasFlag(changes, ChangeFlags::Playhead)) {
        playheadPosition = state.playhead.getPosition();
        playheadComponent->setPlayheadPosition(playheadPosition);
        playheadComponent->repaint();

        // Repaint recording overlay when playhead moves during recording
        if (state.playhead.isRecording) {
            selectionOverlay->repaint();
        }

        // Auto-scroll: keep the playhead in view while playing. When it runs off
        // the right edge (or jumps back, e.g. on loop), page the arrangement so
        // it sits near the left. Dispatching re-enters with a Scroll flag, which
        // syncs every horizontal surface (handled above).
        if (state.playhead.isPlaying && Config::getInstance().getFollowPlayhead()) {
            const int viewW = state.zoom.viewportWidth;
            if (viewW > 0) {
                const int playX = state.timeToPixelLocal(playheadPosition);
                const int scrollX = state.zoom.scrollX;
                const int margin = 48;
                if (playX > scrollX + viewW - margin || playX < scrollX) {
                    timelineController->dispatch(
                        SetScrollPositionEvent{juce::jmax(0, playX - margin), -1});
                }
            }
        }

        if (onPlayheadPositionChanged) {
            onPlayheadPositionChanged(playheadPosition);
        }
    }

    // Selection changes
    if (hasFlag(changes, ChangeFlags::Selection)) {
        timeSelection = state.selection;

        if (timeSelection.isVisuallyActive()) {
            timeline->setTimeSelectionBeats(timeSelection.startBeats, timeSelection.endBeats);
        } else {
            timeline->clearTimeSelection();
        }

        if (selectionOverlay) {
            selectionOverlay->repaint();
        }

        if (onTimeSelectionChanged) {
            onTimeSelectionChanged(timeSelection.startTime, timeSelection.endTime,
                                   timeSelection.isActive());
        }

        if (onEditCursorChanged) {
            onEditCursorChanged(state.editCursorPosition);
        }
    }

    // Loop changes
    if (hasFlag(changes, ChangeFlags::Loop)) {
        loopRegion = state.loop;

        isUpdatingLoopRegion = true;

        if (loopRegion.isValid()) {
            timeline->setLoopRegionBeats(loopRegion.startBeats, loopRegion.endBeats);
            timeline->setLoopEnabled(loopRegion.enabled);
        } else {
            timeline->clearLoopRegion();
        }

        isUpdatingLoopRegion = false;

        if (selectionOverlay) {
            selectionOverlay->repaint();
        }

        if (onLoopRegionChanged) {
            if (loopRegion.isValid()) {
                onLoopRegionChanged(loopRegion.startTime, loopRegion.endTime, loopRegion.enabled);
            } else {
                onLoopRegionChanged(-1.0, -1.0, false);
            }
        }
    }

    // Display config changes
    if (hasFlag(changes, ChangeFlags::Display)) {
        updateGridDivisionDisplay();

        if (onGridQuantizeChanged) {
            const auto& gq = state.display.gridQuantize;
            onGridQuantizeChanged(gq.autoGrid, gq.numerator, gq.denominator, false);
        }
    }

    // Tempo changes
    if (hasFlag(changes, ChangeFlags::Tempo)) {
        if (onTempoChanged) {
            onTempoChanged(state.tempo.bpm);
        }
        if (onTimeSignatureChanged) {
            onTimeSignatureChanged(state.tempo.timeSignatureNumerator,
                                   state.tempo.timeSignatureDenominator);
        }
        if (timeline) {
            timeline->setTimeSignature(state.tempo.timeSignatureNumerator,
                                       state.tempo.timeSignatureDenominator);
        }
        if (trackContentPanel) {
            trackContentPanel->setTimeSignature(state.tempo.timeSignatureNumerator,
                                                state.tempo.timeSignatureDenominator);
        }
    }

    // Punch changes
    if (hasFlag(changes, ChangeFlags::Punch)) {
        if (onPunchRegionChanged) {
            if (state.punch.isValid()) {
                onPunchRegionChanged(state.punch.startTime, state.punch.endTime,
                                     state.punch.punchInEnabled, state.punch.punchOutEnabled);
            } else {
                onPunchRegionChanged(-1.0, -1.0, false, false);
            }
        }
    }

    // Always sync cached state
    syncStateFromController();
}

// ===== TrackManagerListener Implementation =====

void MainView::masterChannelChanged() {
    const auto& master = TrackManager::getInstance().getMasterChannel();
    bool newVisible = master.isVisibleIn(currentViewMode_);

    if (newVisible != masterVisible_) {
        masterVisible_ = newVisible;
        masterHeaderPanel->setVisible(masterVisible_);
        masterContentPanel->setVisible(masterVisible_);
        resized();
        repaint();  // clear the vacated master-strip area (stale-paint fix)
    }
}

// ===== ViewModeListener Implementation =====

void MainView::viewModeChanged(ViewMode mode, const AudioEngineProfile& /*profile*/) {
    currentViewMode_ = mode;

    // Update master visibility for new view mode
    const auto& master = TrackManager::getInstance().getMasterChannel();
    bool newVisible = master.isVisibleIn(currentViewMode_);

    if (newVisible != masterVisible_) {
        masterVisible_ = newVisible;
        masterHeaderPanel->setVisible(masterVisible_);
        masterContentPanel->setVisible(masterVisible_);
    }

    // Update aux visibility for new view mode
    tracksChanged();

    resized();
    repaint();  // clear stale pixels from the previous view (song map / grid)
}

void MainView::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));

    // Draw top border for visual separation from transport above
    g.setColour(DarkTheme::getBorderColour());
    g.fillRect(0, 0, getWidth(), 1);

    // Draw corner toolbar separator lines
    if (!markerLaneSeparatorLine.isEmpty()) {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.fillRect(markerLaneSeparatorLine);
    }
    if (!cornerSeparatorLine.isEmpty()) {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.fillRect(cornerSeparatorLine);
    }
    if (!cornerBottomBorderLine.isEmpty()) {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.fillRect(cornerBottomBorderLine);
    }
    if (!markerCornerRightBorderLine.isEmpty()) {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.fillRect(markerCornerRightBorderLine);
    }

    auto arrangementLayout = computeArrangementLayout();
    SideColumn headerColumn(!arrangementLayout.swapped);
    auto contentArea = arrangementLayout.horizontalScrollBarRowArea;
    auto headerArea = headerColumn.removeFrom(contentArea, trackHeaderWidth);
    headerColumn.removeSpacing(contentArea, LayoutConfig::getInstance().componentSpacing);

    g.setColour(DarkTheme::getColour(DarkTheme::TRACK_BACKGROUND));
    g.fillRect(contentArea);
    g.setColour(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
    g.fillRect(headerArea);

    // Draw borders on both sides of the vertical zoom scrollbar (below corner toolbar)
    if (verticalScrollbarRevealProgress > 0.01f) {
        auto sb = verticalZoomScrollBar->getBounds();
        int top = getTimelineHeight();
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER)
                        .withMultipliedAlpha(verticalScrollbarRevealProgress));
        g.fillRect(sb.getX() - 1, top, 1, getHeight() - top);
        g.fillRect(sb.getRight(), top, 1, getHeight() - top);
    }

    // Draw resize handles
    paintResizeHandle(g);
    paintMasterResizeHandle(g);
}

MainView::ArrangementLayout MainView::computeArrangementLayout() const {
    ArrangementLayout result;
    auto bounds = getLocalBounds();
    auto& layout = LayoutConfig::getInstance();

    result.swapped = Config::getInstance().getScrollbarOnLeft();
    SideColumn headerColumn(!result.swapped);
    // Scrollbar lives opposite the header column. When the user picks
    // "headers on right", the vertical scrollbar sits on the left edge
    // (and vice versa) so the two never share an edge and the headers
    // never have to slide aside when the scrollbar reveals.
    SideColumn zoomColumn(result.swapped);

    const int fullVerticalScrollbarWidth = ARRANGEMENT_SCROLLBAR_SIZE + 2;
    // Interpolate reserved width with the fade progress so the header column
    // slides smoothly in/out with the scrollbar (avoids a layout snap when the
    // scrollbar shares an edge with the track headers).
    const int verticalScrollbarWidth = juce::roundToInt(
        juce::jlimit(0.0f, 1.0f, verticalScrollbarRevealProgress) * fullVerticalScrollbarWidth);
    // Reserve a fixed-height row for the horizontal scrollbar: the bar still
    // fades in/out via alpha, but the master strip stays put instead of sliding
    // up/down as the bar reveals/hides.
    const int horizontalScrollbarHeight = ARRANGEMENT_SCROLLBAR_SIZE;

    {
        auto hitBounds = bounds;
        result.verticalScrollBarHitArea =
            zoomColumn.removeFrom(hitBounds, ARRANGEMENT_SCROLLBAR_HIT_EDGE);
    }

    result.verticalScrollBarArea = zoomColumn.removeFrom(bounds, verticalScrollbarWidth);

    {
        auto hitBounds = bounds;
        result.horizontalScrollBarHitArea =
            hitBounds.removeFromBottom(ARRANGEMENT_SCROLLBAR_HIT_EDGE);
        headerColumn.removeSpacing(result.horizontalScrollBarHitArea,
                                   trackHeaderWidth + layout.componentSpacing);
    }

    result.horizontalScrollBarRowArea = bounds.removeFromBottom(horizontalScrollbarHeight);
    result.horizontalScrollBarArea = result.horizontalScrollBarRowArea;
    headerColumn.removeSpacing(result.horizontalScrollBarArea,
                               trackHeaderWidth + layout.componentSpacing);

    int effectiveMasterHeight = masterVisible_ ? masterStripHeight : 0;
    int effectiveResizeHandleHeight = masterVisible_ ? MASTER_RESIZE_HANDLE_HEIGHT : 0;

    if (masterVisible_) {
        auto masterRowArea = bounds.removeFromBottom(masterStripHeight);
        result.masterHeaderArea = headerColumn.removeFrom(masterRowArea, trackHeaderWidth);
        headerColumn.removeSpacing(masterRowArea, layout.componentSpacing);
        result.masterContentArea = masterRowArea;
    }

    // Master automation band: pinned directly above the master strip. Carved
    // after the master row (removeFromBottom) so it sits just above it.
    const int effectiveMasterAutomationHeight =
        masterVisible_ ? juce::jmax(0, masterAutomationHeight) : 0;
    if (effectiveMasterAutomationHeight > 0) {
        auto bandRow = bounds.removeFromBottom(effectiveMasterAutomationHeight);
        result.masterAutomationHeaderArea = headerColumn.removeFrom(bandRow, trackHeaderWidth);
        headerColumn.removeSpacing(bandRow, layout.componentSpacing);
        result.masterAutomationContentArea = bandRow;
    }

    if (auxVisible_) {
        auto auxRowArea = bounds.removeFromBottom(auxSectionHeight);
        result.auxHeadersArea = headerColumn.removeFrom(auxRowArea, trackHeaderWidth);
        headerColumn.removeSpacing(auxRowArea, layout.componentSpacing);
        result.auxContentArea = auxRowArea;
    }

    if (masterVisible_) {
        bounds.removeFromBottom(MASTER_RESIZE_HANDLE_HEIGHT);
    }

    int effectiveAuxHeight = auxVisible_ ? auxSectionHeight : 0;
    result.verticalScrollBarHitArea.removeFromBottom(
        ARRANGEMENT_SCROLLBAR_SIZE + effectiveMasterHeight + effectiveResizeHandleHeight +
        effectiveAuxHeight + effectiveMasterAutomationHeight);
    result.verticalScrollBarHitArea.removeFromTop(getTimelineHeight());
    result.verticalScrollBarHitArea = result.verticalScrollBarHitArea.reduced(1, 0);

    result.verticalScrollBarArea.removeFromBottom(
        horizontalScrollbarHeight + effectiveMasterHeight + effectiveResizeHandleHeight +
        effectiveAuxHeight + effectiveMasterAutomationHeight);
    result.verticalScrollBarArea.removeFromTop(getTimelineHeight());
    if (result.verticalScrollBarArea.getWidth() > 2)
        result.verticalScrollBarArea = result.verticalScrollBarArea.reduced(1, 0);

    auto timelineStripArea = bounds.removeFromTop(getTimelineHeight());
    result.cornerArea = headerColumn.removeFrom(timelineStripArea, trackHeaderWidth);

    headerColumn.removeSpacing(timelineStripArea, layout.componentSpacing);
    result.markerLaneArea = timelineStripArea.removeFromTop(getMarkerLaneHeight());
    result.timelineArea = timelineStripArea;
    result.trackHeadersArea = headerColumn.removeFrom(bounds, trackHeaderWidth);
    headerColumn.removeSpacing(bounds, layout.componentSpacing);

    result.trackContentArea = bounds;
    result.overlayArea = bounds;
    result.playheadArea =
        bounds.withTop(getTimelineHeight() - LayoutConfig::getInstance().playheadRowHeight);

    // Extend the playhead line down through the master automation band so it
    // tracks the tempo / master lanes too (the band was carved off the bottom
    // before this, so the playhead would otherwise stop above it).
    if (!result.masterAutomationContentArea.isEmpty())
        result.playheadArea.setBottom(result.masterAutomationContentArea.getBottom());

    return result;
}

void MainView::resized() {
    // Detect a genuine window/layout size change (not the per-frame relayout the
    // reveal state machine triggers, which keeps our bounds constant) and arm a
    // short window during which scrollbar reveals are ignored, so the bars don't
    // flash open while the window edge is being dragged.
    if (getWidth() != previousArrangementWidth || getHeight() != previousArrangementHeight) {
        arrangementScrollbarResizeSuppressFrames = ARRANGEMENT_SCROLLBAR_RESIZE_SUPPRESS_FRAMES;
        previousArrangementWidth = getWidth();
        previousArrangementHeight = getHeight();
    }

    // Band height must be known before computeArrangementLayout carves the
    // band area above the master strip.
    masterAutomationHeight = masterVisible_ ? masterAutomationBandHeight(verticalZoom) : 0;

    auto arrangementLayout = computeArrangementLayout();
    auto& layout = LayoutConfig::getInstance();

    horizontalZoomScrollBar->setBounds(arrangementLayout.horizontalScrollBarArea);
    verticalZoomScrollBar->setBounds(arrangementLayout.verticalScrollBarArea);
    horizontalScrollbarHitArea = arrangementLayout.horizontalScrollBarHitArea;
    verticalScrollbarHitArea = arrangementLayout.verticalScrollBarHitArea;

    horizontalZoomScrollBar->setAlpha(horizontalScrollbarRevealProgress);
    verticalZoomScrollBar->setAlpha(verticalScrollbarRevealProgress);
    horizontalZoomScrollBar->setVisible(horizontalScrollbarRevealProgress > 0.01f);
    verticalZoomScrollBar->setVisible(verticalScrollbarRevealProgress > 0.01f);

    if (masterVisible_) {
        masterHeaderPanel->setBounds(arrangementLayout.masterHeaderArea);
        masterContentPanel->setBounds(arrangementLayout.masterContentArea);
    } else {
        // Clear stale bounds when the master strip is hidden: otherwise the song
        // map / master header keep their previous-view bounds and can leave a
        // fragment behind when the view switches.
        masterHeaderPanel->setBounds({});
        masterContentPanel->setBounds({});
    }
    // Always-present footer toggle: hide icon when the master is visible, show
    // icon when it's hidden.
    showMasterButton->setVisible(true);
    showMasterButton->updateSvgData(
        masterVisible_ ? BinaryData::bottom_close_svg : BinaryData::bottom_open_svg,
        masterVisible_ ? BinaryData::bottom_close_svgSize : BinaryData::bottom_open_svgSize);
    showMasterButton->setTooltip(masterVisible_ ? "Hide master track" : "Show master track");
    {
        const int btnSize = 20;
        SideColumn headerColumn(!arrangementLayout.swapped);
        auto restoreArea = arrangementLayout.horizontalScrollBarRowArea;
        auto headerArea = headerColumn.removeFrom(restoreArea, trackHeaderWidth);
        showMasterButton->setBounds(
            headerArea.removeFromRight(btnSize).withSizeKeepingCentre(btnSize, btnSize));
        showMasterButton->toFront(false);
    }

    const bool bandVisible = masterVisible_ && masterAutomationHeight > 0;
    masterAutomationHeaderPanel->setVisible(bandVisible);
    masterAutomationViewport->setVisible(bandVisible);
    if (bandVisible) {
        masterAutomationHeaderPanel->setBounds(arrangementLayout.masterAutomationHeaderArea);
        masterAutomationViewport->setBounds(arrangementLayout.masterAutomationContentArea);
        masterAutomationHeaderPanel->setVerticalZoom(verticalZoom);
        masterAutomationContentPanel->setVerticalZoom(verticalZoom);
        masterAutomationContentPanel->setPixelsPerBeat(horizontalZoom);
        masterAutomationContentPanel->setTempoBPM(trackContentPanel->getTempo());
        masterAutomationContentPanel->setTimelineWidth(trackContentPanel->getWidth());
        masterAutomationViewport->setViewPosition(trackContentViewport->getViewPositionX(), 0);
    }

    if (auxVisible_) {
        auxHeadersPanel->setBounds(arrangementLayout.auxHeadersArea);
        auxContentPanel->setBounds(arrangementLayout.auxContentArea);
    }

    {
        const auto cornerArea = arrangementLayout.cornerArea;
        const int btnSize = 23;
        const int gap = 6;
        const int rowGap = 8;
        const int margin = 8;
        const int markerLaneHeight = getMarkerLaneHeight();
        const auto markerCornerArea = cornerArea.withHeight(markerLaneHeight);
        const auto timelineCornerArea = cornerArea.withTrimmedTop(markerLaneHeight);
        auto grid = timelineCornerArea.withTrimmedLeft(margin).withTrimmedRight(margin);
        // Centre the two button rows vertically so the icons get even top/bottom
        // padding inside the gutter rather than sitting flush against the marker
        // lane separator above.
        const int rowsBlockHeight = btnSize * 2 + rowGap;
        grid.removeFromTop(juce::jmax(0, (grid.getHeight() - rowsBlockHeight) / 2));
        auto topRow = grid.removeFromTop(btnSize);
        grid.removeFromTop(rowGap);
        auto botRow = grid.removeFromTop(btnSize);

        // Invalidate old separator line position before updating
        if (!markerLaneSeparatorLine.isEmpty())
            repaint(markerLaneSeparatorLine.expanded(1));
        if (!cornerSeparatorLine.isEmpty())
            repaint(cornerSeparatorLine.expanded(1));

        // Store separator line position (drawn in paint())
        // Span the full header column width (corner area + componentSpacing gap)
        int lineX = arrangementLayout.swapped ? cornerArea.getX() - layout.componentSpacing
                                              : cornerArea.getX();
        int lineW = cornerArea.getWidth() + layout.componentSpacing;
        markerLaneSeparatorLine =
            markerLaneVisible_ ? juce::Rectangle<int>(lineX, markerCornerArea.getBottom(), lineW, 1)
                               : juce::Rectangle<int>();
        // Vertical border closing off the marker-lane gutter from the marker
        // content beside it (the content sits opposite the header column).
        if (!markerCornerRightBorderLine.isEmpty())
            repaint(markerCornerRightBorderLine.expanded(1));
        const int markerBorderX = arrangementLayout.swapped
                                      ? arrangementLayout.markerLaneArea.getRight()
                                      : arrangementLayout.markerLaneArea.getX() - 1;
        markerCornerRightBorderLine =
            markerLaneVisible_
                ? juce::Rectangle<int>(markerBorderX, markerCornerArea.getY(), 1, markerLaneHeight)
                : juce::Rectangle<int>();
        cornerSeparatorLine =
            juce::Rectangle<int>(lineX, topRow.getBottom() + rowGap / 2, lineW, 1);
        // Bottom border closing off the gutter at the ruler/track boundary, so
        // it lines up with the ruler bottom and reads as separate from tracks.
        if (!cornerBottomBorderLine.isEmpty())
            repaint(cornerBottomBorderLine.expanded(1));
        cornerBottomBorderLine = juce::Rectangle<int>(lineX, cornerArea.getBottom() - 1, lineW, 1);

        // Top row: action buttons on inner side, axis label on outer side
        SideColumn btnSide(!arrangementLayout.swapped);
        SideColumn axisSide(arrangementLayout.swapped);

        zoomFitButton->setBounds(btnSide.removeFrom(topRow, btnSize));
        btnSide.removeSpacing(topRow, gap);
        zoomSelButton->setBounds(btnSide.removeFrom(topRow, btnSize));
        btnSide.removeSpacing(topRow, gap);
        zoomLoopButton->setBounds(btnSide.removeFrom(topRow, btnSize));
        btnSide.removeSpacing(topRow, gap);
        markerLaneToggleButton->setBounds(btnSide.removeFrom(topRow, btnSize));
        btnSide.removeSpacing(topRow, gap);
        secondsRulerToggleButton->setBounds(btnSide.removeFrom(topRow, btnSize));
        axisSide.removeSpacing(topRow, gap);
        hAxisIcon->setBounds(axisSide.removeFrom(topRow, btnSize));

        // Bottom row: action buttons on inner side, axis label on outer side.
        // The two show/hide toggles (markers above, I/O below) sit at the end of
        // each row, vertically aligned.
        trackSmallButton->setBounds(btnSide.removeFrom(botRow, btnSize));
        btnSide.removeSpacing(botRow, gap);
        trackMediumButton->setBounds(btnSide.removeFrom(botRow, btnSize));
        btnSide.removeSpacing(botRow, gap);
        trackLargeButton->setBounds(btnSide.removeFrom(botRow, btnSize));
        btnSide.removeSpacing(botRow, gap);
        ioToggleButton->setBounds(btnSide.removeFrom(botRow, btnSize));
        btnSide.removeSpacing(botRow, gap);
        addTrackButton->setBounds(btnSide.removeFrom(botRow, btnSize));
        axisSide.removeSpacing(botRow, gap);
        vAxisIcon->setBounds(axisSide.removeFrom(botRow, btnSize));
    }

    markerLaneViewport->setVisible(markerLaneVisible_);
    markerLaneViewport->setBounds(arrangementLayout.markerLaneArea);
    timelineViewport->setBounds(arrangementLayout.timelineArea);
    trackHeadersViewport->setBounds(arrangementLayout.trackHeadersArea);
    trackHeadersPanel->refreshHeaderSideLayout();
    trackContentViewport->setBounds(arrangementLayout.trackContentArea);

    // Grid overlay (bottom layer - draws vertical time grid lines)
    // Extend the time grid down through the master automation band so its lanes
    // get the same vertical bar/beat lines as the tracks (the band is a separate
    // viewport scroll-synced to the arrangement, so the grid stays aligned).
    auto gridArea = arrangementLayout.overlayArea;
    if (!arrangementLayout.masterAutomationContentArea.isEmpty())
        gridArea.setBottom(arrangementLayout.masterAutomationContentArea.getBottom());
    gridOverlay->setBounds(gridArea);
    gridOverlay->setScrollOffset(trackContentViewport->getViewPositionX());

    // Selection overlay (above grid)
    selectionOverlay->setBounds(arrangementLayout.overlayArea);

    // No trim needed — internal viewport scrollbars are hidden
    playheadComponent->setBounds(arrangementLayout.playheadArea);

    // Notify controller about viewport resize
    auto viewportWidth = timelineViewport->getWidth();
    auto viewportHeight = trackContentViewport->getHeight();
    if (viewportWidth > 0) {
        // Dispatch viewport resize event to controller
        timelineController->dispatch(ViewportResizedEvent{viewportWidth, viewportHeight});
        timeline->setViewportWidth(viewportWidth);

        // Set initial zoom to show configurable duration on first resize
        if (!initialZoomSet) {
            int availableWidth = viewportWidth - LayoutConfig::TIMELINE_LEFT_PADDING;

            if (availableWidth > 0) {
                auto& config = magda::Config::getInstance();
                int zoomViewBars = config.getDefaultZoomViewBars();
                // horizontalZoom is ppb: convert bars to beats
                const auto& st = timelineController->getState();
                double viewBeats = zoomViewBars * st.tempo.timeSignatureNumerator;
                double zoomForDefaultView =
                    (viewBeats > 0) ? static_cast<double>(availableWidth) / viewBeats : 10.0;

                // Ensure minimum zoom level for usability
                zoomForDefaultView = juce::jmax(zoomForDefaultView, 0.5);

                // Dispatch initial zoom via controller
                timelineController->dispatch(SetZoomCenteredEvent{zoomForDefaultView, 0.0});

                DBG("INITIAL ZOOM: showing " << zoomViewBars
                                             << " bars, availableWidth=" << availableWidth
                                             << ", zoomForDefaultView=" << zoomForDefaultView);

                initialZoomSet = true;
            }
        }
    }

    updateContentSizes();
}

void MainView::setHorizontalZoom(double zoomFactor) {
    // Dispatch to controller
    timelineController->dispatch(SetZoomEvent{zoomFactor});
}

void MainView::setVerticalZoom(double zoomFactor) {
    // Vertical zoom is still managed locally for now
    // TODO: Move to TimelineController when vertical zoom events are added
    verticalZoom = juce::jmax(0.5, juce::jmin(3.0, zoomFactor));
    updateContentSizes();
}

void MainView::scrollToPosition(double timePosition) {
    const auto& state = timelineController->getState();
    auto pixelPosition = state.timeDurationToPixels(timePosition);
    markerLaneViewport->setViewPosition(pixelPosition, 0);
    timelineViewport->setViewPosition(pixelPosition, 0);
    if (masterAutomationViewport)
        masterAutomationViewport->setViewPosition(pixelPosition, 0);
    trackContentViewport->setViewPosition(pixelPosition, trackContentViewport->getViewPositionY());
}

void MainView::scrollToTrack(int trackIndex) {
    if (trackIndex >= 0 && trackIndex < trackHeadersPanel->getNumTracks()) {
        int yPosition = trackHeadersPanel->getTrackYPosition(trackIndex);
        trackContentViewport->setViewPosition(trackContentViewport->getViewPositionX(), yPosition);
        trackHeadersViewport->setViewPosition(0, yPosition);
    }
}

void MainView::selectTrack(int trackIndex) {
    trackHeadersPanel->selectTrack(trackIndex);
    trackContentPanel->selectTrack(trackIndex);
}

void MainView::setTimelineLength(double lengthInSeconds) {
    // Dispatch to controller
    timelineController->dispatch(SetTimelineLengthEvent{lengthInSeconds});

    // Update child components directly (will eventually be handled by listener)
    timeline->setTimelineLength(lengthInSeconds);
    trackContentPanel->setTimelineLength(lengthInSeconds);
}

void MainView::setPlayheadPosition(double position) {
    // Dispatch to controller
    timelineController->dispatch(SetPlayheadPositionEvent{position});
}

void MainView::dispatchUserPlayheadPositionBeats(double positionBeats, bool bypassSnap) {
    const auto& state = timelineController->getState();
    double targetBeats = positionBeats;
    if (!bypassSnap)
        targetBeats = state.snapBeatsToGrid(targetBeats);
    timelineController->dispatch(SetPlayheadPositionBeatsEvent{targetBeats});
}

void MainView::toggleArrangementLock() {
    // Toggle via controller
    bool newLockedState = !timelineController->getState().display.arrangementLocked;
    timelineController->dispatch(SetArrangementLockedEvent{newLockedState});

    // Also update timeline component directly for now
    timeline->setArrangementLocked(newLockedState);
    timeline->repaint();
}

bool MainView::isArrangementLocked() const {
    return timelineController->getState().display.arrangementLocked;
}

void MainView::setLoopEnabled(bool enabled) {
    // If enabling loop and there's an active time selection, create loop from it
    if (enabled && timelineController->getState().selection.isActive()) {
        timelineController->dispatch(CreateLoopFromSelectionEvent{});
        return;
    }

    // Dispatch to controller
    timelineController->dispatch(SetLoopEnabledEvent{enabled});
}

void MainView::syncSnapState() {
    const auto& state = timelineController->getState();
    timeline->setSnapEnabled(state.display.snapEnabled);
}

// Add keyboard event handler for zoom reset shortcut
bool MainView::keyPressed(const juce::KeyPress& key) {
    // Check for Ctrl+0 (or Cmd+0 on Mac) to reset zoom to fit timeline
    if (key == juce::KeyPress('0', juce::ModifierKeys::commandModifier, 0)) {
        timelineController->dispatch(ResetZoomEvent{});
        return true;
    }

    // Check for F4 to toggle arrangement lock
    if (key == juce::KeyPress::F4Key) {
        toggleArrangementLock();
        return true;
    }

    // Check for 'L' to create loop from time selection or selected clip
    if (key == juce::KeyPress('l') || key == juce::KeyPress('L')) {
        if (timelineController->getState().selection.isActive()) {
            timelineController->dispatch(CreateLoopFromSelectionEvent{});
        } else {
            // Set loop from selected clip bounds
            ClipId selectedClipId = SelectionManager::getInstance().getSelectedClip();
            if (selectedClipId != INVALID_CLIP_ID) {
                const auto* clip = ClipManager::getInstance().getClip(selectedClipId);
                if (clip) {
                    const double bpm = timelineController->getState().tempo.bpm;
                    timelineController->dispatch(SetLoopRegionEvent{
                        timelineStartSeconds(*clip, bpm), timelineEndSeconds(*clip, bpm)});
                }
            }
        }
        return true;
    }

    // Note: 'S' key is now used for split in TrackContentPanel
    // Snap toggle is available via the toolbar button

    // Undo/redo is handled by the central UndoManager via MainWindowCommands

    // Check for Escape — exit link mode first, then clear selection
    if (key == juce::KeyPress::escapeKey) {
        if (LinkModeManager::getInstance().isInLinkMode()) {
            LinkModeManager::getInstance().exitAllLinkModes();
            return true;
        }
        timelineController->dispatch(ClearTimeSelectionEvent{});
        SelectionManager::getInstance().clearSelection();
        return true;
    }

    // ===== Clip Shortcuts =====

    auto& selectionManager = SelectionManager::getInstance();

    // Delete/Backspace: Delete selected clips
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        auto selectedClips = selectionManager.getSelectedClips();
        if (!selectedClips.empty()) {
            // Use compound operation for multiple deletes
            if (selectedClips.size() > 1) {
                UndoManager::getInstance().beginCompoundOperation("Delete Clips");
            }

            for (auto clipId : selectedClips) {
                auto cmd = std::make_unique<DeleteClipCommand>(clipId);
                UndoManager::getInstance().executeCommand(std::move(cmd));
            }

            if (selectedClips.size() > 1) {
                UndoManager::getInstance().endCompoundOperation();
            }

            selectionManager.clearSelection();
            DBG("CLIP: Deleted " << selectedClips.size() << " clip(s)");
            return true;
        }
    }

    // NOTE: Cmd+C, Cmd+V, Cmd+X, Cmd+D are now handled by ApplicationCommandManager in MainWindow
    // These old handlers have been removed to prevent double-handling

    // NOTE: Split (Cmd+E) and Trim (Cmd+E with time selection) are handled by
    // ApplicationCommandManager in MainWindow

    // Forward unhandled keys to parent for command manager processing
    if (auto* parent = getParentComponent()) {
        return parent->keyPressed(key);
    }

    return false;
}

void MainView::updateContentSizes() {
    // Use the controller's width calculation for every horizontal surface so
    // the ruler, track content, and scroll limits agree exactly.
    const auto& st = timelineController->getState();
    auto contentWidth = st.getContentWidth();

    // getTotalTracksHeight already applies verticalZoom per track, so do NOT
    // multiply again.
    // Floor to viewport height so both panels cover the full visible area —
    // needed for DnD (plugin drops hit the panel even below the last track).
    // Using the viewport widget height (constant for a given window size)
    // rather than the panel's own height avoids the monotonic-growth bug
    // that caused phantom scrollbars: when content shrinks the panel shrinks
    // back to the viewport floor, not its own stale height.
    int contentHeight = trackHeadersPanel->getTotalTracksHeight();
    int viewportFloor = trackContentViewport->getHeight();
    contentHeight = juce::jmax(contentHeight, viewportFloor);

    // Tell the content panel the minimum height so its own resized() (which
    // re-computes content size from zoom/timeline) doesn't shrink below the
    // viewport — needed for DnD to work in the empty region below tracks.
    trackContentPanel->setMinWidth(contentWidth);
    trackContentPanel->setMinHeight(viewportFloor);

    // Update timeline size with enhanced content width
    markerLane->setSize(contentWidth, getMarkerLaneHeight());
    timeline->setSize(contentWidth, LayoutConfig::getInstance().getTimelineBodyHeight());

    // Update track content and headers with same height
    trackContentPanel->setSize(contentWidth, contentHeight);
    trackContentPanel->setVerticalZoom(verticalZoom);
    trackHeadersPanel->setSize(trackHeaderWidth, contentHeight);
    trackHeadersPanel->setVerticalZoom(verticalZoom);

    // Keep the master automation band in step with the arrangement's horizontal
    // zoom/width so its curves rescale and scroll with the timeline (resized()
    // only runs on a relayout, not on every zoom/scroll change).
    if (masterAutomationContentPanel) {
        masterAutomationContentPanel->setPixelsPerBeat(horizontalZoom);
        masterAutomationContentPanel->setTimelineWidth(contentWidth);
    }

    // Update both zoom scroll bars
    updateVerticalZoomScrollBar();
}

void MainView::scrollBarMoved(juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart) {
    // Sync timeline viewport when track content viewport scrolls horizontally
    if (scrollBarThatHasMoved == &trackContentViewport->getHorizontalScrollBar()) {
        revealHorizontalArrangementScrollbar();

        int scrollX = static_cast<int>(newRangeStart);
        int scrollY = trackContentViewport->getViewPositionY();

        // Update controller state
        timelineController->dispatch(SetScrollPositionEvent{scrollX, scrollY});

        // Sync timeline viewport
        markerLaneViewport->setViewPosition(scrollX, 0);
        timelineViewport->setViewPosition(scrollX, 0);
        if (masterAutomationViewport)
            masterAutomationViewport->setViewPosition(scrollX, 0);

        // Update zoom scroll bar
        updateHorizontalZoomScrollBar();

        // Update grid overlay scroll offset and repaint overlays
        gridOverlay->setScrollOffset(scrollX);
        playheadComponent->repaint();
        selectionOverlay->repaint();
    }

    // Sync track headers viewport and update zoom scroll bar when scrolling vertically
    if (scrollBarThatHasMoved == &trackContentViewport->getVerticalScrollBar()) {
        // Sync track headers viewport to same vertical position. Ensure the
        // two panels are still the same height before syncing: when an
        // automation lane is added via setLaneVisible (e.g. from a device's
        // "Show Automation Lane") the content panel self-sizes to the new
        // total via its resized(), but the headers panel doesn't self-size —
        // it relies on updateContentSizes. If that hook hasn't run yet, the
        // headers viewport silently clamps scrollY to (shorter panel height
        // - viewport height) which presents as "scrolling only works after
        // you go all the way down and back up". updateContentSizes is
        // idempotent and cheap; calling it here costs nothing when sizes are
        // already in sync.
        if (trackContentPanel->getHeight() != trackHeadersPanel->getHeight())
            updateContentSizes();

        int scrollY = trackContentViewport->getViewPositionY();
        trackHeadersViewport->setViewPosition(0, scrollY);

        // Update zoom scroll bar (skip if we're handling a ZoomScrollBar change)
        if (!isUpdatingFromVerticalZoomScrollBar) {
            updateVerticalZoomScrollBar();
        }
    }
}

void MainView::syncTrackHeights() {
    // Sync track heights between headers and content panels
    int numTracks = trackHeadersPanel->getNumTracks();
    for (int i = 0; i < numTracks; ++i) {
        int headerHeight = trackHeadersPanel->getTrackHeight(i);
        int contentHeight = trackContentPanel->getTrackHeight(i);

        if (headerHeight != contentHeight) {
            // Sync to the header height (headers are the source of truth)
            trackContentPanel->setTrackHeight(i, headerHeight);
        }
    }
}

void MainView::setupTrackSynchronization() {
    // Set up callbacks to keep track headers and content in sync
    trackHeadersPanel->onTrackHeightChanged = [this](int trackIndex, int newHeight) {
        trackContentPanel->setTrackHeight(trackIndex, newHeight);
        updateContentSizes();
    };

    // Any layout change in the headers panel (automation lanes added/
    // removed/resized) must re-size BOTH panels so the outer viewports stay
    // in scroll sync. Without this, opening a lane via setLaneVisible leaves
    // TrackContentPanel taller than TrackHeadersPanel — the content viewport
    // can scroll past what the headers viewport can, and the headers stay
    // pinned at y=0 until the user scrolls all the way down and back.
    trackHeadersPanel->onLayoutChanged = [this]() { updateContentSizes(); };

    trackHeadersPanel->onTrackSelected = [this](int trackIndex) {
        if (!isUpdatingTrackSelection) {
            isUpdatingTrackSelection = true;
            trackContentPanel->selectTrack(trackIndex);
            isUpdatingTrackSelection = false;
        }
    };

    // Wire up automation lane visibility toggle
    trackHeadersPanel->onShowAutomationLane = [this](TrackId trackId, AutomationLaneId laneId) {
        trackContentPanel->showAutomationLane(trackId, laneId);
        updateContentSizes();
    };

    trackContentPanel->onTrackSelected = [this](int trackIndex) {
        if (!isUpdatingTrackSelection) {
            isUpdatingTrackSelection = true;
            trackHeadersPanel->selectTrack(trackIndex);
            isUpdatingTrackSelection = false;
        }
    };
}

void MainView::updateHorizontalZoomScrollBar() {
    if (horizontalZoom <= 0)
        return;

    const auto& st = timelineController->getState();
    double totalBeats = st.timelineLengthBeats;
    if (totalBeats <= 0)
        return;

    int viewportWidth = trackContentViewport->getWidth();
    int scrollX = trackContentViewport->getViewPositionX();

    // Calculate visible range as fraction of total timeline
    double visibleBeats =
        (horizontalZoom > 0) ? static_cast<double>(viewportWidth) / horizontalZoom : 0;
    double scrollBeats = (horizontalZoom > 0) ? static_cast<double>(scrollX) / horizontalZoom : 0;

    double visibleStart = scrollBeats / totalBeats;
    double visibleEnd = (scrollBeats + visibleBeats) / totalBeats;

    // Clamp to valid range
    visibleStart = juce::jlimit(0.0, 1.0, visibleStart);
    visibleEnd = juce::jlimit(0.0, 1.0, visibleEnd);

    horizontalZoomScrollBar->setVisibleRange(visibleStart, visibleEnd);
}

void MainView::updateVerticalZoomScrollBar() {
    int totalContentHeight = trackHeadersPanel->getTotalTracksHeight();
    if (totalContentHeight <= 0)
        return;

    int viewportHeight = trackContentViewport->getHeight();
    int scrollY = trackContentViewport->getViewPositionY();

    // Calculate rangeHeight from zoom using inverse of: zoom = 0.5 + rangeHeight * 2.5
    // rangeHeight = (zoom - 0.5) / 2.5
    double rangeHeight = (verticalZoom - 0.5) / 2.5;
    rangeHeight = juce::jlimit(0.01, 1.0, rangeHeight);

    // getTotalTracksHeight already includes verticalZoom.
    int scaledContentHeight = totalContentHeight;

    // Calculate scroll position as fraction
    double scrollFraction =
        (scaledContentHeight > viewportHeight)
            ? static_cast<double>(scrollY) / (scaledContentHeight - viewportHeight)
            : 0.0;
    scrollFraction = juce::jlimit(0.0, 1.0, scrollFraction);

    // Position the thumb based on scroll position, keeping rangeHeight constant
    double maxStart = 1.0 - rangeHeight;
    double visibleStart = scrollFraction * maxStart;
    double visibleEnd = visibleStart + rangeHeight;

    // Clamp to valid range
    visibleStart = juce::jlimit(0.0, 1.0, visibleStart);
    visibleEnd = juce::jlimit(0.0, 1.0, visibleEnd);

    verticalZoomScrollBar->setVisibleRange(visibleStart, visibleEnd);
}

void MainView::revealHorizontalArrangementScrollbar() {
    if (isUpdatingArrangementScrollbarLayout)
        return;
    if (arrangementScrollbarResizeSuppressFrames > 0)
        return;
    if (!Config::getInstance().getArrangementScrollbarsAutoHide())
        return;

    horizontalScrollbarRevealFrames =
        juce::jmax(horizontalScrollbarRevealFrames, ARRANGEMENT_SCROLLBAR_REVEAL_HOLD_FRAMES);
    horizontalScrollbarRevealProgress =
        juce::jmax(horizontalScrollbarRevealProgress, ARRANGEMENT_SCROLLBAR_FADE_IN_STEP);
    horizontalZoomScrollBar->setAlpha(horizontalScrollbarRevealProgress);
    horizontalZoomScrollBar->setVisible(true);
    repaint(horizontalZoomScrollBar->getBounds().expanded(0, 2));
}

void MainView::revealVerticalArrangementScrollbar() {
    if (arrangementScrollbarResizeSuppressFrames > 0)
        return;
    if (!Config::getInstance().getArrangementScrollbarsAutoHide())
        return;

    verticalScrollbarRevealFrames =
        juce::jmax(verticalScrollbarRevealFrames, ARRANGEMENT_SCROLLBAR_REVEAL_HOLD_FRAMES);
    verticalScrollbarRevealProgress =
        juce::jmax(verticalScrollbarRevealProgress, ARRANGEMENT_SCROLLBAR_FADE_IN_STEP);
    verticalZoomScrollBar->setAlpha(verticalScrollbarRevealProgress);
    verticalZoomScrollBar->setVisible(true);
}

void MainView::updateArrangementScrollbarHover(const juce::MouseEvent& event) {
    auto position = event.getEventRelativeTo(this).getPosition();
    isHorizontalScrollbarHovered = horizontalScrollbarHitArea.contains(position);
    isVerticalScrollbarHovered = verticalScrollbarHitArea.contains(position);
}

void MainView::updateArrangementScrollbarVisibility() {
    if (!horizontalZoomScrollBar || !verticalZoomScrollBar)
        return;

    if (arrangementScrollbarResizeSuppressFrames > 0)
        --arrangementScrollbarResizeSuppressFrames;

    // Classic mode (auto-hide disabled): pin both scrollbars fully visible and
    // skip the entire fade/hover state machine. The first tick after the user
    // toggles the preference will snap progress to 1.0 and re-run layout so the
    // reservation reaches its full width/height.
    if (!Config::getInstance().getArrangementScrollbarsAutoHide()) {
        if (horizontalScrollbarRevealProgress < 1.0f || verticalScrollbarRevealProgress < 1.0f) {
            horizontalScrollbarRevealProgress = 1.0f;
            verticalScrollbarRevealProgress = 1.0f;
            juce::ScopedValueSetter<bool> scrollbarLayoutUpdate(
                isUpdatingArrangementScrollbarLayout, true);
            resized();
            repaint();
        }
        return;
    }

    auto mousePosition = getLocalPoint(nullptr, juce::Desktop::getInstance().getMousePosition());

    // Edge dwell: require the cursor to rest in the narrow hit strip for a few
    // frames before counting as a hover, so quick grazes don't trigger a fade
    // cycle. Once the bar is already visible the dwell is bypassed — moving
    // the cursor over the visible bar should keep it open immediately.
    const bool inVerticalHitStrip = verticalScrollbarHitArea.contains(mousePosition);
    const bool inHorizontalHitStrip = horizontalScrollbarHitArea.contains(mousePosition);

    verticalHoverDwellFrames = inVerticalHitStrip ? (verticalHoverDwellFrames + 1) : 0;
    horizontalHoverDwellFrames = inHorizontalHitStrip ? (horizontalHoverDwellFrames + 1) : 0;

    const bool overVisibleVerticalBar = verticalZoomScrollBar->isVisible() &&
                                        verticalZoomScrollBar->getBounds().contains(mousePosition);
    const bool overVisibleHorizontalBar =
        horizontalZoomScrollBar->isVisible() &&
        horizontalZoomScrollBar->getBounds().contains(mousePosition);

    isVerticalScrollbarHovered =
        overVisibleVerticalBar ||
        (inVerticalHitStrip &&
         verticalHoverDwellFrames >= ARRANGEMENT_SCROLLBAR_HOVER_DWELL_FRAMES);
    isHorizontalScrollbarHovered =
        overVisibleHorizontalBar ||
        (inHorizontalHitStrip &&
         horizontalHoverDwellFrames >= ARRANGEMENT_SCROLLBAR_HOVER_DWELL_FRAMES);

    // While the user is hovering or dragging the scrollbar, keep the hold-frames
    // counter pinned at the full hold window. When the trigger ends (mouse
    // leaves, drag stops), the counter decays — small re-entries within the
    // hold window refresh it back to full instead of restarting the fade.
    auto refreshHold = [](bool active, int& frames) {
        if (active) {
            frames = ARRANGEMENT_SCROLLBAR_REVEAL_HOLD_FRAMES;
        } else if (frames > 0) {
            --frames;
        }
    };
    refreshHold(isHorizontalScrollbarHovered || horizontalZoomScrollBar->isDragging() ||
                    isZoomActive,
                horizontalScrollbarRevealFrames);
    refreshHold(isVerticalScrollbarHovered || verticalZoomScrollBar->isDragging(),
                verticalScrollbarRevealFrames);

    const bool targetHorizontalVisible = horizontalScrollbarRevealFrames > 0;
    const bool targetVerticalVisible = verticalScrollbarRevealFrames > 0;

    auto nextProgress = [](float current, bool targetVisible, float fadeOutStep) {
        if (targetVisible) {
            return juce::jmin(1.0f, current + MainView::ARRANGEMENT_SCROLLBAR_FADE_IN_STEP);
        }

        return juce::jmax(0.0f, current - fadeOutStep);
    };

    auto nextHorizontalProgress =
        nextProgress(horizontalScrollbarRevealProgress, targetHorizontalVisible,
                     HORIZONTAL_SCROLLBAR_FADE_OUT_STEP);
    auto nextVerticalProgress = nextProgress(verticalScrollbarRevealProgress, targetVerticalVisible,
                                             VERTICAL_SCROLLBAR_FADE_OUT_STEP);

    if (nextHorizontalProgress == horizontalScrollbarRevealProgress &&
        nextVerticalProgress == verticalScrollbarRevealProgress)
        return;

    horizontalScrollbarRevealProgress = nextHorizontalProgress;
    verticalScrollbarRevealProgress = nextVerticalProgress;

    // Vertical reveal progress feeds the reserved width in computeArrangementLayout,
    // so any change has to re-run layout for headers to slide smoothly along.
    // resized() also handles setAlpha/setVisible on the scrollbars.
    {
        juce::ScopedValueSetter<bool> scrollbarLayoutUpdate(isUpdatingArrangementScrollbarLayout,
                                                            true);
        resized();
    }
    repaint();
}

void MainView::setupTimelineCallbacks() {
    // Set up timeline zoom callback - dispatches to TimelineController
    timeline->onZoomChanged = [this](double newZoom, double anchorBeats, int anchorContentX) {
        revealHorizontalArrangementScrollbar();

        // Set crosshair cursor during zoom operations
        setMouseCursor(juce::MouseCursor::CrosshairCursor);

        // On first zoom callback, capture the viewport-relative position
        if (!isZoomActive) {
            isZoomActive = true;
            int currentScrollX = trackContentViewport->getViewPositionX();
            zoomAnchorViewportX = anchorContentX - currentScrollX;
        }

        // Dispatch to controller with anchor information
        timelineController->dispatch(
            SetZoomAnchoredBeatsEvent{newZoom, anchorBeats, zoomAnchorViewportX});
    };

    // Set up timeline zoom end callback
    timeline->onZoomEnd = [this]() {
        // Reset zoom anchor tracking for next zoom operation
        isZoomActive = false;

        // Reset cursor to normal when zoom ends
        setMouseCursor(juce::MouseCursor::NormalCursor);
    };

    // Set up zoom-to-fit callback (e.g., double-click to fit loop region)
    timeline->onZoomToFitBeatsRequested = [this](double startBeats, double endBeats) {
        if (endBeats <= startBeats)
            return;

        revealHorizontalArrangementScrollbar();

        // Dispatch to controller
        timelineController->dispatch(ZoomToFitBeatsEvent{startBeats, endBeats, 0.05});
    };
}

// PlayheadComponent implementation
MainView::PlayheadComponent::PlayheadComponent(MainView& owner) : owner(owner) {
    setInterceptsMouseClicks(false, true);  // Only intercept clicks when hitTest returns true
}

MainView::PlayheadComponent::~PlayheadComponent() = default;

void MainView::PlayheadComponent::paint(juce::Graphics& g) {
    const auto& state = owner.timelineController->getState();
    int scrollOffset = owner.trackContentViewport->getViewPositionX();

    // Get positions from state
    double editPos = state.playhead.editPosition;
    double playbackPos = state.playhead.playbackPosition;
    double editBeats = state.playhead.editPositionBeats;
    double playBeats = state.playhead.playbackPositionBeats;
    bool isPlaying = state.playhead.isPlaying;

    // Calculate edit cursor position in pixels (triangle position)
    // Use std::round to match TimeRuler so cursors align with the ruler ticks.
    int editX = static_cast<int>(std::round(editBeats * owner.horizontalZoom)) +
                LayoutConfig::TIMELINE_LEFT_PADDING;
    editX -= scrollOffset;

    // Calculate play cursor position in pixels (vertical line position)
    int playX = static_cast<int>(std::round(playBeats * owner.horizontalZoom)) +
                LayoutConfig::TIMELINE_LEFT_PADDING;
    playX -= scrollOffset;

    // Draw edit cursor (triangle) - always visible
    if (editPos >= 0 && editPos <= owner.timelineLength && editX >= 0 && editX < getWidth()) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        // Fill the playhead row: top edge at y0, tip at the row bottom.
        const float ph = static_cast<float>(LayoutConfig::getInstance().playheadRowHeight);
        juce::Path triangle;
        triangle.addTriangle(editX - 6, 0.0f, editX + 6, 0.0f, editX, ph);
        g.fillPath(triangle);
    }

    // Draw play cursor (vertical line) - only during playback when position differs from edit
    if (isPlaying && playbackPos >= 0 && playbackPos <= owner.timelineLength && playX >= 0 &&
        playX < getWidth()) {
        // Draw thin vertical line extending the full track area, starting at the
        // top of the track content (just below the playhead/triangle row).
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        const float lineTop = static_cast<float>(LayoutConfig::getInstance().playheadRowHeight);
        g.drawLine(static_cast<float>(playX), lineTop, static_cast<float>(playX),
                   static_cast<float>(getHeight()), 1.5f);
    }
}

void MainView::PlayheadComponent::setPlayheadPosition(double position) {
    playheadPosition = position;
    repaint();
}

bool MainView::PlayheadComponent::hitTest([[maybe_unused]] int x, [[maybe_unused]] int y) {
    // Don't intercept mouse events - playhead is display-only (just a triangle)
    // Clicks pass through to timeline/tracks for time selection
    return false;
}

void MainView::PlayheadComponent::mouseDown(const juce::MouseEvent& e) {
    // Get edit position from controller state
    const auto& state = owner.timelineController->getState();
    double editPos = state.playhead.editPosition;
    double editBeats = state.playhead.editPositionBeats;

    // Calculate edit cursor (triangle) position in pixels
    int editX = static_cast<int>(std::round(editBeats * owner.horizontalZoom)) +
                LayoutConfig::TIMELINE_LEFT_PADDING;

    // Adjust for horizontal scroll offset
    int scrollOffset = owner.trackContentViewport->getViewPositionX();
    editX -= scrollOffset;

    // Check if click is near the edit cursor triangle (within 10 pixels)
    if (std::abs(e.x - editX) <= 10) {
        isDragging = true;
        dragStartX = e.x;
        dragStartPosition = editPos;
    }
}

void MainView::PlayheadComponent::mouseDrag(const juce::MouseEvent& e) {
    if (isDragging) {
        // Calculate the change in position
        int deltaX = e.x - dragStartX;

        // Convert pixel change to time change
        // horizontalZoom is ppb: deltaX / ppb = deltaBeats, then convert to seconds
        const auto& state = owner.timelineController->getState();
        double deltaBeats = deltaX / owner.horizontalZoom;
        double deltaTime = state.beatsToSeconds(deltaBeats);

        // Calculate new playhead position
        double newPosition = dragStartPosition + deltaTime;

        // Clamp to valid range
        newPosition = juce::jlimit(0.0, owner.timelineLength, newPosition);

        // Update playhead position
        owner.setPlayheadPosition(newPosition);

        // Notify timeline of position change
        owner.timeline->setPlayheadPosition(newPosition);
    }
}

void MainView::PlayheadComponent::mouseUp([[maybe_unused]] const juce::MouseEvent& event) {
    isDragging = false;
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void MainView::PlayheadComponent::mouseMove(const juce::MouseEvent& event) {
    // Get edit position from controller state
    const auto& state = owner.timelineController->getState();
    double editBeats = state.playhead.editPositionBeats;

    // Calculate edit cursor (triangle) position in pixels
    int editX = static_cast<int>(std::round(editBeats * owner.horizontalZoom)) +
                LayoutConfig::TIMELINE_LEFT_PADDING;

    // Adjust for horizontal scroll offset
    int scrollOffset = owner.trackContentViewport->getViewPositionX();
    editX -= scrollOffset;

    // Change cursor when over edit cursor triangle
    if (std::abs(event.x - editX) <= 10) {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

void MainView::mouseDown(const juce::MouseEvent& event) {
    // Always grab keyboard focus so shortcuts work
    grabKeyboardFocus();
    updateArrangementScrollbarHover(event);

    if (getResizeHandleArea().contains(event.getPosition())) {
        isResizingHeaders = true;
        lastMouseX = event.x;
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        return;
    }

    if (getMasterResizeHandleArea().contains(event.getPosition())) {
        isResizingMasterStrip = true;
        resizeStartY = event.y;
        resizeStartHeight = masterStripHeight;
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        return;
    }

    // Removed timeline zoom handling - let timeline component handle its own zoom
    // The timeline component now handles zoom gestures in its lower half
}

void MainView::mouseDrag(const juce::MouseEvent& event) {
    updateArrangementScrollbarHover(event);

    if (isResizingHeaders) {
        int deltaX = event.x - lastMouseX;
        if (Config::getInstance().getScrollbarOnLeft())
            deltaX = -deltaX;

        auto& layout = LayoutConfig::getInstance();
        int newWidth = juce::jlimit(layout.minTrackHeaderWidth, layout.maxTrackHeaderWidth,
                                    trackHeaderWidth + deltaX);

        if (newWidth != trackHeaderWidth) {
            trackHeaderWidth = newWidth;
            resized();
            repaint();
        }

        lastMouseX = event.x;  // Update for next drag event
    }

    if (isResizingMasterStrip) {
        // Dragging up (negative deltaY) should increase height
        int deltaY = resizeStartY - event.y;
        int newHeight = juce::jlimit(MIN_MASTER_STRIP_HEIGHT, MAX_MASTER_STRIP_HEIGHT,
                                     resizeStartHeight + deltaY);

        if (newHeight != masterStripHeight) {
            masterStripHeight = newHeight;
            resized();
            repaint();
        }
    }
}

void MainView::mouseUp(const juce::MouseEvent& event) {
    updateArrangementScrollbarHover(event);

    if (isResizingHeaders) {
        isResizingHeaders = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    if (isResizingMasterStrip) {
        isResizingMasterStrip = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    // Removed zoom handling - timeline component handles its own zoom
}

void MainView::mouseMove(const juce::MouseEvent& event) {
    updateArrangementScrollbarHover(event);

    auto handleArea = getResizeHandleArea();
    auto masterHandleArea = getMasterResizeHandleArea();

    if (handleArea.contains(event.getPosition())) {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        repaint(handleArea);  // Repaint to show hover effect
    } else if (masterHandleArea.contains(event.getPosition())) {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        repaint(masterHandleArea);  // Repaint to show hover effect
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint(handleArea);        // Repaint to remove hover effect
        repaint(masterHandleArea);  // Repaint to remove hover effect
    }
}

void MainView::mouseExit(const juce::MouseEvent& event) {
    auto position = event.getEventRelativeTo(this).getPosition();
    if (!getLocalBounds().contains(position)) {
        isHorizontalScrollbarHovered = false;
        isVerticalScrollbarHovered = false;
    }

    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint(getResizeHandleArea());        // Remove hover effect
    repaint(getMasterResizeHandleArea());  // Remove hover effect
}

// Resize handle helper methods
juce::Rectangle<int> MainView::getResizeHandleArea() const {
    // Position the resize handle in the padding space between headers and content
    // Starts below the corner toolbar / timeline area
    auto& layout = LayoutConfig::getInstance();
    auto arrangementLayout = computeArrangementLayout();
    int top = getTimelineHeight();
    int x = arrangementLayout.swapped
                ? arrangementLayout.trackHeadersArea.getX() - layout.componentSpacing
                : arrangementLayout.trackHeadersArea.getRight();
    return juce::Rectangle<int>(x, top, layout.componentSpacing, getHeight() - top);
}

void MainView::paintResizeHandle(juce::Graphics& g) {
    auto handleArea = getResizeHandleArea();

    // Check if mouse is over the handle for hover effect
    auto mousePos = getMouseXYRelative();
    bool isHovered = handleArea.contains(mousePos);

    // Draw subtle resize handle with hover effect
    if (isHovered || isResizingHeaders) {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).brighter(0.3f));
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    }

    // Draw a thinner visual line in the center
    int centerX = handleArea.getCentreX();
    g.fillRect(centerX - 1, handleArea.getY(), 2, handleArea.getHeight());

    // Draw a subtle highlight line when hovered or resizing
    if (isHovered || isResizingHeaders) {
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).withAlpha(0.4f));
        g.fillRect(centerX, handleArea.getY() + 4, 1, handleArea.getHeight() - 8);
    }
}

juce::Rectangle<int> MainView::getMasterResizeHandleArea() const {
    // Return empty area if master is not visible
    if (!masterVisible_) {
        return {};
    }

    // Position the resize handle in the gap between track content and master strip.
    // The horizontal scrollbar row is a fixed-height reservation, so the handle
    // stays put while the bar fades in/out within that slot.
    int effectiveAuxHeight = auxVisible_ ? auxSectionHeight : 0;
    int horizontalScrollbarHeight = ARRANGEMENT_SCROLLBAR_SIZE;
    int resizeHandleY = getHeight() - horizontalScrollbarHeight - masterStripHeight -
                        MASTER_RESIZE_HANDLE_HEIGHT - effectiveAuxHeight;
    return juce::Rectangle<int>(0, resizeHandleY, getWidth(), MASTER_RESIZE_HANDLE_HEIGHT);
}

void MainView::paintMasterResizeHandle(juce::Graphics& g) {
    auto handleArea = getMasterResizeHandleArea();

    // Check if mouse is over the handle for hover effect
    auto mousePos = getMouseXYRelative();
    bool isHovered = handleArea.contains(mousePos);

    // Draw subtle resize handle with hover effect
    if (isHovered || isResizingMasterStrip) {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).brighter(0.3f));
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    }

    // Draw a horizontal line
    int centerY = handleArea.getCentreY();
    g.fillRect(handleArea.getX(), centerY - 1, handleArea.getWidth(), 2);

    // Draw a subtle highlight line when hovered or resizing
    if (isHovered || isResizingMasterStrip) {
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).withAlpha(0.4f));
        g.fillRect(handleArea.getX() + 4, centerY, handleArea.getWidth() - 8, 1);
    }
}

void MainView::resetZoomToFitTimeline() {
    // Dispatch to controller
    timelineController->dispatch(ResetZoomEvent{});

    DBG("ZOOM RESET: timelineLength=" << timelineController->getState().timelineLength << ", zoom="
                                      << timelineController->getState().zoom.horizontalZoom);
}

void MainView::zoomToSelection() {
    const auto& sel = timelineController->getState().selection;
    if (sel.isActive()) {
        timelineController->dispatch(ZoomToFitBeatsEvent{sel.startBeats, sel.endBeats, 0.05});
    }
}

void MainView::setAllTrackHeights(int height) {
    verticalZoom = 1.0;
    trackHeadersPanel->setVerticalZoom(verticalZoom);
    trackContentPanel->setVerticalZoom(verticalZoom);

    int numTracks = trackHeadersPanel->getNumTracks();
    for (int i = 0; i < numTracks; ++i) {
        trackHeadersPanel->setTrackHeight(i, height);
        trackContentPanel->setTrackHeight(i, height);
    }
    updateContentSizes();
    updateVerticalZoomScrollBar();
}

void MainView::clearTimeSelection() {
    // Dispatch to controller
    timelineController->dispatch(ClearTimeSelectionEvent{});
}

void MainView::createLoopFromSelection() {
    // Dispatch to controller - it handles clearing selection after creating loop
    timelineController->dispatch(CreateLoopFromSelectionEvent{});

    const auto& state = timelineController->getState();
    if (state.loop.isValid()) {
        DBG("LOOP CREATED: " << state.loop.startTime << "s - " << state.loop.endTime << "s");
    }
}

void MainView::setupSelectionCallbacks() {
    // Set up snap to grid callback for track content panel
    // This uses the controller's state for snapping
    trackContentPanel->snapTimeToGrid = [this](double time) {
        return timelineController->getState().snapTimeToGrid(time);
    };
    trackContentPanel->snapBeatsToGrid = [this](double beats) {
        return timelineController->getState().snapBeatsToGrid(beats);
    };
    trackContentPanel->getGridSpacingBeats = [this]() -> double {
        return timelineController->getState().getSnapBeatFraction();
    };
    // Master automation band (tempo, master volume) snaps to the same grid.
    if (masterAutomationContentPanel) {
        masterAutomationContentPanel->snapBeatToGrid = [this](double beats) {
            return timelineController->getState().snapBeatsToGrid(beats);
        };
        masterAutomationContentPanel->getGridSpacingBeats = [this]() -> double {
            return timelineController->getState().getSnapBeatFraction();
        };
    }

    // Set up render callbacks (bubble up to MainWindow)
    trackContentPanel->onClipRenderRequested = [this](ClipId id) {
        if (onClipRenderRequested)
            onClipRenderRequested(id);
    };
    trackContentPanel->onRenderTimeSelectionRequested = [this]() {
        if (onRenderTimeSelectionRequested)
            onRenderTimeSelectionRequested();
    };
    trackContentPanel->onBounceInPlaceRequested = [this](ClipId id) {
        if (onBounceInPlaceRequested)
            onBounceInPlaceRequested(id);
    };
    trackContentPanel->onBounceToNewTrackRequested = [this](ClipId id) {
        if (onBounceToNewTrackRequested)
            onBounceToNewTrackRequested(id);
    };
    // Keep the transparent grid overlay clean while a clip is being dragged —
    // the incremental repaint from the clip's setBounds doesn't reach this
    // sibling, leaving a trail of grid lines over the vacated area.
    trackContentPanel->onClipDragOverlayRepaint = [this]() {
        if (gridOverlay)
            gridOverlay->repaint();
    };

    // Set up time selection callback from track content panel
    trackContentPanel->onTimeSelectionBeatsChanged = [this](double startBeats, double endBeats,
                                                            std::set<int> trackIndices) {
        if (startBeats < 0 || endBeats < 0) {
            timelineController->dispatch(ClearTimeSelectionEvent{});
        } else {
            timelineController->dispatch(
                SetTimeSelectionBeatsEvent{startBeats, endBeats, trackIndices});
            // Move playhead to follow the left side of selection
            timelineController->dispatch(SetPlayheadPositionBeatsEvent{startBeats});
        }
    };

    trackContentPanel->onMixedTimeSelectionBeatsChanged =
        [this](double startBeats, double endBeats, std::set<int> trackIndices,
               std::set<AutomationLaneId> laneIds) {
            if (startBeats < 0 || endBeats < 0) {
                timelineController->dispatch(ClearTimeSelectionEvent{});
            } else {
                timelineController->dispatch(SetTimeSelectionBeatsEvent{
                    startBeats, endBeats, trackIndices, false, std::move(laneIds)});
                timelineController->dispatch(SetPlayheadPositionBeatsEvent{startBeats});
            }
        };

    trackContentPanel->onAutomationTimeSelectionBeatsChanged =
        [this](double startBeats, double endBeats, std::set<int> trackIndices,
               std::set<AutomationLaneId> laneIds) {
            if (startBeats < 0 || endBeats < 0) {
                timelineController->dispatch(ClearTimeSelectionEvent{});
            } else {
                timelineController->dispatch(SetTimeSelectionBeatsEvent{
                    startBeats, endBeats, trackIndices, true, std::move(laneIds)});
                timelineController->dispatch(SetPlayheadPositionBeatsEvent{startBeats});
            }
        };

    // Set up playhead position callback from track content panel (click to set playhead)
    trackContentPanel->onPlayheadPositionBeatsChanged = [this](double positionBeats) {
        dispatchUserPlayheadPositionBeats(positionBeats, false);
    };

    // Set up loop region callback from timeline
    timeline->onLoopRegionBeatsChanged = [this](double startBeats, double endBeats) {
        // Prevent recursive updates - only dispatch if user changed it, not programmatic update
        if (isUpdatingLoopRegion) {
            return;
        }

        if (startBeats < 0 || endBeats < 0) {
            timelineController->dispatch(ClearLoopRegionEvent{});
        } else {
            timelineController->dispatch(SetLoopRegionBeatsEvent{startBeats, endBeats});
        }
    };
}

// SelectionOverlayComponent implementation
MainView::SelectionOverlayComponent::SelectionOverlayComponent(MainView& owner) : owner(owner) {
    setInterceptsMouseClicks(false, false);  // Transparent to all mouse events
}

MainView::SelectionOverlayComponent::~SelectionOverlayComponent() = default;

void MainView::SelectionOverlayComponent::paint(juce::Graphics& g) {
    // NOTE: Recording region drawing removed — now handled by
    // TrackContentPanel::paintRecordingPreviews() with real-time MIDI notes.
    drawTimeSelection(g);
    drawLoopRegion(g);
}

void MainView::SelectionOverlayComponent::drawTimeSelection(juce::Graphics& g) {
    const auto& state = owner.timelineController->getState();
    if (!state.selection.isVisuallyActive()) {
        return;
    }

    // Calculate pixel positions from authoritative beat state.
    double startBeats = state.selection.startBeats;
    double endBeats = state.selection.endBeats;
    // Add LEFT_PADDING to align with timeline markers; round to match TimeRuler.
    int startX = static_cast<int>(std::round(startBeats * state.zoom.horizontalZoom)) +
                 LayoutConfig::TIMELINE_LEFT_PADDING;
    int endX = static_cast<int>(std::round(endBeats * state.zoom.horizontalZoom)) +
               LayoutConfig::TIMELINE_LEFT_PADDING;

    // Adjust for scroll offset
    int scrollOffset = owner.trackContentViewport->getViewPositionX();
    startX -= scrollOffset;
    endX -= scrollOffset;

    // Skip if out of view horizontally
    if (endX < 0 || startX > getWidth()) {
        return;
    }

    // Clamp to visible area horizontally
    startX = juce::jmax(0, startX);
    endX = juce::jmin(getWidth(), endX);

    const int selectionWidth = endX - startX;
    const auto edgeColour = DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.8f);

    if (state.selection.automationOnly && !state.selection.automationLaneIds.empty()) {
        const int scrollY = owner.trackContentViewport->getViewPositionY();
        for (auto laneId : state.selection.automationLaneIds) {
            juce::Rectangle<int> laneBounds;
            if (!owner.trackContentPanel->getAutomationLaneBounds(laneId, laneBounds))
                continue;

            const int drawY = laneBounds.getY() - scrollY;
            const int drawBottom = drawY + laneBounds.getHeight();
            if (drawBottom < 0 || drawY > getHeight())
                continue;

            const int clippedY = juce::jmax(0, drawY);
            const int clippedBottom = juce::jmin(getHeight(), drawBottom);
            const int drawHeight = clippedBottom - clippedY;
            if (drawHeight <= 0)
                continue;

            paintTimeSelectionBand(g, {startX, clippedY, selectionWidth, drawHeight});

            g.setColour(edgeColour);
            g.drawLine(static_cast<float>(startX), static_cast<float>(clippedY),
                       static_cast<float>(startX), static_cast<float>(clippedBottom), 2.0f);
            g.drawLine(static_cast<float>(endX), static_cast<float>(clippedY),
                       static_cast<float>(endX), static_cast<float>(clippedBottom), 2.0f);
        }
        return;
    }

    int scrollY = owner.trackContentViewport->getViewPositionY();
    int numTracks = owner.trackContentPanel->getNumTracks();

    // Clip time selections only paint clip rows. Automation-only selections
    // have their own lane-scoped branch above.
    for (int trackIndex = 0; trackIndex < numTracks; ++trackIndex) {
        if (state.selection.includesTrack(trackIndex)) {
            int trackY = owner.trackContentPanel->getTrackYPosition(trackIndex) - scrollY;
            int trackHeight = owner.trackContentPanel->getTrackHeight(trackIndex);
            trackHeight = static_cast<int>(trackHeight * owner.verticalZoom);

            if (trackY + trackHeight < 0 || trackY > getHeight()) {
                continue;
            }

            int drawY = juce::jmax(0, trackY);
            int drawBottom = juce::jmin(getHeight(), trackY + trackHeight);
            int drawHeight = drawBottom - drawY;

            if (drawHeight > 0) {
                paintTimeSelectionBand(g, {startX, drawY, selectionWidth, drawHeight});

                g.setColour(edgeColour);
                g.drawLine(static_cast<float>(startX), static_cast<float>(drawY),
                           static_cast<float>(startX), static_cast<float>(drawBottom), 2.0f);
                g.drawLine(static_cast<float>(endX), static_cast<float>(drawY),
                           static_cast<float>(endX), static_cast<float>(drawBottom), 2.0f);
            }
        }
    }

    for (auto laneId : state.selection.automationLaneIds) {
        juce::Rectangle<int> laneBounds;
        if (!owner.trackContentPanel->getAutomationLaneBounds(laneId, laneBounds))
            continue;

        const int drawY = laneBounds.getY() - scrollY;
        const int drawBottom = drawY + laneBounds.getHeight();
        if (drawBottom < 0 || drawY > getHeight())
            continue;

        const int clippedY = juce::jmax(0, drawY);
        const int clippedBottom = juce::jmin(getHeight(), drawBottom);
        const int drawHeight = clippedBottom - clippedY;
        if (drawHeight <= 0)
            continue;

        paintTimeSelectionBand(g, {startX, clippedY, selectionWidth, drawHeight});

        g.setColour(edgeColour);
        g.drawLine(static_cast<float>(startX), static_cast<float>(clippedY),
                   static_cast<float>(startX), static_cast<float>(clippedBottom), 2.0f);
        g.drawLine(static_cast<float>(endX), static_cast<float>(clippedY), static_cast<float>(endX),
                   static_cast<float>(clippedBottom), 2.0f);
    }
}

void MainView::SelectionOverlayComponent::paintTimeSelectionBand(juce::Graphics& g,
                                                                 juce::Rectangle<int> bandRect) {
    if (bandRect.isEmpty() || !owner.trackContentPanel || !owner.trackContentViewport)
        return;

    const int scrollX = owner.trackContentViewport->getViewPositionX();
    const int scrollY = owner.trackContentViewport->getViewPositionY();

    auto panelRect = bandRect.translated(scrollX, scrollY)
                         .getIntersection(owner.trackContentPanel->getLocalBounds());
    if (panelRect.isEmpty())
        return;

    const auto invertPixel = [](juce::Colour px) {
        const auto invertAndContrast = [](int channel) {
            constexpr float contrast = 1.15f;
            const float inverted = static_cast<float>(255 - channel);
            return static_cast<juce::uint8>(juce::jlimit(
                0, 255, static_cast<int>(std::round((inverted - 128.0f) * contrast + 128.0f))));
        };

        auto inverted =
            juce::Colour::fromRGB(invertAndContrast(px.getRed()), invertAndContrast(px.getGreen()),
                                  invertAndContrast(px.getBlue()));
        auto blueTinted =
            inverted.interpolatedWith(DarkTheme::getColour(DarkTheme::ACCENT_BLUE), 0.45f);
        return blueTinted.withAlpha(px.getAlpha());
    };

    // Force a software image: NativeImageType on Windows is Direct2D-backed,
    // and BitmapData pixel writes don't round-trip back to the GPU surface,
    // so the inversion below would silently no-op on Windows.
    auto snapshot = owner.trackContentPanel->createComponentSnapshot(panelRect, false, 1.0f,
                                                                     juce::SoftwareImageType{});
    if (snapshot.isValid() && snapshot.getWidth() > 0 && snapshot.getHeight() > 0) {
        juce::Image::BitmapData data(snapshot, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < data.height; ++y) {
            for (int x = 0; x < data.width; ++x) {
                const auto px = data.getPixelColour(x, y);
                data.setPixelColour(x, y, invertPixel(px));
            }
        }

        g.drawImageAt(snapshot, panelRect.getX() - scrollX, panelRect.getY() - scrollY);
    }

    if (owner.gridOverlay) {
        auto gridSnapshot = owner.gridOverlay->createComponentSnapshot(bandRect, false, 1.0f,
                                                                       juce::SoftwareImageType{});
        if (gridSnapshot.isValid() && gridSnapshot.getWidth() > 0 && gridSnapshot.getHeight() > 0) {
            juce::Image::BitmapData data(gridSnapshot, juce::Image::BitmapData::readWrite);
            for (int y = 0; y < data.height; ++y) {
                for (int x = 0; x < data.width; ++x) {
                    const auto px = data.getPixelColour(x, y);
                    if (px.getAlpha() == 0)
                        continue;

                    data.setPixelColour(x, y, invertPixel(px));
                }
            }

            g.drawImageAt(gridSnapshot, bandRect.getX(), bandRect.getY());
        }
    }
}

void MainView::SelectionOverlayComponent::drawLoopRegion(juce::Graphics& g) {
    const auto& state = owner.timelineController->getState();

    // Always draw if there's a valid loop region, but use grey when disabled
    if (!state.loop.isValid()) {
        return;
    }

    // Calculate pixel positions from authoritative beat state.
    double loopStartBeats = state.loop.startBeats;
    double loopEndBeats = state.loop.endBeats;
    // std::round so the loop edges sit on the same column as the ruler flags
    // and the bar/beat ticks; truncation here was the source of the visible
    // 1-pixel misalignment between the ruler loop strip and the overlay line.
    int startX = static_cast<int>(std::round(loopStartBeats * state.zoom.horizontalZoom)) +
                 LayoutConfig::TIMELINE_LEFT_PADDING;
    int endX = static_cast<int>(std::round(loopEndBeats * state.zoom.horizontalZoom)) +
               LayoutConfig::TIMELINE_LEFT_PADDING;

    // Adjust for scroll offset
    int scrollOffset = owner.trackContentViewport->getViewPositionX();
    startX -= scrollOffset;
    endX -= scrollOffset;

    // Skip if out of view
    if (endX < 0 || startX > getWidth()) {
        return;
    }

    // Track original positions before clamping (for marker visibility)
    int originalStartX = startX;
    int originalEndX = endX;

    // Clamp to visible area for the filled region
    startX = juce::jmax(0, startX);
    endX = juce::jmin(getWidth(), endX);

    // Use different colors based on enabled state
    bool enabled = state.loop.enabled;
    juce::Colour regionColour = enabled ? DarkTheme::getColour(DarkTheme::LOOP_REGION)
                                        : juce::Colour(0x15808080);  // Light grey, very transparent
    juce::Colour markerColour = enabled
                                    ? DarkTheme::getColour(DarkTheme::LOOP_MARKER).withAlpha(0.8f)
                                    : juce::Colour(0xFF606060);  // Medium grey

    // Draw semi-transparent loop region
    g.setColour(regionColour);
    g.fillRect(startX, 0, endX - startX, getHeight());

    // Draw loop region edges only if they're actually visible (not clamped)
    g.setColour(markerColour);
    if (originalStartX >= 0 && originalStartX <= getWidth()) {
        g.drawLine(static_cast<float>(originalStartX), 0.0f, static_cast<float>(originalStartX),
                   static_cast<float>(getHeight()), 2.0f);
    }
    if (originalEndX >= 0 && originalEndX <= getWidth()) {
        g.drawLine(static_cast<float>(originalEndX), 0.0f, static_cast<float>(originalEndX),
                   static_cast<float>(getHeight()), 2.0f);
    }
}

void MainView::SelectionOverlayComponent::drawRecordingRegion(juce::Graphics& g) {
    const auto& state = owner.timelineController->getState();

    if (!state.playhead.isRecording) {
        return;
    }

    // Recording region: from edit playhead beat to current playback beat.
    double recordStartBeats = state.playhead.editPositionBeats;
    double recordEndBeats = state.playhead.playbackPositionBeats;

    if (recordEndBeats <= recordStartBeats) {
        return;
    }

    int startX = static_cast<int>(std::round(recordStartBeats * state.zoom.horizontalZoom)) +
                 LayoutConfig::TIMELINE_LEFT_PADDING;
    int endX = static_cast<int>(std::round(recordEndBeats * state.zoom.horizontalZoom)) +
               LayoutConfig::TIMELINE_LEFT_PADDING;

    // Adjust for scroll offset
    int scrollOffset = owner.trackContentViewport->getViewPositionX();
    startX -= scrollOffset;
    endX -= scrollOffset;

    // Skip if out of view
    if (endX < 0 || startX > getWidth()) {
        return;
    }

    startX = juce::jmax(0, startX);
    endX = juce::jmin(getWidth(), endX);

    int scrollY = owner.trackContentViewport->getViewPositionY();
    auto& tracks = TrackManager::getInstance().getTracks();

    for (int trackIndex = 0; trackIndex < (int)tracks.size(); ++trackIndex) {
        if (!tracks[trackIndex].recordArmed) {
            continue;
        }

        int trackY = owner.trackContentPanel->getTrackYPosition(trackIndex) - scrollY;
        int trackHeight = static_cast<int>(owner.trackContentPanel->getTrackHeight(trackIndex) *
                                           owner.verticalZoom);

        // Skip if not visible
        if (trackY + trackHeight < 0 || trackY > getHeight()) {
            continue;
        }

        int drawY = juce::jmax(0, trackY);
        int drawBottom = juce::jmin(getHeight(), trackY + trackHeight);
        int drawHeight = drawBottom - drawY;

        if (drawHeight > 0) {
            // Use the same style as a MIDI clip: darker fill of the default clip color
            auto clipColour = juce::Colour(Config::getDefaultColour(
                static_cast<int>(ClipManager::getInstance().getArrangementClips().size())));
            g.setColour(clipColour.darker(0.3f));
            g.fillRoundedRectangle(startX, drawY, endX - startX, drawHeight, 3.0f);

            // Red recording border
            g.setColour(juce::Colours::red);
            g.drawRoundedRectangle(static_cast<float>(startX), static_cast<float>(drawY),
                                   static_cast<float>(endX - startX),
                                   static_cast<float>(drawHeight), 3.0f, 1.5f);
        }
    }
}

// ===== MasterHeaderPanel Implementation =====

MainView::MasterHeaderPanel::MasterHeaderPanel() {
    // Register as TrackManager listener
    TrackManager::getInstance().addListener(this);

    setupControls();

    // Sync initial state from master channel
    masterChannelChanged();
}

MainView::MasterHeaderPanel::~MasterHeaderPanel() {
    TrackManager::getInstance().removeListener(this);
}

void MainView::MasterHeaderPanel::setupControls() {
    // Speaker on/off button (toggles master mute). Dual-icon: audible = gray
    // speaker (master_on), muted = orange block (master_off); pre-baked colors.
    speakerButton = std::make_unique<SvgButton>(
        "Speaker", BinaryData::master_on_svg, BinaryData::master_on_svgSize,
        BinaryData::master_off_1_svg, BinaryData::master_off_1_svgSize);
    speakerButton->setClickingTogglesState(true);
    speakerButton->setTooltip("Mute master");
    speakerButton->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    speakerButton->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    speakerButton->onClick = [this]() {
        UndoManager::getInstance().executeCommand(
            std::make_unique<SetMasterMuteCommand>(speakerButton->getToggleState()));
    };
    addAndMakeVisible(*speakerButton);

    // Automation button: same icon as the per-track headers, opens the master
    // automation menu.
    automationButton = std::make_unique<SvgButton>("Automation", BinaryData::automation_svg,
                                                   BinaryData::automation_svgSize);
    automationButton->setTooltip(tr("tracks.automation"));
    automationButton->setColour(juce::TextButton::buttonColourId,
                                DarkTheme::getColour(DarkTheme::SURFACE));
    automationButton->setColour(juce::TextButton::buttonOnColourId,
                                DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    automationButton->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    automationButton->setNormalBackgroundColor(DarkTheme::getColour(DarkTheme::SURFACE));
    automationButton->setIconPadding(6.0f);  // a touch smaller than the speaker glyph
    automationButton->onClick = [this]() {
        // Alt/Option-click toggles global show/hide of all automation lanes.
        if (juce::ModifierKeys::getCurrentModifiers().isAltDown()) {
            auto& am = AutomationManager::getInstance();
            am.setGlobalLaneVisibility(!am.isGlobalLaneVisibilityEnabled());
            return;
        }
        showMasterAutomationMenu(automationButton.get());
    };
    addAndMakeVisible(*automationButton);

    hideButton = std::make_unique<SvgButton>("Hide master track", BinaryData::bottom_close_svg,
                                             BinaryData::bottom_close_svgSize);
    hideButton->setTooltip("Hide master track");
    hideButton->setOriginalColor(juce::Colour(0xFFB3B3B3));
    hideButton->setNormalColor(juce::Colour(0xFFB3B3B3));
    hideButton->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    hideButton->setPressedColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    hideButton->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    hideButton->setBorderThickness(1.0f);
    hideButton->onClick = []() {
        TrackManager::getInstance().setMasterVisible(
            ViewModeController::getInstance().getViewMode(), false);
    };
    addAndMakeVisible(*hideButton);

    volumeLabel = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Decibels);
    volumeLabel->setRange(-60.0, 6.0, 0.0);
    // Curve the fill to match the level meter's power scale so the volume fill
    // edge lines up with the meter's 0 dB tick below it.
    volumeLabel->setFillExponent(static_cast<double>(LevelMeter::METER_CURVE_EXPONENT));
    volumeLabel->setDoubleClickResetsValue(true);
    volumeLabel->onValueChange = [this]() {
        const float db = static_cast<float>(volumeLabel->getValue());
        UndoManager::getInstance().executeCommand(
            std::make_unique<SetMasterVolumeCommand>(dbToGain(db)));
    };
    addAndMakeVisible(*volumeLabel);

    peakMeter = std::make_unique<LevelMeter>();
    peakMeter->setOrientation(LevelMeter::Orientation::Horizontal);
    addAndMakeVisible(*peakMeter);

    peakValueLabel = std::make_unique<ClickableLabel>();
    peakValueLabel->setText("-inf", juce::dontSendNotification);
    peakValueLabel->setJustificationType(juce::Justification::centredLeft);
    peakValueLabel->setFont(FontManager::getInstance().getMonoFont(9.0f));
    peakValueLabel->setColour(juce::Label::textColourId,
                              DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    peakValueLabel->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    peakValueLabel->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    peakValueLabel->setTooltip("Click to reset peak");
    peakValueLabel->onClick = [this]() {
        peakValue_ = 0.0f;
        peakValueLabel->setText("-inf", juce::dontSendNotification);
        if (peakMeter)
            peakMeter->resetPeak();
    };
    addAndMakeVisible(*peakValueLabel);
}

void MainView::MasterHeaderPanel::paint(juce::Graphics& g) {
    // Background
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));

    // Border
    auto bounds = getLocalBounds();
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(bounds, 1);

    // "Master" label at top
    auto labelArea = bounds.reduced(6, 2).removeFromTop(14);
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    g.setFont(FontManager::getInstance().getUIFont(11.0f));
    g.drawText(magda::technicalText(magda::TechnicalTextToken::Master), labelArea,
               juce::Justification::centredLeft);
}

void MainView::MasterHeaderPanel::mouseDown(const juce::MouseEvent& event) {
    SelectionManager::getInstance().selectTrack(MASTER_TRACK_ID);

    if (event.mods.isPopupMenu())
        showMasterAutomationMenu(this);
}

void MainView::MasterHeaderPanel::showMasterAutomationMenu(juce::Component* anchor) {
    // Same menu builder as the per-track headers — it walks the master channel's
    // volume, macros, modulators, and device chain (pan/sends are skipped for
    // the master). The band updates via its own listener, so no callback.
    showAutomationMenu(MASTER_TRACK_ID, anchor);
}

void MainView::MasterHeaderPanel::resized() {
    auto contentArea = getLocalBounds().reduced(2);
    // The hide control now lives in the footer (next to the show toggle), not
    // in the master header.
    hideButton->setVisible(false);
    contentArea.removeFromTop(14);  // "Master" label row
    contentArea.removeFromTop(6);   // padding below the label

    // Two rows sharing a fixed icon column. The value/meter column takes the
    // remaining width; the peak readout sits below the meter instead of
    // occupying a separate empty-left column.
    const int rowH = 20;
    const int rowGap = 2;
    const int colGap = 6;
    const int iconSize = 20;
    const int rowLeftInset = 6;
    const int iconRightInset = 8;

    const int iconColumnWidth = iconSize;
    const int mainColumnWidth =
        juce::jmax(0, contentArea.getWidth() - iconColumnWidth - colGap - iconRightInset);

    auto row1 = contentArea.removeFromTop(rowH);
    contentArea.removeFromTop(rowGap);
    auto meterRow = contentArea;

    auto topMain = row1.removeFromLeft(mainColumnWidth);
    row1.removeFromLeft(colGap);
    auto topIcon = row1.removeFromLeft(iconColumnWidth);

    auto meterMain = meterRow.removeFromLeft(mainColumnWidth);
    meterRow.removeFromLeft(colGap);
    auto meterIcon = meterRow.removeFromLeft(iconColumnWidth);

    topMain.removeFromLeft(rowLeftInset);
    meterMain.removeFromLeft(rowLeftInset);

    constexpr int peakReadoutHeight = 10;
    auto peakReadout = meterMain.removeFromBottom(peakReadoutHeight);
    auto peakMeterBounds = meterMain;

    volumeLabel->setBounds(topMain);
    speakerButton->setBounds(topIcon.withSizeKeepingCentre(iconSize, iconSize));

    peakMeter->setBounds(peakMeterBounds);
    peakValueLabel->setBounds(peakReadout);
    automationButton->setBounds(meterIcon.withSizeKeepingCentre(iconSize, iconSize));
}

void MainView::MasterHeaderPanel::masterChannelChanged() {
    const auto& master = TrackManager::getInstance().getMasterChannel();

    // Dual-icon: toggle state drives which baked icon (audible vs muted) shows.
    speakerButton->setToggleState(master.muted, juce::dontSendNotification);
    speakerButton->setTooltip(master.muted ? "Unmute master" : "Mute master");

    volumeLabel->setValue(gainToDb(master.volume), juce::dontSendNotification);

    repaint();
}

void MainView::MasterHeaderPanel::setPeakLevels(float leftPeak, float rightPeak) {
    if (peakMeter) {
        peakMeter->setLevels(leftPeak, rightPeak);

        const float peakDb = peakMeter->getPeakDb();
        if (peakDb > gainToDb(peakValue_)) {
            peakValue_ = dbToGain(peakDb);
            peakValueLabel->setText(formatDbValue(peakDb), juce::dontSendNotification);
        }
    }
}

// ===== MasterContentPanel Implementation =====

MainView::MasterContentPanel::MasterContentPanel() {
    // Empty for now - will show waveform later
}

void MainView::MasterContentPanel::paint(juce::Graphics& g) {
    // Background matching track content area
    g.fillAll(DarkTheme::getColour(DarkTheme::TRACK_BACKGROUND));

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);

    // Draw a subtle indicator that this is the master output area
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).withAlpha(0.3f));
    g.setFont(FontManager::getInstance().getUIFont(11.0f));
    g.drawText(tr("common.master_output"), getLocalBounds(), juce::Justification::centred);
}

// ===== TrackManagerListener — aux track management =====

void MainView::tracksChanged() {
    // Count aux tracks and update aux section visibility
    const auto& tracks = TrackManager::getInstance().getTracks();
    int auxCount = 0;
    for (const auto& track : tracks) {
        if (track.type == TrackType::Aux && track.isVisibleIn(currentViewMode_))
            ++auxCount;
    }

    auxSectionHeight = auxCount * AUX_ROW_HEIGHT;
    bool newAuxVisible = auxCount > 0;

    auxVisible_ = newAuxVisible;
    auxHeadersPanel->setVisible(auxVisible_);
    auxContentPanel->setVisible(auxVisible_);
    auxContentPanel->setAuxTrackCount(auxCount);
    resized();
}

// ===== Grid Division Display =====

juce::String MainView::calculateGridDivisionString() const {
    const auto& state = timelineController->getState();

    // If grid override is active, return the numerator/denominator string
    if (!state.display.gridQuantize.autoGrid) {
        int num = state.display.gridQuantize.numerator;
        int den = state.display.gridQuantize.denominator;
        return juce::String(num) + "/" + juce::String(den);
    }

    // Auto mode: compute smart grid and format as text
    int num = 0, den = 0;
    bool isBars = false;
    calculateSmartGridNumeratorDenominator(num, den, isBars);

    if (isBars) {
        return num == 1 ? "1 bar" : juce::String(num) + " bars";
    }
    return juce::String(num) + "/" + juce::String(den);
}

void MainView::calculateSmartGridNumeratorDenominator(int& outNum, int& outDen,
                                                      bool& outIsBars) const {
    const auto& state = timelineController->getState();
    double zoom = state.zoom.horizontalZoom;
    int timeSigNumerator = state.tempo.timeSignatureNumerator;
    auto& layout = LayoutConfig::getInstance();
    int minPixelSpacing = layout.minGridPixelSpacing;

    outIsBars = false;

    // Try beat subdivisions (powers of 2)
    double frac = GridConstants::findBeatSubdivision(zoom, minPixelSpacing);
    if (frac > 0) {
        // Convert beat fraction to whole-note-relative num/den
        // beatFraction = 2^p, denominator = 4 / beatFraction
        outNum = 1;
        outDen = static_cast<int>(4.0 / frac);
        if (outDen < 1)
            outDen = 1;  // For frac > 4 (shouldn't happen)
        return;
    }

    // Bar multiples
    int mult = GridConstants::findBarMultiple(zoom, timeSigNumerator, minPixelSpacing);
    outNum = mult;
    outDen = 0;
    outIsBars = true;
}

void MainView::updateGridDivisionDisplay() {
    if (horizontalZoomScrollBar) {
        horizontalZoomScrollBar->setLabel(calculateGridDivisionString());
    }

    // When Auto mode is on, push the smart grid values to the transport panel
    // and update the state so auto→manual switch can seed from them
    const auto& state = timelineController->getState();
    if (state.display.gridQuantize.autoGrid) {
        int num = 0, den = 0;
        bool isBars = false;
        calculateSmartGridNumeratorDenominator(num, den, isBars);
        timelineController->dispatch(SetAutoGridDisplayEvent{num, den});
        if (onGridQuantizeChanged)
            onGridQuantizeChanged(true, num, den, isBars);
    }
}

// ===== AuxHeadersPanel Implementation =====

MainView::AuxHeadersPanel::AuxHeadersPanel() {
    TrackManager::getInstance().addListener(this);
    rebuildAuxRows();
}

MainView::AuxHeadersPanel::~AuxHeadersPanel() {
    TrackManager::getInstance().removeListener(this);
}

void MainView::AuxHeadersPanel::tracksChanged() {
    rebuildAuxRows();
}

void MainView::AuxHeadersPanel::rebuildAuxRows() {
    // Remove all existing child components
    for (auto& row : auxRows_) {
        removeChildComponent(row->nameLabel.get());
        removeChildComponent(row->volumeLabel.get());
        removeChildComponent(row->panLabel.get());
        removeChildComponent(row->muteButton.get());
        removeChildComponent(row->soloButton.get());
    }
    auxRows_.clear();

    const auto& tracks = TrackManager::getInstance().getTracks();
    auto currentViewMode = ViewModeController::getInstance().getViewMode();

    for (const auto& track : tracks) {
        if (track.type != TrackType::Aux || !track.isVisibleIn(currentViewMode))
            continue;

        auto row = std::make_unique<AuxRow>();
        row->trackId = track.id;

        // Name label - show "Aux N" based on bus index
        juce::String auxName = "Aux " + juce::String(track.auxBusIndex + 1);
        row->nameLabel = std::make_unique<juce::Label>("auxName", auxName);
        row->nameLabel->setColour(juce::Label::textColourId,
                                  DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        row->nameLabel->setFont(FontManager::getInstance().getUIFont(11.0f));
        addAndMakeVisible(*row->nameLabel);

        // Volume label
        row->volumeLabel =
            std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Decibels);
        row->volumeLabel->setRange(-60.0, 6.0, 0.0);
        float db = gainToDb(track.volume);
        row->volumeLabel->setValue(db, juce::dontSendNotification);
        TrackId tid = track.id;
        auto* volLabelPtr = row->volumeLabel.get();
        row->volumeLabel->onValueChange = [tid, volLabelPtr]() {
            float newDb = static_cast<float>(volLabelPtr->getValue());
            float gain = dbToGain(newDb);
            UndoManager::getInstance().executeCommand(
                std::make_unique<SetTrackVolumeCommand>(tid, gain));
        };
        addAndMakeVisible(*row->volumeLabel);

        // Pan label
        row->panLabel = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Pan);
        row->panLabel->setRange(-1.0, 1.0, 0.0);
        row->panLabel->setValue(track.pan, juce::dontSendNotification);
        auto* panLabelPtr = row->panLabel.get();
        row->panLabel->onValueChange = [tid, panLabelPtr]() {
            UndoManager::getInstance().executeCommand(std::make_unique<SetTrackPanCommand>(
                tid, static_cast<float>(panLabelPtr->getValue())));
        };
        addAndMakeVisible(*row->panLabel);

        // Mute button
        row->muteButton = std::make_unique<juce::TextButton>("M");
        row->muteButton->setConnectedEdges(
            juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
            juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
        row->muteButton->setColour(juce::TextButton::buttonColourId,
                                   DarkTheme::getColour(DarkTheme::SURFACE));
        row->muteButton->setColour(juce::TextButton::buttonOnColourId,
                                   DarkTheme::getColour(DarkTheme::STATUS_WARNING));
        row->muteButton->setColour(juce::TextButton::textColourOffId,
                                   DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        row->muteButton->setColour(juce::TextButton::textColourOnId,
                                   DarkTheme::getColour(DarkTheme::BACKGROUND));
        row->muteButton->setClickingTogglesState(true);
        row->muteButton->setToggleState(track.muted, juce::dontSendNotification);
        auto* muteBtnPtr = row->muteButton.get();
        row->muteButton->onClick = [tid, muteBtnPtr]() {
            UndoManager::getInstance().executeCommand(
                std::make_unique<SetTrackMuteCommand>(tid, muteBtnPtr->getToggleState()));
        };
        addAndMakeVisible(*row->muteButton);

        // Solo button
        row->soloButton = std::make_unique<juce::TextButton>("S");
        row->soloButton->setConnectedEdges(
            juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
            juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
        row->soloButton->setColour(juce::TextButton::buttonColourId,
                                   DarkTheme::getColour(DarkTheme::SURFACE));
        row->soloButton->setColour(juce::TextButton::buttonOnColourId,
                                   DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
        row->soloButton->setColour(juce::TextButton::textColourOffId,
                                   DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        row->soloButton->setColour(juce::TextButton::textColourOnId,
                                   DarkTheme::getColour(DarkTheme::BACKGROUND));
        row->soloButton->setClickingTogglesState(true);
        row->soloButton->setToggleState(track.soloed, juce::dontSendNotification);
        auto* soloBtnPtr = row->soloButton.get();
        row->soloButton->onClick = [tid, soloBtnPtr]() {
            UndoManager::getInstance().executeCommand(
                std::make_unique<SetTrackSoloCommand>(tid, soloBtnPtr->getToggleState()));
        };
        addAndMakeVisible(*row->soloButton);

        auxRows_.push_back(std::move(row));
    }

    resized();
    repaint();
}

void MainView::AuxHeadersPanel::paint(juce::Graphics& g) {
    // Slightly different background to distinguish from regular tracks
    g.fillAll(DarkTheme::getColour(DarkTheme::SURFACE).darker(0.1f));

    // Draw borders between rows
    int rowHeight = getHeight() / juce::jmax(1, static_cast<int>(auxRows_.size()));
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);

    for (size_t i = 1; i < auxRows_.size(); ++i) {
        int y = static_cast<int>(i) * rowHeight;
        g.drawHorizontalLine(y, 0.0f, static_cast<float>(getWidth()));
    }
}

void MainView::AuxHeadersPanel::mouseDown(const juce::MouseEvent& event) {
    if (auxRows_.empty())
        return;

    int rowHeight = getHeight() / static_cast<int>(auxRows_.size());
    int rowIndex = event.getPosition().getY() / rowHeight;

    if (rowIndex >= 0 && rowIndex < static_cast<int>(auxRows_.size())) {
        const auto trackId = auxRows_[rowIndex]->trackId;
        SelectionManager::getInstance().selectTrack(trackId);

        if (event.mods.isPopupMenu()) {
            juce::PopupMenu menu;
            menu.addItem(1, "Delete Aux Track");
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                               [trackId](int result) {
                                   if (result == 1)
                                       UndoManager::getInstance().executeCommand(
                                           std::make_unique<DeleteTrackCommand>(trackId));
                               });
        }
    }
}

void MainView::AuxHeadersPanel::resized() {
    if (auxRows_.empty())
        return;

    int rowHeight = getHeight() / static_cast<int>(auxRows_.size());
    auto bounds = getLocalBounds();

    for (size_t i = 0; i < auxRows_.size(); ++i) {
        auto& row = *auxRows_[i];
        auto rowArea = bounds.removeFromTop(rowHeight);
        // Centre a fixed 18px-tall strip within the row (matching master header controls)
        auto controlArea = rowArea.withSizeKeepingCentre(rowArea.getWidth() - 8, 18);

        // Layout: [Name 36px] [M 18px] [S 18px] [Vol 40px] [Pan 32px]
        row.nameLabel->setBounds(controlArea.removeFromLeft(36));
        controlArea.removeFromLeft(4);
        row.muteButton->setBounds(controlArea.removeFromLeft(18).withSizeKeepingCentre(16, 16));
        controlArea.removeFromLeft(2);
        row.soloButton->setBounds(controlArea.removeFromLeft(18).withSizeKeepingCentre(16, 16));
        controlArea.removeFromLeft(4);
        row.volumeLabel->setBounds(controlArea.removeFromLeft(40));
        controlArea.removeFromLeft(4);
        row.panLabel->setBounds(controlArea.removeFromLeft(32));
    }
}

void MainView::AuxHeadersPanel::updateMetering(AudioEngine* engine) {
    // Aux metering could be added here in the future
    // For now, aux tracks share the same metering infrastructure as regular tracks
    (void)engine;
}

// ===== AuxContentPanel Implementation =====

void MainView::AuxContentPanel::paint(juce::Graphics& g) {
    // Background matching track content but slightly different to distinguish aux
    g.fillAll(DarkTheme::getColour(DarkTheme::TRACK_BACKGROUND).darker(0.05f));

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);

    if (auxTrackCount_ > 0) {
        // Draw row separators
        int rowHeight = getHeight() / auxTrackCount_;
        for (int i = 1; i < auxTrackCount_; ++i) {
            int y = i * rowHeight;
            g.drawHorizontalLine(y, 0.0f, static_cast<float>(getWidth()));
        }
    }
}

}  // namespace magda
