#include "SessionView.hpp"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <unordered_map>

#include "../../audio/AudioBridge.hpp"
#include "../../audio/MeteringBuffer.hpp"
#include "../../engine/AudioEngine.hpp"
#include "../../engine/TracktionEngineWrapper.hpp"
#include "../components/common/SvgButton.hpp"
#include "../components/common/TextSlider.hpp"
#include "../components/mixer/LevelMeter.hpp"
#include "../components/mixer/LevelMeterScale.hpp"
#include "../components/mixer/RoutingSelector.hpp"
#include "../components/mixer/RoutingSyncHelper.hpp"
#include "../panels/state/PanelController.hpp"
#include "../state/TimelineController.hpp"
#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"
#include "../themes/SmallButtonLookAndFeel.hpp"
#include "../utils/SelectionPolicy.hpp"
#include "ClipSlotButton.hpp"
#include "core/ClipCommands.hpp"
#include "core/ClipPropertyCommands.hpp"
#include "core/Config.hpp"
#include "core/SelectionManager.hpp"
#include "core/SessionLaunchService.hpp"
#include "core/SessionViewState.hpp"
#include "core/TechnicalText.hpp"
#include "core/TempoUtils.hpp"
#include "core/TrackCommands.hpp"
#include "core/TrackPropertyCommands.hpp"
#include "core/UndoManager.hpp"
#include "core/ViewModeController.hpp"

namespace magda {

// dB conversion helpers for faders
namespace {
constexpr float MIN_DB = level_meter_scale::minDb;
constexpr float MAX_DB = level_meter_scale::maxDb;

juce::String formatTrackIds(const std::vector<TrackId>& trackIds) {
    juce::String text("[");
    for (size_t i = 0; i < trackIds.size(); ++i) {
        if (i > 0)
            text << ",";
        text << trackIds[i];
    }
    text << "]";
    return text;
}

juce::String formatSessionClips() {
    auto clips = ClipManager::getInstance().getSessionClips();
    std::sort(clips.begin(), clips.end(), [](const ClipInfo& a, const ClipInfo& b) {
        if (a.trackId != b.trackId)
            return a.trackId < b.trackId;
        if (a.sceneIndex != b.sceneIndex)
            return a.sceneIndex < b.sceneIndex;
        return a.id < b.id;
    });

    juce::String text("[");
    for (size_t i = 0; i < clips.size(); ++i) {
        const auto& clip = clips[i];
        if (i > 0)
            text << ",";
        text << "{id=" << clip.id << ",track=" << clip.trackId << ",scene=" << clip.sceneIndex
             << ",name=\"" << clip.name << "\"}";
    }
    text << "]";
    return text;
}

float gainToDb(float gain) {
    return level_meter_scale::gainToDb(gain);
}

float dbToGain(float db) {
    if (db <= MIN_DB)
        return 0.0f;
    return std::pow(10.0f, db / 20.0f);
}

float dbToMeterPos(float db) {
    return level_meter_scale::dbToMeterPos(db);
}

float meterPosToDb(float pos) {
    return level_meter_scale::meterPosToDb(pos);
}

// Multi-track edit fan-out: when a strip is part of a multi-selection, every
// selected track receives the same edit. Otherwise only the clicked track is
// touched.
std::vector<TrackId> getMultiEditTargets(TrackId clickedId) {
    auto& sel = SelectionManager::getInstance();
    if (sel.isTrackSelected(clickedId) && sel.getSelectedTrackCount() > 1) {
        const auto& set = sel.getSelectedTracks();
        return std::vector<TrackId>(set.begin(), set.end());
    }
    return {clickedId};
}
}  // namespace

class SessionView::SessionToggleRail : public juce::Component {
  public:
    SessionToggleRail() {
        auto& cfg = Config::getInstance();

        setupButton(sendsButton_, "SessionShowSends", BinaryData::iconsendsboldm_svg,
                    BinaryData::iconsendsboldm_svgSize, "Show sends", cfg.getMixerShowSends(),
                    [](bool v) { Config::getInstance().setMixerShowSends(v); });

        setupButton(routingButton_, "SessionShowRouting", BinaryData::inputoutput_svg,
                    BinaryData::inputoutput_svgSize, "Show I/O routing", cfg.getMixerShowRouting(),
                    [](bool v) { Config::getInstance().setMixerShowRouting(v); });

        setupButton(monitorButton_, "SessionShowMonitor", BinaryData::recordmonitor_svg,
                    BinaryData::recordmonitor_svgSize, "Show record/monitor row",
                    cfg.getMixerShowMonitor(),
                    [](bool v) { Config::getInstance().setMixerShowMonitor(v); });
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.fillRect(bounds.getRight() - 1, bounds.getY(), 1, bounds.getHeight());
    }

    void resized() override {
        constexpr int BTN_SIZE = 28;
        constexpr int BTN_SPACING = 6;
        constexpr int EDGE_PADDING = 8;

        auto bounds = getLocalBounds();
        const int x = (bounds.getWidth() - BTN_SIZE) / 2;

        int yTop = EDGE_PADDING;
        if (sendsButton_) {
            sendsButton_->setBounds(x, yTop, BTN_SIZE, BTN_SIZE);
            yTop += BTN_SIZE + BTN_SPACING;
        }

        int yBottom = bounds.getHeight() - EDGE_PADDING - BTN_SIZE;
        if (monitorButton_) {
            monitorButton_->setBounds(x, yBottom, BTN_SIZE, BTN_SIZE);
            yBottom -= BTN_SIZE + BTN_SPACING;
        }
        if (routingButton_)
            routingButton_->setBounds(x, yBottom, BTN_SIZE, BTN_SIZE);
    }

    void refreshFromConfig() {
        auto& cfg = Config::getInstance();
        applyToggleState(sendsButton_.get(), cfg.getMixerShowSends());
        applyToggleState(routingButton_.get(), cfg.getMixerShowRouting());
        applyToggleState(monitorButton_.get(), cfg.getMixerShowMonitor());
    }

    static constexpr int RAIL_WIDTH = 36;
    std::function<void()> onToggleChanged;

  private:
    std::unique_ptr<SvgButton> sendsButton_;
    std::unique_ptr<SvgButton> routingButton_;
    std::unique_ptr<SvgButton> monitorButton_;

    void setupButton(std::unique_ptr<SvgButton>& btn, const juce::String& name, const char* svgData,
                     size_t svgSize, const juce::String& tooltip, bool initialState,
                     std::function<void(bool)> setter) {
        btn = std::make_unique<SvgButton>(name, svgData, svgSize);
        btn->setOriginalColor(juce::Colour(0xFFB3B3B3));
        btn->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        btn->setPressedColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        btn->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
        btn->setBorderThickness(1.0f);
        btn->setTooltip(tooltip);
        btn->setWantsKeyboardFocus(false);
        applyToggleState(btn.get(), initialState);

        btn->onClick = [this, raw = btn.get(), setter = std::move(setter)]() {
            const bool newState = !raw->isActive();
            setter(newState);
            applyToggleState(raw, newState);
            Config::getInstance().save();
            if (onToggleChanged)
                onToggleChanged();
        };

        addAndMakeVisible(*btn);
    }

    static void applyToggleState(SvgButton* btn, bool on) {
        if (btn == nullptr)
            return;
        btn->setActive(on);
        const auto base = DarkTheme::getColour(DarkTheme::TEXT_SECONDARY);
        btn->setNormalColor(on ? base : base.withAlpha(0.3f));
        btn->repaint();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SessionToggleRail)
};

// Custom grid content that draws track separators and empty cells
class SessionView::GridContent : public juce::Component {
  public:
    GridContent(int /*clipHeight*/, int separatorWidth, int /*clipMargin*/, int numScenes)
        : separatorWidth_(separatorWidth), numScenes_(numScenes) {
        setInterceptsMouseClicks(true, true);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu() && onContextMenu)
            onContextMenu();
    }

    std::function<void()> onContextMenu;

    void setNumTracks(int numTracks) {
        numTracks_ = numTracks;
        repaint();
    }

    void setNumScenes(int numScenes) {
        numScenes_ = numScenes;
        repaint();
    }

    void setTrackWidths(const std::vector<int>& widths) {
        trackWidths_ = widths;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));

        // Draw vertical separators between tracks (after each clip slot)
        g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
        int x = 0;
        for (int i = 0; i < numTracks_ && i < static_cast<int>(trackWidths_.size()); ++i) {
            x += trackWidths_[i];
            g.fillRect(x, 0, separatorWidth_, getHeight());
            x += separatorWidth_;
        }
    }

  private:
    int numTracks_ = 0;
    std::vector<int> trackWidths_;
    int separatorWidth_;
    int numScenes_;
};

// Custom viewport that draws track separators in the background area
class SessionView::GridViewport : public juce::Viewport {
  public:
    GridViewport() {
        setInterceptsMouseClicks(true, true);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu() && onContextMenu)
            onContextMenu();
        else
            juce::Viewport::mouseDown(e);
    }

    std::function<void()> onContextMenu;

    void setTrackLayout(int numTracks, const std::vector<int>& trackWidths, int separatorWidth) {
        numTracks_ = numTracks;
        trackWidths_ = trackWidths;
        separatorWidth_ = separatorWidth;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));

        // Draw vertical separators in the background (visible when content is shorter than
        // viewport)
        int scrollX = getViewPositionX();
        g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
        int x = 0;
        for (int i = 0; i < numTracks_ && i < static_cast<int>(trackWidths_.size()); ++i) {
            x += trackWidths_[i];
            g.fillRect(x - scrollX, 0, separatorWidth_, getHeight());
            x += separatorWidth_;
        }
    }

  private:
    int numTracks_ = 0;
    std::vector<int> trackWidths_;
    int separatorWidth_ = 3;
};

// Container for track headers with clipping
class SessionView::HeaderContainer : public juce::Component {
  public:
    HeaderContainer() {
        setInterceptsMouseClicks(false, true);
    }

    void setTrackLayout(int numTracks, const std::vector<int>& trackWidths, int separatorWidth,
                        int scrollOffset) {
        numTracks_ = numTracks;
        trackWidths_ = trackWidths;
        separatorWidth_ = separatorWidth;
        scrollOffset_ = scrollOffset;
        repaint();
    }

    std::function<void(juce::Graphics&)> onPaintOverChildren;

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));

        // Draw vertical separators between tracks
        g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
        int x = 0;
        for (int i = 0; i < numTracks_ && i < static_cast<int>(trackWidths_.size()); ++i) {
            x += trackWidths_[i];
            g.fillRect(x - scrollOffset_, 0, separatorWidth_, getHeight());
            x += separatorWidth_;
        }
    }

    void paintOverChildren(juce::Graphics& g) override {
        if (onPaintOverChildren)
            onPaintOverChildren(g);
    }

  private:
    int numTracks_ = 0;
    std::vector<int> trackWidths_;
    int separatorWidth_ = 3;
    int scrollOffset_ = 0;
};

// Container for scene buttons with clipping. No background fill — the
// master/scene column blends into the parent so only the buttons read.
class SessionView::SceneContainer : public juce::Component {
  public:
    SceneContainer() {
        setInterceptsMouseClicks(false, true);
    }
};

// ResizeHandle for dragging to resize areas
class SessionView::ResizeHandle : public juce::Component {
  public:
    enum Direction { Horizontal, Vertical };

    ResizeHandle(Direction dir) : direction(dir) {
        setMouseCursor(direction == Horizontal ? juce::MouseCursor::LeftRightResizeCursor
                                               : juce::MouseCursor::UpDownResizeCursor);
    }

    void paint(juce::Graphics& g) override {
        g.setColour(DarkTheme::getColour(DarkTheme::RESIZE_HANDLE));
        g.fillAll();
    }

    void mouseDown(const juce::MouseEvent& event) override {
        if (event.mods.isPopupMenu()) {
            if (onContextMenu)
                onContextMenu();
            return;
        }
        if (onResizeStart) {
            onResizeStart();
        }
    }

    void mouseDrag(const juce::MouseEvent& event) override {
        auto delta = direction == Horizontal ? event.getDistanceFromDragStartX()
                                             : event.getDistanceFromDragStartY();

        if (onResize) {
            onResize(delta);
        }
    }

    std::function<void()> onResizeStart;
    std::function<void(int)> onResize;
    std::function<void()> onContextMenu;

  private:
    Direction direction;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ResizeHandle)
};

// Container for track faders at the bottom
class SessionView::FaderContainer : public juce::Component {
  public:
    FaderContainer() {
        setInterceptsMouseClicks(false, true);
    }

    void setTrackLayout(int numTracks, const std::vector<int>& trackWidths, int separatorWidth,
                        int scrollOffset) {
        numTracks_ = numTracks;
        trackWidths_ = trackWidths;
        separatorWidth_ = separatorWidth;
        scrollOffset_ = scrollOffset;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
        // Top border
        g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
        g.fillRect(0, 0, getWidth(), 1);

        // Draw vertical separators between tracks
        int x = 0;
        for (int i = 0; i < numTracks_ && i < static_cast<int>(trackWidths_.size()); ++i) {
            x += trackWidths_[i];
            g.fillRect(x - scrollOffset_, 1, separatorWidth_, getHeight() - 1);
            x += separatorWidth_;
        }
    }

  private:
    int numTracks_ = 0;
    std::vector<int> trackWidths_;
    int separatorWidth_ = 3;
    int scrollOffset_ = 0;
};

// Container for I/O routing row (between stop buttons and fader row)
class SessionView::IOContainer : public juce::Component {
  public:
    IOContainer() {
        setInterceptsMouseClicks(true, true);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu() && onContextMenu)
            onContextMenu();
    }

    std::function<void()> onContextMenu;

    void setTrackLayout(int numTracks, const std::vector<int>& trackWidths, int separatorWidth,
                        int scrollOffset) {
        numTracks_ = numTracks;
        trackWidths_ = trackWidths;
        separatorWidth_ = separatorWidth;
        scrollOffset_ = scrollOffset;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
        g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
        g.fillRect(0, 0, getWidth(), 1);

        int x = 0;
        for (int i = 0; i < numTracks_ && i < static_cast<int>(trackWidths_.size()); ++i) {
            x += trackWidths_[i];
            g.fillRect(x - scrollOffset_, 1, separatorWidth_, getHeight() - 1);
            x += separatorWidth_;
        }
    }

  private:
    int numTracks_ = 0;
    std::vector<int> trackWidths_;
    int separatorWidth_ = 3;
    int scrollOffset_ = 0;
};

// Beat-pulse indicator band. Each track segment fades a small coloured dot
// at its own subdivision of the project beat — a quick visual sync without
// taking permanent vertical space. The rate / hide affordances flank the
// dot on each side and are hit-tested directly (no child components).
class SessionView::BeatBandContainer : public juce::Component {
  public:
    BeatBandContainer() {
        setInterceptsMouseClicks(true, false);
        rateIcon_ = magda::ManagedDrawable::create(BinaryData::rate_svg, BinaryData::rate_svgSize);
        hideIcon_ = magda::ManagedDrawable::create(BinaryData::hide_svg, BinaryData::hide_svgSize);
        // SVGs ship with #B3B3B3 fills; tint to the theme's secondary text
        // colour once so the icons read against the column BG.
        const auto tint = DarkTheme::getColour(DarkTheme::TEXT_SECONDARY);
        if (rateIcon_)
            rateIcon_->replaceColour(juce::Colour(0xFFB3B3B3), tint);
        if (hideIcon_)
            hideIcon_->replaceColour(juce::Colour(0xFFB3B3B3), tint);
    }

    void setTrackLayout(int numTracks, const std::vector<int>& trackWidths, int separatorWidth,
                        int scrollOffset) {
        numTracks_ = numTracks;
        trackWidths_ = trackWidths;
        separatorWidth_ = separatorWidth;
        scrollOffset_ = scrollOffset;
        repaint();
    }

    /** Set normalised beat phases [0, 1) per track. Length must equal numTracks. */
    void setTrackBeatPhases(std::vector<double> phases) {
        if (phases == phases_)
            return;
        phases_ = std::move(phases);
        repaint();
    }

    /** Returns the visible-track index at the given x, or -1. */
    int trackIndexAtX(int x) const {
        int cursor = -scrollOffset_;
        for (int i = 0; i < numTracks_ && i < static_cast<int>(trackWidths_.size()); ++i) {
            int w = trackWidths_[i];
            if (x >= cursor && x < cursor + w)
                return i;
            cursor += w + separatorWidth_;
        }
        return -1;
    }

    /** Hide-state predicate, polled at paint time so the container doesn't
        have to mirror SessionView's set. */
    std::function<bool(int trackIndex)> isTrackHidden;

    void mouseDown(const juce::MouseEvent& e) override {
        int trackIdx = trackIndexAtX(e.x);
        if (trackIdx < 0)
            return;

        // Icon zones flank the dot. Hit-test directly — right-click no
        // longer opens the rate menu, the rate icon is the only entry.
        // Expand the hit boxes a few pixels each side so the tiny icons are
        // still easy to land with a normal click.
        auto layout = layoutForTrack(trackIdx);
        if (layout.rateIconBounds.expanded(3, 3).contains(e.x, e.y)) {
            if (onRateIconClicked)
                onRateIconClicked(trackIdx);
            return;
        }
        if (layout.hideIconBounds.expanded(3, 3).contains(e.x, e.y)) {
            if (onHideIconClicked)
                onHideIconClicked(trackIdx);
            return;
        }
    }

    void paint(juce::Graphics& g) override {
        g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
        g.fillRect(0, 0, getWidth(), 1);

        const auto pulseColour = DarkTheme::getColour(DarkTheme::ACCENT_CYAN);
        constexpr float kDotRadius = 2.5f;

        int cursor = -scrollOffset_;
        for (int i = 0; i < numTracks_ && i < static_cast<int>(trackWidths_.size()); ++i) {
            int w = trackWidths_[i];
            auto layout = layoutForTrack(i);

            const bool hidden = isTrackHidden && isTrackHidden(i);

            if (!hidden) {
                double phase = (i < static_cast<int>(phases_.size())) ? phases_[i] : 0.0;
                float alpha = static_cast<float>(juce::jmax(0.0, 0.85 - phase * 0.85));
                g.setColour(pulseColour.withAlpha(alpha));
                g.fillEllipse(layout.dotCentre.getX() - kDotRadius,
                              layout.dotCentre.getY() - kDotRadius, kDotRadius * 2.0f,
                              kDotRadius * 2.0f);
            }

            if (hideIcon_) {
                hideIcon_->drawWithin(g, layout.hideIconBounds.toFloat(),
                                      juce::RectanglePlacement::centred, hidden ? 0.55f : 0.3f);
            }
            if (rateIcon_) {
                rateIcon_->drawWithin(g, layout.rateIconBounds.toFloat(),
                                      juce::RectanglePlacement::centred, 0.3f);
            }

            cursor += w;
            g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
            g.fillRect(cursor, 1, separatorWidth_, getHeight() - 1);
            cursor += separatorWidth_;
        }
    }

    std::function<void(int trackIndex)> onRateIconClicked;
    std::function<void(int trackIndex)> onHideIconClicked;

  private:
    struct TrackLayout {
        juce::Rectangle<int> rateIconBounds;
        juce::Rectangle<int> hideIconBounds;
        juce::Point<float> dotCentre;
    };

    /** Layout per track column:
          left half  → beat-pulse dot (vertically centred)
          right half → [hide] stacked above [rate]              */
    TrackLayout layoutForTrack(int trackIdx) const {
        TrackLayout out;
        if (trackIdx < 0 || trackIdx >= numTracks_ ||
            trackIdx >= static_cast<int>(trackWidths_.size()))
            return out;

        int cursor = -scrollOffset_;
        for (int i = 0; i < trackIdx; ++i)
            cursor += trackWidths_[i] + separatorWidth_;
        int w = trackWidths_[trackIdx];

        const int yCentre = getHeight() / 2;
        // Dot sits in the centre of the column.
        out.dotCentre = {static_cast<float>(cursor + w / 2), static_cast<float>(yCentre)};

        // Icons stacked in the right half, derived from band height.
        constexpr int kGap = 2;
        const int iconH = (getHeight() - 1 - kGap) / 2;
        constexpr int kRightPad = 3;
        const int xIcon = cursor + w - iconH - kRightPad;
        const int stackH = iconH + kGap + iconH;
        const int yTop = (getHeight() - stackH) / 2;

        out.hideIconBounds = juce::Rectangle<int>(xIcon, yTop, iconH, iconH);
        out.rateIconBounds = juce::Rectangle<int>(xIcon, yTop + iconH + kGap, iconH, iconH);
        return out;
    }

    int numTracks_ = 0;
    std::vector<int> trackWidths_;
    int separatorWidth_ = 3;
    int scrollOffset_ = 0;
    std::vector<double> phases_;
    magda::ManagedDrawable rateIcon_;
    magda::ManagedDrawable hideIcon_;
};

// Passive beat pulse in the master column. Mixer controls live in the shared
// left rail; this keeps the master corner as a timing indicator only.
class SessionView::MasterBeatIndicator : public juce::Component {
  public:
    void setBeatPhase(double phase) {
        if (phase == phase_)
            return;
        phase_ = phase;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
        g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
        g.fillRect(0, 0, getWidth(), 1);

        const float alpha = static_cast<float>(juce::jmax(0.0, 0.85 - phase_ * 0.85));
        constexpr float kDotRadius = 3.0f;
        const auto centre = getLocalBounds().toFloat().getCentre();
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_CYAN).withAlpha(alpha));
        g.fillEllipse(centre.getX() - kDotRadius, centre.getY() - kDotRadius, kDotRadius * 2.0f,
                      kDotRadius * 2.0f);
    }

  private:
    double phase_ = 1.0;
};

// Container for send section (between stop buttons and IO row)
class SessionView::SendSectionContainer : public juce::Component {
  public:
    SendSectionContainer() {
        setInterceptsMouseClicks(true, true);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu() && onContextMenu)
            onContextMenu();
    }

    std::function<void()> onContextMenu;

    void setTrackLayout(int numTracks, const std::vector<int>& trackWidths, int separatorWidth,
                        int scrollOffset) {
        numTracks_ = numTracks;
        trackWidths_ = trackWidths;
        separatorWidth_ = separatorWidth;
        scrollOffset_ = scrollOffset;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
        g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
        g.fillRect(0, 0, getWidth(), 1);

        int x = 0;
        for (int i = 0; i < numTracks_ && i < static_cast<int>(trackWidths_.size()); ++i) {
            x += trackWidths_[i];
            g.fillRect(x - scrollOffset_, 1, separatorWidth_, getHeight() - 1);
            x += separatorWidth_;
        }
    }

  private:
    int numTracks_ = 0;
    std::vector<int> trackWidths_;
    int separatorWidth_ = 3;
    int scrollOffset_ = 0;
};

// Per-track send strip (shows send rows inside a viewport)
class SessionView::MiniSendStrip : public juce::Component {
  public:
    MiniSendStrip(TrackId trackId) : trackId_(trackId) {
        setInterceptsMouseClicks(true, true);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu()) {
            showSendContextMenu();
        }
    }

    void paint(juce::Graphics& /*g*/) override {}

    void resized() override {
        layoutSlots();
    }

    void updateFromTrack() {
        const auto* track = TrackManager::getInstance().getTrack(trackId_);
        if (!track)
            return;

        bool countChanged = slots_.size() != track->sends.size();
        if (countChanged) {
            rebuildSlots(track->sends);
        } else {
            for (size_t i = 0; i < slots_.size(); ++i) {
                auto& slot = slots_[i];
                const auto& send = track->sends[i];
                if (slot.levelSlider && !slot.levelSlider->isBeingDragged())
                    slot.levelSlider->setValue(send.level, juce::dontSendNotification);
                if (slot.nameLabel && send.destTrackId != INVALID_TRACK_ID) {
                    if (auto* destTrack = TrackManager::getInstance().getTrack(send.destTrackId))
                        slot.nameLabel->setText(destTrack->name, juce::dontSendNotification);
                }
            }
        }
    }

    TrackId getTrackId() const {
        return trackId_;
    }

  private:
    static constexpr int SEND_SLOT_HEIGHT = 18;

    struct SendSlot {
        int busIndex;
        std::unique_ptr<juce::Label> nameLabel;
        std::unique_ptr<daw::ui::TextSlider> levelSlider;
        std::unique_ptr<juce::TextButton> removeButton;
    };

    TrackId trackId_;
    std::vector<SendSlot> slots_;

    void rebuildSlots(const std::vector<SendInfo>& sends) {
        for (auto& slot : slots_) {
            removeChildComponent(slot.nameLabel.get());
            removeChildComponent(slot.levelSlider.get());
            removeChildComponent(slot.removeButton.get());
        }
        slots_.clear();

        for (const auto& send : sends) {
            SendSlot slot;
            slot.busIndex = send.busIndex;

            // Dest name label
            slot.nameLabel = std::make_unique<juce::Label>();
            juce::String destName = "Bus " + juce::String(send.busIndex);
            if (send.destTrackId != INVALID_TRACK_ID) {
                if (auto* destTrack = TrackManager::getInstance().getTrack(send.destTrackId))
                    destName = destTrack->name;
            }
            slot.nameLabel->setText(destName, juce::dontSendNotification);
            slot.nameLabel->setFont(FontManager::getInstance().getUIFont(9.0f));
            slot.nameLabel->setColour(juce::Label::textColourId,
                                      DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
            slot.nameLabel->setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(*slot.nameLabel);

            // Level slider (horizontal, 0-1)
            slot.levelSlider =
                std::make_unique<daw::ui::TextSlider>(daw::ui::TextSlider::Format::Decimal);
            slot.levelSlider->setOrientation(daw::ui::TextSlider::Orientation::Horizontal);
            slot.levelSlider->setRange(0.0, 1.0, 0.01);
            slot.levelSlider->setValue(send.level, juce::dontSendNotification);
            slot.levelSlider->setFont(FontManager::getInstance().getUIFont(9.0f));
            int busIdx = send.busIndex;
            slot.levelSlider->onValueChanged = [this, busIdx](double val) {
                UndoManager::getInstance().executeCommand(std::make_unique<SetSendLevelCommand>(
                    trackId_, busIdx, static_cast<float>(val)));
            };
            addAndMakeVisible(*slot.levelSlider);

            // Remove button
            slot.removeButton = std::make_unique<juce::TextButton>("x");
            slot.removeButton->setConnectedEdges(
                juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
                juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
            slot.removeButton->setColour(juce::TextButton::buttonColourId,
                                         DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
            slot.removeButton->setColour(juce::TextButton::textColourOffId,
                                         DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
            slot.removeButton->onClick = [this, busIdx]() {
                UndoManager::getInstance().executeCommand(
                    std::make_unique<RemoveSendCommand>(trackId_, busIdx));
            };
            addAndMakeVisible(*slot.removeButton);

            slots_.push_back(std::move(slot));
        }

        layoutSlots();
    }

    void layoutSlots() {
        int w = getWidth();
        int y = 0;
        for (auto& slot : slots_) {
            auto row = juce::Rectangle<int>(0, y, w, SEND_SLOT_HEIGHT);
            slot.nameLabel->setBounds(row.removeFromLeft(row.getWidth() * 40 / 100));
            auto removeArea = row.removeFromRight(16);
            slot.removeButton->setBounds(removeArea);
            slot.levelSlider->setBounds(row);
            y += SEND_SLOT_HEIGHT + 1;
        }
        int totalH = juce::jmax(1, y);
        if (getHeight() != totalH)
            setSize(getWidth(), totalH);
    }

    void showSendContextMenu() {
        juce::PopupMenu menu;

        // Add Send submenu
        juce::PopupMenu sendSubMenu;
        const auto& tracks = TrackManager::getInstance().getTracks();
        std::set<TrackId> existingSendDests;
        if (auto* thisTrack = TrackManager::getInstance().getTrack(trackId_)) {
            for (const auto& send : thisTrack->sends)
                existingSendDests.insert(send.destTrackId);
        }
        for (const auto& t : tracks) {
            if (t.id != trackId_ && t.type != TrackType::Master &&
                existingSendDests.find(t.id) == existingSendDests.end()) {
                sendSubMenu.addItem(1000 + t.id, t.name);
            }
        }
        if (sendSubMenu.getNumItems() == 0) {
            sendSubMenu.addItem(-1, "(No tracks available)", false);
        }
        menu.addSubMenu("Add Send", sendSubMenu);

        auto safeThis = juce::Component::SafePointer<MiniSendStrip>(this);
        menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis](int result) {
            if (safeThis && result >= 1000) {
                UndoManager::getInstance().executeCommand(std::make_unique<AddSendCommand>(
                    safeThis->trackId_, static_cast<TrackId>(result - 1000)));
            }
        });
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MiniSendStrip)
};

// Mini I/O routing strip per track (2x2 grid: AudioIn/Out, MidiIn/Out)
class SessionView::MiniIOStrip : public juce::Component {
  public:
    MiniIOStrip(TrackId trackId, AudioEngine* audioEngine)
        : trackId_(trackId), audioEngine_(audioEngine) {
        audioInSelector_ = std::make_unique<RoutingSelector>(RoutingSelector::Type::AudioIn);
        audioOutSelector_ = std::make_unique<RoutingSelector>(RoutingSelector::Type::AudioOut);
        midiInSelector_ = std::make_unique<RoutingSelector>(RoutingSelector::Type::MidiIn);
        midiOutSelector_ = std::make_unique<RoutingSelector>(RoutingSelector::Type::MidiOut);

        addAndMakeVisible(*audioInSelector_);
        addAndMakeVisible(*audioOutSelector_);
        addAndMakeVisible(*midiInSelector_);
        addAndMakeVisible(*midiOutSelector_);

        populateOptions();
        setupRoutingCallbacks();
    }

    void resized() override {
        auto bounds = getLocalBounds();
        int halfH = bounds.getHeight() / 2;
        int halfW = bounds.getWidth() / 2;

        audioInSelector_->setBounds(0, 0, halfW, halfH);
        audioOutSelector_->setBounds(halfW, 0, bounds.getWidth() - halfW, halfH);
        midiInSelector_->setBounds(0, halfH, halfW, bounds.getHeight() - halfH);
        midiOutSelector_->setBounds(halfW, halfH, bounds.getWidth() - halfW,
                                    bounds.getHeight() - halfH);
    }

    void updateFromTrack() {
        const auto* track = TrackManager::getInstance().getTrack(trackId_);
        if (!track)
            return;

        auto* deviceManager = audioEngine_ ? audioEngine_->getDeviceManager() : nullptr;
        auto* device = deviceManager ? deviceManager->getCurrentAudioDevice() : nullptr;
        auto* midiBridge = audioEngine_ ? audioEngine_->getMidiBridge() : nullptr;

        juce::BigInteger enabledInputChannels, enabledOutputChannels;
        std::map<int, juce::String> teInputDeviceNames;
        if (auto* bridge = audioEngine_->getAudioBridge()) {
            enabledInputChannels = bridge->getEnabledInputChannels();
            enabledOutputChannels = bridge->getEnabledOutputChannels();
            teInputDeviceNames = bridge->getInputDeviceNamesByChannel();
        }

        RoutingSyncHelper::syncSelectorsFromTrack(
            *track, audioInSelector_.get(), midiInSelector_.get(), audioOutSelector_.get(),
            midiOutSelector_.get(), midiBridge, device, trackId_, outputTrackMapping_,
            midiOutputTrackMapping_, &inputTrackMapping_, enabledInputChannels,
            enabledOutputChannels, nullptr, teInputDeviceNames);
    }

    TrackId getTrackId() const {
        return trackId_;
    }

  private:
    TrackId trackId_;
    AudioEngine* audioEngine_;
    std::unique_ptr<RoutingSelector> audioInSelector_;
    std::unique_ptr<RoutingSelector> audioOutSelector_;
    std::unique_ptr<RoutingSelector> midiInSelector_;
    std::unique_ptr<RoutingSelector> midiOutSelector_;
    std::map<int, TrackId> inputTrackMapping_;
    std::map<int, TrackId> outputTrackMapping_;
    std::map<int, TrackId> midiOutputTrackMapping_;

    void populateOptions() {
        if (!audioEngine_)
            return;

        auto* deviceManager = audioEngine_->getDeviceManager();
        auto* device = deviceManager ? deviceManager->getCurrentAudioDevice() : nullptr;
        auto* midiBridge = audioEngine_->getMidiBridge();

        juce::BigInteger enabledInputChannels, enabledOutputChannels;
        std::map<int, juce::String> teInputDeviceNames;
        if (auto* bridge = audioEngine_->getAudioBridge()) {
            enabledInputChannels = bridge->getEnabledInputChannels();
            enabledOutputChannels = bridge->getEnabledOutputChannels();
            teInputDeviceNames = bridge->getInputDeviceNamesByChannel();
        }

        RoutingSyncHelper::populateAudioInputOptions(audioInSelector_.get(), device, trackId_,
                                                     &inputTrackMapping_, enabledInputChannels,
                                                     nullptr, teInputDeviceNames);
        RoutingSyncHelper::populateAudioOutputOptions(audioOutSelector_.get(), trackId_, device,
                                                      outputTrackMapping_, enabledOutputChannels);
        RoutingSyncHelper::populateMidiInputOptions(midiInSelector_.get(), midiBridge);
        RoutingSyncHelper::populateMidiOutputOptions(midiOutSelector_.get(), midiBridge,
                                                     midiOutputTrackMapping_);

        // Sync current track state into selectors
        updateFromTrack();
    }

    void setupRoutingCallbacks() {
        auto* midiBridge = audioEngine_ ? audioEngine_->getMidiBridge() : nullptr;

        audioInSelector_->onEnabledChanged = [this](bool enabled) {
            if (enabled) {
                midiInSelector_->setEnabled(false);
                TrackManager::getInstance().setTrackMidiInput(trackId_, "");
                auto* trackInfo = TrackManager::getInstance().getTrack(trackId_);
                if (trackInfo && trackInfo->audioInputDevice.startsWith("track:"))
                    TrackManager::getInstance().setTrackAudioInput(trackId_,
                                                                   trackInfo->audioInputDevice);
                else
                    TrackManager::getInstance().setTrackAudioInput(trackId_, "default");
            } else {
                TrackManager::getInstance().setTrackAudioInput(trackId_, "");
            }
        };

        audioInSelector_->onSelectionChanged = [this](int selectedId) {
            if (selectedId == 1) {
                TrackManager::getInstance().setTrackAudioInput(trackId_, "");
            } else if (selectedId >= 200) {
                auto it = inputTrackMapping_.find(selectedId);
                if (it != inputTrackMapping_.end())
                    TrackManager::getInstance().setTrackAudioInput(
                        trackId_, "track:" + juce::String(it->second));
            } else if (selectedId >= 10) {
                TrackManager::getInstance().setTrackAudioInput(trackId_, "default");
            }
        };

        midiInSelector_->onEnabledChanged = [this, midiBridge](bool enabled) {
            if (enabled) {
                audioInSelector_->setEnabled(false);
                TrackManager::getInstance().setTrackAudioInput(trackId_, "");
                int selectedId = midiInSelector_->getSelectedId();
                if (selectedId == 1) {
                    TrackManager::getInstance().setTrackMidiInput(trackId_, "all");
                } else if (selectedId >= 10 && midiBridge) {
                    auto midiInputs = midiBridge->getAvailableMidiInputs();
                    int deviceIndex = selectedId - 10;
                    if (deviceIndex >= 0 && deviceIndex < static_cast<int>(midiInputs.size()))
                        TrackManager::getInstance().setTrackMidiInput(trackId_,
                                                                      midiInputs[deviceIndex].id);
                    else
                        TrackManager::getInstance().setTrackMidiInput(trackId_, "all");
                } else {
                    TrackManager::getInstance().setTrackMidiInput(trackId_, "all");
                }
            } else {
                TrackManager::getInstance().setTrackMidiInput(trackId_, "");
            }
        };

        midiInSelector_->onSelectionChanged = [this, midiBridge](int selectedId) {
            if (selectedId == 2) {
                TrackManager::getInstance().setTrackMidiInput(trackId_, "");
            } else if (selectedId == 1) {
                TrackManager::getInstance().setTrackMidiInput(trackId_, "all");
            } else if (selectedId >= 10 && midiBridge) {
                auto midiInputs = midiBridge->getAvailableMidiInputs();
                int deviceIndex = selectedId - 10;
                if (deviceIndex >= 0 && deviceIndex < static_cast<int>(midiInputs.size()))
                    TrackManager::getInstance().setTrackMidiInput(trackId_,
                                                                  midiInputs[deviceIndex].id);
            }
        };

        audioOutSelector_->onEnabledChanged = [this](bool enabled) {
            if (enabled)
                TrackManager::getInstance().setTrackAudioOutput(trackId_, "master");
            else
                TrackManager::getInstance().setTrackAudioOutput(trackId_, "");
        };

        audioOutSelector_->onSelectionChanged = [this](int selectedId) {
            if (selectedId == 1) {
                TrackManager::getInstance().setTrackAudioOutput(trackId_, "master");
            } else if (selectedId == 2) {
                TrackManager::getInstance().setTrackAudioOutput(trackId_, "");
            } else if (selectedId >= 200) {
                auto it = outputTrackMapping_.find(selectedId);
                if (it != outputTrackMapping_.end())
                    TrackManager::getInstance().setTrackAudioOutput(
                        trackId_, "track:" + juce::String(it->second));
            } else if (selectedId >= 10) {
                TrackManager::getInstance().setTrackAudioOutput(trackId_, "master");
            }
        };

        midiOutSelector_->onEnabledChanged = [this](bool enabled) {
            if (!enabled)
                TrackManager::getInstance().setTrackMidiOutput(trackId_, "");
        };

        midiOutSelector_->onSelectionChanged = [this, midiBridge](int selectedId) {
            if (selectedId == 1) {
                TrackManager::getInstance().setTrackMidiOutput(trackId_, "");
            } else if (selectedId >= 200) {
                auto it = midiOutputTrackMapping_.find(selectedId);
                if (it != midiOutputTrackMapping_.end())
                    TrackManager::getInstance().setTrackMidiOutput(
                        trackId_, "track:" + juce::String(it->second));
            } else if (selectedId >= 10 && midiBridge) {
                auto midiOutputs = midiBridge->getAvailableMidiOutputs();
                int deviceIndex = selectedId - 10;
                if (deviceIndex >= 0 && deviceIndex < static_cast<int>(midiOutputs.size()))
                    TrackManager::getInstance().setTrackMidiOutput(trackId_,
                                                                   midiOutputs[deviceIndex].id);
            }
        };
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MiniIOStrip)
};

// MiniDbScale defined in ClipSlotButton.hpp

// Mini channel strip for session view fader row
class SessionView::MiniChannelStrip : public juce::Component {
  public:
    MiniChannelStrip(TrackId trackId, const TrackInfo& track) : trackId_(trackId) {
        trackColour_ = track.colour;

        // Volume fader (vertical TextSlider with dB format)
        volumeSlider_ =
            std::make_unique<daw::ui::TextSlider>(daw::ui::TextSlider::Format::Decibels);
        volumeSlider_->setOrientation(daw::ui::TextSlider::Orientation::Vertical);
        volumeSlider_->setRange(0.0, 1.0, 0.001);
        volumeSlider_->setFont(FontManager::getInstance().getUIFont(9.0f));
        float db = gainToDb(track.volume);
        volumeSlider_->setValue(dbToMeterPos(db), juce::dontSendNotification);
        volumeSlider_->setValueFormatter([](double pos) -> juce::String {
            float db = meterPosToDb(static_cast<float>(pos));
            if (db <= MIN_DB)
                return "-inf";
            if (std::abs(db) < 0.05f)
                db = 0.0f;
            return juce::String(db, 1);
        });
        volumeSlider_->setValueParser([](const juce::String& text) -> double {
            auto t = text.trim();
            if (t.endsWithIgnoreCase("db"))
                t = t.dropLastCharacters(2).trim();
            if (t.equalsIgnoreCase("-inf") || t.equalsIgnoreCase("inf"))
                return 0.0;
            return static_cast<double>(dbToMeterPos(t.getFloatValue()));
        });
        volumeSlider_->setShowText(false);
        volumeSlider_->onValueChanged = [this](double newValue) {
            const float currentDb = meterPosToDb(static_cast<float>(newValue));
            const float currentGain = dbToGain(currentDb);
            auto& sel = SelectionManager::getInstance();
            const bool multi = sel.isTrackSelected(trackId_) && sel.getSelectedTrackCount() > 1;
            if (multi) {
                if (multiTrackBaseVolumes_.empty()) {
                    auto& tm = TrackManager::getInstance();
                    for (auto tid : sel.getSelectedTracks())
                        if (auto* t = tm.getTrack(tid))
                            multiTrackBaseVolumes_[tid] = t->volume;
                    multiTrackDragStartDb_ = currentDb;
                }
                const double deltaDb = currentDb - multiTrackDragStartDb_;
                for (auto& [tid, baseVol] : multiTrackBaseVolumes_) {
                    float baseDb = gainToDb(baseVol);
                    float newDb =
                        juce::jlimit(MIN_DB, MAX_DB, static_cast<float>(baseDb + deltaDb));
                    float newGain = dbToGain(newDb);
                    UndoManager::getInstance().executeCommand(
                        std::make_unique<SetTrackVolumeCommand>(tid, newGain));
                }
            } else {
                UndoManager::getInstance().executeCommand(
                    std::make_unique<SetTrackVolumeCommand>(trackId_, currentGain));
            }
        };
        volumeSlider_->onDragEnd = [this]() { multiTrackBaseVolumes_.clear(); };
        {
            AutomationTarget volTarget;
            volTarget.kind = ControlTarget::Kind::TrackVolume;
            volTarget.devicePath = magda::ChainNodePath::trackLevel(trackId_);
            volumeSlider_->setAutomationTarget(volTarget);
        }
        addAndMakeVisible(*volumeSlider_);

        // dB scale labels (between fader and meter)
        dbScale_ = std::make_unique<MiniDbScale>();
        addAndMakeVisible(*dbScale_);

        // Level meter
        levelMeter_ = std::make_unique<LevelMeter>();
        addAndMakeVisible(*levelMeter_);

        // Mute button (square corners, toggle)
        muteButton_ = std::make_unique<juce::TextButton>("M");
        muteButton_->setConnectedEdges(
            juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
            juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
        muteButton_->setColour(juce::TextButton::buttonColourId,
                               DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        muteButton_->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFAA8855));
        muteButton_->setColour(juce::TextButton::textColourOffId,
                               DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        muteButton_->setColour(juce::TextButton::textColourOnId,
                               DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        muteButton_->setClickingTogglesState(true);
        muteButton_->setToggleState(track.muted, juce::dontSendNotification);
        muteButton_->onClick = [this]() {
            const bool newState = muteButton_->getToggleState();
            for (auto tid : getMultiEditTargets(trackId_))
                UndoManager::getInstance().executeCommand(
                    std::make_unique<SetTrackMuteCommand>(tid, newState));
        };
        addAndMakeVisible(*muteButton_);

        // Solo button (square corners, toggle)
        soloButton_ = std::make_unique<juce::TextButton>("S");
        soloButton_->setConnectedEdges(
            juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
            juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
        soloButton_->setColour(juce::TextButton::buttonColourId,
                               DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        soloButton_->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFAAAA55));
        soloButton_->setColour(juce::TextButton::textColourOffId,
                               DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        soloButton_->setColour(juce::TextButton::textColourOnId,
                               DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        soloButton_->setClickingTogglesState(true);
        soloButton_->setToggleState(track.soloed, juce::dontSendNotification);
        soloButton_->onClick = [this]() {
            const bool newState = soloButton_->getToggleState();
            for (auto tid : getMultiEditTargets(trackId_))
                UndoManager::getInstance().executeCommand(
                    std::make_unique<SetTrackSoloCommand>(tid, newState));
        };
        addAndMakeVisible(*soloButton_);

        // Record arm button (square corners, toggle)
        recordButton_ = std::make_unique<juce::TextButton>("R");
        recordButton_->setConnectedEdges(
            juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
            juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
        recordButton_->setColour(juce::TextButton::buttonColourId,
                                 DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        recordButton_->setColour(juce::TextButton::buttonOnColourId,
                                 DarkTheme::getColour(DarkTheme::STATUS_ERROR));
        recordButton_->setColour(juce::TextButton::textColourOffId,
                                 DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        recordButton_->setColour(juce::TextButton::textColourOnId,
                                 DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        recordButton_->setClickingTogglesState(true);
        recordButton_->setToggleState(track.recordArmed, juce::dontSendNotification);
        recordButton_->onClick = [this]() {
            const bool armed = recordButton_->getToggleState();
            for (auto tid : getMultiEditTargets(trackId_))
                TrackManager::getInstance().setTrackRecordArmed(tid, armed);
        };
        addAndMakeVisible(*recordButton_);

        // Monitor button (3-state: Off → In → Auto → Off)
        monitorButton_ = std::make_unique<juce::TextButton>("-");
        monitorButton_->setConnectedEdges(
            juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
            juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
        monitorButton_->setColour(juce::TextButton::buttonColourId,
                                  DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        monitorButton_->setColour(juce::TextButton::buttonOnColourId,
                                  DarkTheme::getColour(DarkTheme::ACCENT_GREEN));
        monitorButton_->setColour(juce::TextButton::textColourOffId,
                                  DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        monitorButton_->setColour(juce::TextButton::textColourOnId,
                                  DarkTheme::getColour(DarkTheme::BACKGROUND));
        monitorButton_->setTooltip("Input monitoring (Off/In/Auto)");
        monitorButton_->onClick = [this]() {
            auto* t = TrackManager::getInstance().getTrack(trackId_);
            if (!t)
                return;
            InputMonitorMode nextMode;
            switch (t->inputMonitor) {
                case InputMonitorMode::Off:
                    nextMode = InputMonitorMode::In;
                    break;
                case InputMonitorMode::In:
                    nextMode = InputMonitorMode::Auto;
                    break;
                case InputMonitorMode::Auto:
                    nextMode = InputMonitorMode::Off;
                    break;
            }
            for (auto tid : getMultiEditTargets(trackId_))
                UndoManager::getInstance().executeCommand(
                    std::make_unique<SetTrackInputMonitorCommand>(tid, nextMode));
        };
        addAndMakeVisible(*monitorButton_);
        updateMonitorVisual(track.inputMonitor);

        // Pan slider (horizontal, compact)
        panSlider_ = std::make_unique<daw::ui::TextSlider>(daw::ui::TextSlider::Format::Pan);
        panSlider_->setOrientation(daw::ui::TextSlider::Orientation::Horizontal);
        panSlider_->setRange(-1.0, 1.0, 0.01);
        panSlider_->setFont(FontManager::getInstance().getUIFont(8.0f));
        panSlider_->setValue(track.pan, juce::dontSendNotification);
        panSlider_->onValueChanged = [this](double newValue) {
            auto& sel = SelectionManager::getInstance();
            const bool multi = sel.isTrackSelected(trackId_) && sel.getSelectedTrackCount() > 1;
            if (multi) {
                if (multiTrackBasePans_.empty()) {
                    auto& tm = TrackManager::getInstance();
                    for (auto tid : sel.getSelectedTracks())
                        if (auto* t = tm.getTrack(tid))
                            multiTrackBasePans_[tid] = t->pan;
                    multiTrackDragStartPan_ = newValue;
                }
                const double delta = newValue - multiTrackDragStartPan_;
                for (auto& [tid, basePan] : multiTrackBasePans_) {
                    float newPan = juce::jlimit(-1.0f, 1.0f, static_cast<float>(basePan + delta));
                    UndoManager::getInstance().executeCommand(
                        std::make_unique<SetTrackPanCommand>(tid, newPan));
                }
            } else {
                UndoManager::getInstance().executeCommand(
                    std::make_unique<SetTrackPanCommand>(trackId_, static_cast<float>(newValue)));
            }
        };
        panSlider_->onDragEnd = [this]() { multiTrackBasePans_.clear(); };
        {
            AutomationTarget panTarget;
            panTarget.kind = ControlTarget::Kind::TrackPan;
            panTarget.devicePath = magda::ChainNodePath::trackLevel(trackId_);
            panSlider_->setAutomationTarget(panTarget);
        }
        addAndMakeVisible(*panSlider_);

        // Listen for mouse events on all children so we can intercept right-clicks
        volumeSlider_->addMouseListener(this, false);
        dbScale_->addMouseListener(this, false);
        levelMeter_->addMouseListener(this, false);
        muteButton_->addMouseListener(this, false);
        soloButton_->addMouseListener(this, false);
        recordButton_->addMouseListener(this, false);
        monitorButton_->addMouseListener(this, false);
        panSlider_->addMouseListener(this, false);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu() && onContextMenu)
            onContextMenu();
    }

    std::function<void()> onContextMenu;

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();

        // Track colour bar at top (3px)
        g.setColour(trackColour_);
        g.fillRect(bounds.removeFromTop(3));
    }

    void setShowRecordMonitor(bool show) {
        recordButton_->setVisible(show);
        monitorButton_->setVisible(show);
        resized();
    }

    void resized() override {
        auto bounds = getLocalBounds();
        bounds.removeFromTop(3);  // colour bar

        // Button rows at bottom
        auto msRow = bounds.removeFromBottom(18);
        int halfW = msRow.getWidth() / 2;
        muteButton_->setBounds(msRow.removeFromLeft(halfW));
        soloButton_->setBounds(msRow);

        if (recordButton_->isVisible()) {
            auto rmRow = bounds.removeFromBottom(18);
            halfW = rmRow.getWidth() / 2;
            recordButton_->setBounds(rmRow.removeFromLeft(halfW));
            monitorButton_->setBounds(rmRow);
        }

        auto panRow = bounds.removeFromBottom(14);
        panSlider_->setBounds(panRow);

        // Layout mirrors MixerView: meter and fader share the same bounds, with
        // the fader thumb drawn above the peak meter and dB labels on the right.
        static constexpr int DB_SCALE_WIDTH = 18;
        static constexpr int SCALE_GAP = 2;
        if (bounds.getWidth() > DB_SCALE_WIDTH + SCALE_GAP + 20) {
            auto scaleBounds = bounds.removeFromRight(DB_SCALE_WIDTH);
            bounds.removeFromRight(SCALE_GAP);
            dbScale_->setBounds(scaleBounds.withTrimmedTop(2).withTrimmedBottom(2));
            dbScale_->setVisible(true);
        } else {
            dbScale_->setVisible(false);
        }

        auto faderBounds = bounds.reduced(1, 2);
        levelMeter_->setBounds(faderBounds);
        volumeSlider_->setBounds(faderBounds);
        volumeSlider_->toFront(false);
    }

    void setMeterLevels(float left, float right) {
        levelMeter_->setLevels(left, right);
    }

    void updateFromTrack(const TrackInfo& track) {
        float db = gainToDb(track.volume);
        volumeSlider_->setValue(dbToMeterPos(db), juce::dontSendNotification);
        panSlider_->setValue(track.pan, juce::dontSendNotification);
        muteButton_->setToggleState(track.muted, juce::dontSendNotification);
        soloButton_->setToggleState(track.soloed, juce::dontSendNotification);
        recordButton_->setToggleState(track.recordArmed, juce::dontSendNotification);
        updateMonitorVisual(track.inputMonitor);
        trackColour_ = track.colour;
        repaint();
    }

    TrackId getTrackId() const {
        return trackId_;
    }

  private:
    TrackId trackId_;
    juce::Colour trackColour_;
    std::unique_ptr<daw::ui::TextSlider> volumeSlider_;
    std::unique_ptr<daw::ui::TextSlider> panSlider_;
    std::unique_ptr<MiniDbScale> dbScale_;
    std::unique_ptr<LevelMeter> levelMeter_;
    std::unique_ptr<juce::TextButton> muteButton_;
    std::unique_ptr<juce::TextButton> soloButton_;
    std::unique_ptr<juce::TextButton> recordButton_;
    std::unique_ptr<juce::TextButton> monitorButton_;

    // Multi-track relative drag state for volume/pan (see ChannelStrip).
    std::unordered_map<TrackId, float> multiTrackBaseVolumes_;
    std::unordered_map<TrackId, float> multiTrackBasePans_;
    double multiTrackDragStartDb_ = 0.0;
    double multiTrackDragStartPan_ = 0.0;

    void updateMonitorVisual(InputMonitorMode mode) {
        switch (mode) {
            case InputMonitorMode::Off:
                monitorButton_->setButtonText("-");
                break;
            case InputMonitorMode::In:
                monitorButton_->setButtonText("I");
                break;
            case InputMonitorMode::Auto:
                monitorButton_->setButtonText("A");
                break;
        }
        monitorButton_->setToggleState(mode != InputMonitorMode::Off, juce::dontSendNotification);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MiniChannelStrip)
};

// Mini master strip for session view (TextSlider + LevelMeter, orange accent)
class SessionView::MiniMasterStrip : public juce::Component {
  public:
    MiniMasterStrip() {
        volumeSlider_ =
            std::make_unique<daw::ui::TextSlider>(daw::ui::TextSlider::Format::Decibels);
        volumeSlider_->setOrientation(daw::ui::TextSlider::Orientation::Vertical);
        volumeSlider_->setRange(0.0, 1.0, 0.001);
        volumeSlider_->setFont(FontManager::getInstance().getUIFont(9.0f));

        const auto& master = TrackManager::getInstance().getMasterChannel();
        float db = gainToDb(master.volume);
        volumeSlider_->setValue(dbToMeterPos(db), juce::dontSendNotification);
        volumeSlider_->setValueFormatter([](double pos) -> juce::String {
            float db = meterPosToDb(static_cast<float>(pos));
            if (db <= MIN_DB)
                return "-inf";
            if (std::abs(db) < 0.05f)
                db = 0.0f;
            return juce::String(db, 1);
        });
        volumeSlider_->setValueParser([](const juce::String& text) -> double {
            auto t = text.trim();
            if (t.endsWithIgnoreCase("db"))
                t = t.dropLastCharacters(2).trim();
            if (t.equalsIgnoreCase("-inf") || t.equalsIgnoreCase("inf"))
                return 0.0;
            return static_cast<double>(dbToMeterPos(t.getFloatValue()));
        });
        volumeSlider_->setShowText(false);

        volumeSlider_->onValueChanged = [](double newValue) {
            float gain = dbToGain(meterPosToDb(static_cast<float>(newValue)));
            UndoManager::getInstance().executeCommand(
                std::make_unique<SetMasterVolumeCommand>(gain));
        };
        addAndMakeVisible(*volumeSlider_);

        levelMeter_ = std::make_unique<LevelMeter>();
        addAndMakeVisible(*levelMeter_);

        dbScale_ = std::make_unique<MiniDbScale>();
        addAndMakeVisible(*dbScale_);

        // Listen for mouse events on children for right-click context menu
        volumeSlider_->addMouseListener(this, false);
        levelMeter_->addMouseListener(this, false);
        dbScale_->addMouseListener(this, false);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu() && onContextMenu)
            onContextMenu();
        else
            SelectionManager::getInstance().selectTrack(MASTER_TRACK_ID);
    }

    std::function<void()> onContextMenu;

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();
        // Orange accent bar at top
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
        g.fillRect(bounds.removeFromTop(3));
    }

    void resized() override {
        auto bounds = getLocalBounds();
        bounds.removeFromTop(3);

        static constexpr int DB_SCALE_WIDTH = 18;
        static constexpr int SCALE_GAP = 2;
        if (bounds.getWidth() > DB_SCALE_WIDTH + SCALE_GAP + 20) {
            auto scaleBounds = bounds.removeFromRight(DB_SCALE_WIDTH);
            bounds.removeFromRight(SCALE_GAP);
            dbScale_->setBounds(scaleBounds.withTrimmedTop(2).withTrimmedBottom(2));
            dbScale_->setVisible(true);
        } else {
            dbScale_->setVisible(false);
        }

        auto faderBounds = bounds.reduced(1, 2);
        levelMeter_->setBounds(faderBounds);
        volumeSlider_->setBounds(faderBounds);
        volumeSlider_->toFront(false);
    }

    void updateVolume(float volume) {
        float db = gainToDb(volume);
        volumeSlider_->setValue(dbToMeterPos(db), juce::dontSendNotification);
    }

    void setMeterLevels(float left, float right) {
        levelMeter_->setLevels(left, right);
    }

  private:
    std::unique_ptr<daw::ui::TextSlider> volumeSlider_;
    std::unique_ptr<MiniDbScale> dbScale_;
    std::unique_ptr<LevelMeter> levelMeter_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MiniMasterStrip)
};

SessionView::SessionView() {
    // Get current view mode
    currentViewMode_ = ViewModeController::getInstance().getViewMode();
    syncMixerVisibilityFromConfig();

    toggleRail_ = std::make_unique<SessionToggleRail>();
    toggleRail_->onToggleChanged = [this]() {
        syncMixerVisibilityFromConfig();
        resized();
    };
    addAndMakeVisible(*toggleRail_);

    // Create header container for clipping
    headerContainer = std::make_unique<HeaderContainer>();
    headerContainer->onPaintOverChildren = [this](juce::Graphics& g) {
        paintHeaderDragFeedback(g);
    };
    addAndMakeVisible(*headerContainer);

    // Create scene container for clipping
    sceneContainer = std::make_unique<SceneContainer>();
    addAndMakeVisible(*sceneContainer);

    // Create viewport for scrollable grid with custom grid content
    gridContent = std::make_unique<GridContent>(CLIP_SLOT_HEIGHT, TRACK_SEPARATOR_WIDTH,
                                                CLIP_SLOT_MARGIN, numScenes_);
    gridContent->onContextMenu = [this]() {
        juce::PopupMenu menu;
        menu.addItem(1, "Add Scene");
        menu.addItem(2, "Remove Scene");
        auto safeThis = juce::Component::SafePointer<SessionView>(this);
        menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis](int result) {
            if (!safeThis)
                return;
            if (result == 1)
                safeThis->addScene();
            else if (result == 2)
                safeThis->removeScene();
        });
    };
    gridViewport = std::make_unique<GridViewport>();
    gridViewport->onContextMenu = [this]() {
        juce::PopupMenu menu;
        menu.addItem(1, "Add Scene");
        menu.addItem(2, "Remove Scene");
        auto safeThis = juce::Component::SafePointer<SessionView>(this);
        menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis](int result) {
            if (!safeThis)
                return;
            if (result == 1)
                safeThis->addScene();
            else if (result == 2)
                safeThis->removeScene();
        });
    };
    gridViewport->setViewedComponent(gridContent.get(), false);
    gridViewport->setScrollBarsShown(true, true);
    gridViewport->getHorizontalScrollBar().addListener(this);
    gridViewport->getVerticalScrollBar().addListener(this);
    addAndMakeVisible(*gridViewport);

    // Create I/O routing container (between grid and faders, hidden by default)
    ioContainer_ = std::make_unique<IOContainer>();
    ioContainer_->onContextMenu = [this]() { showMixerContextMenu(); };
    addChildComponent(*ioContainer_);

    // Beat indicator band — sits in the toggles strip above the faders.
    beatBandContainer_ = std::make_unique<BeatBandContainer>();
    auto resolveTrack = [this](int trackIdx) -> TrackId {
        if (trackIdx < 0 || trackIdx >= static_cast<int>(visibleTrackIds_.size()))
            return INVALID_TRACK_ID;
        return visibleTrackIds_[trackIdx];
    };
    beatBandContainer_->onRateIconClicked = [this, resolveTrack](int trackIdx) {
        TrackId t = resolveTrack(trackIdx);
        if (t != INVALID_TRACK_ID)
            showBeatRateMenuFor(t);
    };
    beatBandContainer_->onHideIconClicked = [this, resolveTrack](int trackIdx) {
        TrackId t = resolveTrack(trackIdx);
        if (t != INVALID_TRACK_ID)
            toggleBeatHidden(t);
    };
    beatBandContainer_->isTrackHidden = [this, resolveTrack](int trackIdx) {
        TrackId t = resolveTrack(trackIdx);
        return t != INVALID_TRACK_ID && isBeatHidden(t);
    };
    addAndMakeVisible(*beatBandContainer_);

    masterBeatIndicator_ = std::make_unique<MasterBeatIndicator>();
    addAndMakeVisible(*masterBeatIndicator_);

    // Create send section container (between stop buttons and IO row, hidden by default)
    sendSectionContainer_ = std::make_unique<SendSectionContainer>();
    sendSectionContainer_->onContextMenu = [this]() { showMixerContextMenu(); };
    addChildComponent(*sendSectionContainer_);

    // Create send resize handle (top edge of send section)
    sendResizeHandle_ = std::make_unique<ResizeHandle>(ResizeHandle::Vertical);
    sendResizeHandle_->onResizeStart = [this]() { dragStartSendHeight_ = sendSectionHeight_; };
    sendResizeHandle_->onResize = [this](int delta) {
        sendSectionHeight_ = juce::jlimit(MIN_SEND_SECTION_HEIGHT, MAX_SEND_SECTION_HEIGHT,
                                          dragStartSendHeight_ - delta);
        resized();
    };
    sendResizeHandle_->onContextMenu = [this]() { showMixerContextMenu(); };
    addChildComponent(*sendResizeHandle_);

    // Create fader container at the bottom
    faderContainer = std::make_unique<FaderContainer>();
    addAndMakeVisible(*faderContainer);

    // Create resize handle between stop button row and fader row
    faderResizeHandle_ = std::make_unique<ResizeHandle>(ResizeHandle::Vertical);
    faderResizeHandle_->onResizeStart = [this]() { dragStartFaderHeight_ = faderRowHeight_; };
    faderResizeHandle_->onResize = [this](int delta) {
        faderRowHeight_ =
            juce::jlimit(MIN_FADER_ROW_HEIGHT, MAX_FADER_ROW_HEIGHT, dragStartFaderHeight_ - delta);
        resized();
    };
    faderResizeHandle_->onContextMenu = [this]() { showMixerContextMenu(); };
    addAndMakeVisible(*faderResizeHandle_);

    setupSceneButtons();

    // Master label (top-right corner, above scene buttons)
    masterLabel_ =
        std::make_unique<juce::TextButton>(magda::technicalText(magda::TechnicalTextToken::Master));
    masterLabel_->setColour(juce::TextButton::buttonColourId,
                            DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
    masterLabel_->setColour(juce::TextButton::textColourOffId,
                            DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    masterLabel_->setLookAndFeel(&daw::ui::SmallButtonLookAndFeel::getInstance());
    masterLabel_->onClick = []() { SelectionManager::getInstance().selectTrack(MASTER_TRACK_ID); };
    addAndMakeVisible(*masterLabel_);

    // Create master strip in the fader row (scene column area)
    masterStrip_ = std::make_unique<MiniMasterStrip>();
    masterStrip_->onContextMenu = [this]() { showMixerContextMenu(); };
    addAndMakeVisible(*masterStrip_);

    // Create drag ghost label for file drag preview (added to grid content)
    dragGhostLabel_ = std::make_unique<juce::Label>();
    dragGhostLabel_->setFont(FontManager::getInstance().getUIFontBold(11.0f));
    dragGhostLabel_->setJustificationType(juce::Justification::centred);
    dragGhostLabel_->setColour(juce::Label::backgroundColourId,
                               DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.6f));
    dragGhostLabel_->setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    dragGhostLabel_->setColour(juce::Label::outlineColourId,
                               DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    dragGhostLabel_->setVisible(false);
    gridContent->addAndMakeVisible(*dragGhostLabel_);

    // Register as TrackManager listener
    TrackManager::getInstance().addListener(this);

    // Register as ClipManager listener
    ClipManager::getInstance().addListener(this);

    // Register as SelectionManager listener so multi-selected headers light up
    SelectionManager::getInstance().addListener(this);

    // Register as ViewModeController listener
    ViewModeController::getInstance().addListener(this);

    // Build tracks from TrackManager
    rebuildTracks();
}

SessionView::~SessionView() {
    if (audioEngine_) {
        if (auto* mb = audioEngine_->getMidiBridge())
            mb->removeMidiDeviceListListener(this);
    }
    stopTimer();
    TrackManager::getInstance().removeListener(this);
    ClipManager::getInstance().removeListener(this);
    SelectionManager::getInstance().removeListener(this);
    ViewModeController::getInstance().removeListener(this);
    gridViewport->getHorizontalScrollBar().removeListener(this);
    gridViewport->getVerticalScrollBar().removeListener(this);
}

void SessionView::tracksChanged() {
    DBG("SessionView::tracksChanged oldVisibleTracks=" << formatTrackIds(visibleTrackIds_)
                                                       << " sessionClips=" << formatSessionClips());
    rebuildTracks();
}

void SessionView::trackPropertyChanged(int trackId) {
    // Find the track in our visible list
    const auto* track = TrackManager::getInstance().getTrack(trackId);
    if (!track)
        return;

    // Find index in visible track IDs
    int index = -1;
    for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
        if (visibleTrackIds_[i] == trackId) {
            index = static_cast<int>(i);
            break;
        }
    }

    if (index >= 0 && index < static_cast<int>(trackHeaders.size())) {
        // Update header text with collapse indicator for groups
        juce::String headerText = track->name;
        if (track->isGroup()) {
            bool collapsed = track->isCollapsedIn(currentViewMode_);
            headerText = (collapsed ? juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6 "))
                                    : juce::String(juce::CharPointer_UTF8("\xe2\x96\xbc "))) +
                         track->name;
        }
        trackHeaders[index]->setButtonText(headerText);

        // Sync strip state (volume, mute, solo, colour)
        if (index < static_cast<int>(trackMiniStrips_.size())) {
            trackMiniStrips_[index]->updateFromTrack(*track);
        }

        // Sync IO strip routing state
        if (index < static_cast<int>(trackIOStrips_.size())) {
            trackIOStrips_[index]->updateFromTrack();
        }

        // Sync send strip state
        if (index < static_cast<int>(trackSendStrips_.size())) {
            trackSendStrips_[index]->updateFromTrack();
        }

        // Refresh clip slots for this track so the empty-slot record/stop glyph
        // tracks the track's record-arm state.
        if (index < static_cast<int>(clipSlots.size())) {
            int numSlots = static_cast<int>(clipSlots[index].size());
            for (int sceneIndex = 0; sceneIndex < numSlots; ++sceneIndex) {
                updateClipSlotAppearance(index, sceneIndex);
            }
        }
    }
}

void SessionView::trackDevicesChanged(TrackId trackId) {
    // Sends are notified via trackDevicesChanged — forward to trackPropertyChanged
    trackPropertyChanged(trackId);
}

void SessionView::viewModeChanged(ViewMode mode, const AudioEngineProfile& /*profile*/) {
    currentViewMode_ = mode;
    rebuildTracks();
}

void SessionView::masterChannelChanged() {
    // Update master strip from master channel state
    if (masterStrip_) {
        const auto& master = TrackManager::getInstance().getMasterChannel();
        masterStrip_->updateVolume(master.volume);
    }
}

int SessionView::getTrackX(int trackIndex) const {
    int x = 0;
    for (int i = 0; i < trackIndex && i < static_cast<int>(trackColumnWidths_.size()); ++i) {
        x += trackColumnWidths_[i] + TRACK_SEPARATOR_WIDTH;
    }
    return x;
}

int SessionView::getTotalTracksWidth() const {
    int total = 0;
    for (size_t i = 0; i < trackColumnWidths_.size(); ++i) {
        total += trackColumnWidths_[i];
        if (i < trackColumnWidths_.size() - 1)
            total += TRACK_SEPARATOR_WIDTH;
    }
    // Add final separator if there are tracks
    if (!trackColumnWidths_.empty())
        total += TRACK_SEPARATOR_WIDTH;
    return total;
}

int SessionView::getTrackIndexAtX(int x) const {
    int cumX = 0;
    for (int i = 0; i < static_cast<int>(trackColumnWidths_.size()); ++i) {
        cumX += trackColumnWidths_[i] + TRACK_SEPARATOR_WIDTH;
        if (x < cumX)
            return i;
    }
    return -1;
}

void SessionView::rebuildTracks() {
    DBG("SessionView::rebuildTracks begin oldVisibleTracks="
        << formatTrackIds(visibleTrackIds_) << " gridChildren="
        << gridContent->getNumChildComponents() << " sessionClips=" << formatSessionClips());

    // Clear existing track headers, clip slots, strips, IO strips, and send strips
    trackHeaders.clear();
    clipSlots.clear();
    trackMiniStrips_.clear();
    trackIOStrips_.clear();
    trackSendViewports_.clear();
    trackSendStrips_.clear();
    trackResizeHandles_.clear();
    visibleTrackIds_.clear();

    auto& trackManager = TrackManager::getInstance();

    // Build hierarchical list of visible track IDs (respecting collapse state)
    std::function<void(TrackId)> addTrackRecursive = [&](TrackId trackId) {
        const auto* track = trackManager.getTrack(trackId);
        if (!track || !track->isVisibleIn(currentViewMode_))
            return;

        visibleTrackIds_.push_back(trackId);

        // Add children if group is not collapsed
        if (track->isGroup() && !track->isCollapsedIn(currentViewMode_)) {
            for (auto childId : track->childIds) {
                addTrackRecursive(childId);
            }
        }
    };

    // Start with visible top-level tracks
    auto topLevelTracks = trackManager.getVisibleTopLevelTracks(currentViewMode_);
    for (auto trackId : topLevelTracks) {
        addTrackRecursive(trackId);
    }

    int numTracks = static_cast<int>(visibleTrackIds_.size());
    DBG("SessionView::rebuildTracks visibleTracks=" << formatTrackIds(visibleTrackIds_)
                                                    << " sessionClips=" << formatSessionClips());

    // Initialize per-track widths (preserve existing widths where possible)
    std::vector<int> oldWidths = trackColumnWidths_;
    trackColumnWidths_.resize(numTracks, DEFAULT_CLIP_SLOT_WIDTH);
    for (int i = 0; i < numTracks && i < static_cast<int>(oldWidths.size()); ++i) {
        trackColumnWidths_[i] = oldWidths[i];
    }

    // Create per-track resize handles
    trackResizeHandles_.clear();
    for (int i = 0; i < numTracks; ++i) {
        auto handle = std::make_unique<ResizeHandle>(ResizeHandle::Horizontal);
        int trackIdx = i;
        handle->onResizeStart = [this, trackIdx]() {
            dragStartTrackWidth_ = trackColumnWidths_[trackIdx];
        };
        handle->onResize = [this, trackIdx](int delta) {
            trackColumnWidths_[trackIdx] =
                juce::jlimit(MIN_TRACK_WIDTH, MAX_TRACK_WIDTH, dragStartTrackWidth_ + delta);
            resized();
        };
        headerContainer->addAndMakeVisible(*handle);
        trackResizeHandles_.push_back(std::move(handle));
    }

    // Update grid content track count
    gridContent->setNumTracks(numTracks);

    // Create track headers for visible tracks only
    for (int i = 0; i < numTracks; ++i) {
        const auto* track = trackManager.getTrack(visibleTrackIds_[i]);
        if (!track)
            continue;

        auto header = std::make_unique<TrackHeaderButton>();

        // Show collapse indicator for groups
        juce::String headerText = track->name;
        if (track->isGroup()) {
            bool collapsed = track->isCollapsedIn(currentViewMode_);
            headerText = (collapsed ? juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6 "))   // ▶
                                    : juce::String(juce::CharPointer_UTF8("\xe2\x96\xbc ")))  // ▼
                         + track->name;
        }
        header->setColour(juce::TextButton::buttonColourId, track->colour.withAlpha(0.5f));

        header->setButtonText(headerText);
        header->setColour(juce::TextButton::textColourOffId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        header->setLookAndFeel(&daw::ui::SmallButtonLookAndFeel::getInstance());

        // Click handler - select track and toggle collapse for groups.
        // Modifier-aware: Cmd toggles in/out of the multi-selection,
        // Shift range-selects from the anchor. Modifiers are captured in
        // onHeaderMouseDown because juce::TextButton::onClick doesn't
        // surface them on its own.
        TrackId trackId = track->id;
        header->onClick = [this, trackId]() {
            auto& sel = SelectionManager::getInstance();
            if (lastHeaderClickCmd_) {
                sel.toggleTrackSelection(trackId);
                return;
            }
            if (lastHeaderClickShift_) {
                TrackId anchor = sel.getAnchorTrack();
                const auto& tracks = TrackManager::getInstance().getTracks();
                int anchorIdx = -1, clickedIdx = -1;
                for (size_t k = 0; k < tracks.size(); ++k) {
                    if (tracks[k].id == anchor)
                        anchorIdx = static_cast<int>(k);
                    if (tracks[k].id == trackId)
                        clickedIdx = static_cast<int>(k);
                }
                if (anchorIdx >= 0 && clickedIdx >= 0) {
                    int lo = std::min(anchorIdx, clickedIdx);
                    int hi = std::max(anchorIdx, clickedIdx);
                    std::unordered_set<TrackId> rangeIds;
                    for (int k = lo; k <= hi; ++k)
                        rangeIds.insert(tracks[k].id);
                    sel.selectTracks(rangeIds);
                    return;
                }
                // Fall through to single select if we can't form a range
            }
            selectTrack(trackId);

            // Additionally toggle collapse for groups
            const auto* t = TrackManager::getInstance().getTrack(trackId);
            if (t && t->isGroup()) {
                bool collapsed = t->isCollapsedIn(currentViewMode_);
                TrackManager::getInstance().setTrackCollapsed(trackId, !collapsed);
            }
        };

        header->onDeleteTrack = [trackId]() {
            UndoManager::getInstance().executeCommand(
                std::make_unique<DeleteTrackCommand>(trackId));
        };
        header->canGroupSelectedTracks = [trackId]() {
            auto& sel = SelectionManager::getInstance();
            return sel.getSelectedTrackCount() >= 2 && sel.isTrackSelected(trackId);
        };
        header->onGroupSelectedTracks = []() {
            auto& sel = SelectionManager::getInstance();
            std::vector<TrackId> selectedTracks(sel.getSelectedTracks().begin(),
                                                sel.getSelectedTracks().end());
            auto cmd = std::make_unique<GroupTracksCommand>(selectedTracks, "Group");
            auto* cmdPtr = cmd.get();
            UndoManager::getInstance().executeCommand(std::move(cmd));
            TrackId groupId = cmdPtr->getCreatedGroupId();
            if (groupId != INVALID_TRACK_ID)
                sel.selectTrack(groupId);
        };
        header->canUngroupTracks = [trackId]() {
            const auto* track = TrackManager::getInstance().getTrack(trackId);
            return track != nullptr && track->isGroup() && !track->childIds.empty();
        };
        header->onUngroupTracks = [trackId]() {
            auto cmd = std::make_unique<UngroupTrackCommand>(trackId);
            auto childIds = cmd->getChildren();
            UndoManager::getInstance().executeCommand(std::move(cmd));
            if (!childIds.empty()) {
                std::unordered_set<TrackId> selectedChildren(childIds.begin(), childIds.end());
                SelectionManager::getInstance().selectTracks(selectedChildren);
            }
        };

        int headerIdx = i;
        header->onHeaderMouseDown = [this, headerIdx](const juce::MouseEvent& e) {
            // Capture modifier state for the upcoming onClick (TextButton
            // doesn't pass modifiers through to its click callback).
            lastHeaderClickCmd_ = e.mods.isCommandDown();
            lastHeaderClickShift_ = e.mods.isShiftDown();
            headerDragIndex_ = headerIdx;
            headerDragStartX_ = getTrackX(headerIdx) + trackColumnWidths_[headerIdx] / 2;
        };
        header->onHeaderMouseDrag = [this](const juce::MouseEvent& e) {
            if (headerDragIndex_ < 0)
                return;
            auto localE = e.getEventRelativeTo(headerContainer.get());
            int dx = std::abs(localE.x - (headerDragStartX_ - trackHeaderScrollOffset));
            if (!headerIsDragging_ && dx > HEADER_DRAG_THRESHOLD)
                headerIsDragging_ = true;
            if (headerIsDragging_) {
                calculateHeaderDropTarget(localE.x + trackHeaderScrollOffset);
                headerContainer->repaint();
            }
        };
        header->onHeaderMouseUp = [this](const juce::MouseEvent&) {
            if (headerIsDragging_)
                executeHeaderDrop();
            resetHeaderDragState();
        };

        headerContainer->addAndMakeVisible(*header);
        trackHeaders.push_back(std::move(header));
    }

    // Create clip slots for each visible track
    for (int track = 0; track < numTracks; ++track) {
        std::vector<std::unique_ptr<juce::TextButton>> trackSlots;
        TrackId slotTrackId = visibleTrackIds_[track];
        const auto* slotTrack = trackManager.getTrack(slotTrackId);
        bool isGroup = slotTrack && slotTrack->isGroup();

        for (int scene = 0; scene < numScenes_; ++scene) {
            auto slot = std::make_unique<ClipSlotButton>();

            slot->setButtonText("");
            slot->isGroupSlot = isGroup;
            slot->setColour(juce::TextButton::buttonColourId,
                            DarkTheme::getColour(DarkTheme::SURFACE));
            slot->setColour(juce::TextButton::textColourOffId,
                            DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));

            wireClipSlotCallbacks(*slot, track, scene);

            gridContent->addAndMakeVisible(*slot);
            trackSlots.push_back(std::move(slot));
        }

        clipSlots.push_back(std::move(trackSlots));
    }

    // Create mini channel strips (TextSlider + LevelMeter + M/S buttons)
    for (int i = 0; i < numTracks; ++i) {
        TrackId trackId = visibleTrackIds_[i];
        const auto* track = trackManager.getTrack(trackId);
        if (!track)
            continue;

        auto strip = std::make_unique<MiniChannelStrip>(trackId, *track);
        strip->onContextMenu = [this]() { showMixerContextMenu(); };
        strip->setShowRecordMonitor(recordMonitorVisible_);
        faderContainer->addAndMakeVisible(*strip);
        trackMiniStrips_.push_back(std::move(strip));
    }

    // Create mini I/O routing strips per track
    for (int i = 0; i < numTracks; ++i) {
        TrackId trackId = visibleTrackIds_[i];
        auto ioStrip = std::make_unique<MiniIOStrip>(trackId, audioEngine_);
        ioContainer_->addAndMakeVisible(*ioStrip);
        trackIOStrips_.push_back(std::move(ioStrip));
    }

    // Create mini send strips per track (each in a viewport for scrolling)
    for (int i = 0; i < numTracks; ++i) {
        TrackId trackId = visibleTrackIds_[i];
        auto strip = std::make_unique<MiniSendStrip>(trackId);
        auto viewport = std::make_unique<juce::Viewport>();
        viewport->setViewedComponent(strip.get(), false);
        viewport->setScrollBarsShown(true, false);
        sendSectionContainer_->addAndMakeVisible(*viewport);
        strip->updateFromTrack();
        trackSendViewports_.push_back(std::move(viewport));
        trackSendStrips_.push_back(std::move(strip));
    }

    resized();
    updateHeaderSelectionVisuals();

    // Populate all clip slots with their current clip data
    updateAllClipSlots();

    DBG("SessionView::rebuildTracks end visibleTracks="
        << formatTrackIds(visibleTrackIds_) << " gridChildren="
        << gridContent->getNumChildComponents() << " sessionClips=" << formatSessionClips());
}

void SessionView::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));
}

void SessionView::paintOverChildren(juce::Graphics& g) {
    g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));

    // Vertical separator on left edge of scene column
    auto sceneBounds = sceneContainer->getBounds();
    g.fillRect(sceneBounds.getX() - 1, 0, 1, getHeight());

    // Plugin drag overlay
    if (showPluginDropOverlay_) {
        if (pluginDropTrackIndex_ >= 0 &&
            pluginDropTrackIndex_ < static_cast<int>(visibleTrackIds_.size())) {
            // Highlight the specific track column
            auto vpBounds = gridViewport->getBounds();
            int trackX =
                vpBounds.getX() + getTrackX(pluginDropTrackIndex_) - trackHeaderScrollOffset;
            int trackW = (pluginDropTrackIndex_ < static_cast<int>(trackColumnWidths_.size()))
                             ? trackColumnWidths_[pluginDropTrackIndex_]
                             : DEFAULT_CLIP_SLOT_WIDTH;
            auto colBounds = juce::Rectangle<int>(trackX, 0, trackW, vpBounds.getBottom());
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.2f));
            g.fillRect(colBounds);
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.5f));
            g.drawRect(colBounds, 2);
        } else {
            // Past last track — show "new track" indicator
            auto vpBounds = gridViewport->getBounds();
            int lastTrackEnd = vpBounds.getX() + getTotalTracksWidth() - trackHeaderScrollOffset;
            int indicatorW = DEFAULT_CLIP_SLOT_WIDTH;
            auto indicatorBounds =
                juce::Rectangle<int>(lastTrackEnd, 0, indicatorW, vpBounds.getBottom());
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.12f));
            g.fillRect(indicatorBounds);
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.35f));
            g.drawRect(indicatorBounds, 2);

            // Draw "+" icon
            auto centre = indicatorBounds.getCentre().toFloat();
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.6f));
            g.drawLine(centre.getX() - 8, centre.getY(), centre.getX() + 8, centre.getY(), 2.0f);
            g.drawLine(centre.getX(), centre.getY() - 8, centre.getX(), centre.getY() + 8, 2.0f);
        }
    }

    // File drag "new track" overlay (when dragging audio files past last track)
    if (dragHoverTrackIndex_ == -1 && dragHoverSceneIndex_ >= 0) {
        auto vpBounds = gridViewport->getBounds();
        int lastTrackEnd = vpBounds.getX() + getTotalTracksWidth() - trackHeaderScrollOffset;
        int indicatorW = DEFAULT_CLIP_SLOT_WIDTH;
        auto indicatorBounds =
            juce::Rectangle<int>(lastTrackEnd, 0, indicatorW, vpBounds.getBottom());
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.12f));
        g.fillRect(indicatorBounds);
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.35f));
        g.drawRect(indicatorBounds, 2);

        // Draw "+" icon
        auto centre = indicatorBounds.getCentre().toFloat();
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.6f));
        g.drawLine(centre.getX() - 8, centre.getY(), centre.getX() + 8, centre.getY(), 2.0f);
        g.drawLine(centre.getX(), centre.getY() - 8, centre.getX(), centre.getY() + 8, 2.0f);
    }

    paintControllerSceneWindowHighlight(g);
}

void SessionView::updateControllerSceneWindowHighlight() {
    auto window = SessionViewState::getInstance().getControllerSceneWindow();
    if (window.revision == controllerSceneWindowRevision_)
        return;

    controllerSceneWindowRevision_ = window.revision;
    controllerSceneOffset_ = window.sceneOffset;
    controllerSceneCount_ = window.sceneCount;
    repaint();
}

void SessionView::paintControllerSceneWindowHighlight(juce::Graphics& g) {
    if (controllerSceneOffset_ < 0 || controllerSceneCount_ <= 0 || gridViewport == nullptr ||
        sceneContainer == nullptr)
        return;

    const int sceneRowHeight = CLIP_SLOT_HEIGHT + CLIP_SLOT_MARGIN;
    const int y = gridViewport->getY() + controllerSceneOffset_ * sceneRowHeight -
                  gridViewport->getViewPositionY();
    const int h = controllerSceneCount_ * sceneRowHeight - CLIP_SLOT_MARGIN;
    if (h <= 0)
        return;

    auto clipArea = gridViewport->getBounds().withRight(sceneContainer->getRight());
    if (y >= clipArea.getBottom() || y + h <= clipArea.getY())
        return;

    auto highlight = juce::Rectangle<int>(gridViewport->getX(), y,
                                          sceneContainer->getRight() - gridViewport->getX(), h)
                         .expanded(3)
                         .getIntersection(clipArea.reduced(1));
    if (highlight.isEmpty())
        return;

    auto accent = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);
    g.setColour(accent.withAlpha(0.82f));
    g.drawRoundedRectangle(highlight.toFloat(), 7.0f, 3.0f);
}

void SessionView::resized() {
    auto bounds = getLocalBounds();

    int numTracks = static_cast<int>(trackHeaders.size());
    int sceneRowHeight = CLIP_SLOT_HEIGHT + CLIP_SLOT_MARGIN;

    if (toggleRail_)
        toggleRail_->setBounds(bounds.removeFromLeft(SessionToggleRail::RAIL_WIDTH));

    // Fader row at the bottom (tracks area + master strip in scene column).
    // A thin band along the top of the row hosts the beat indicators above
    // each track strip, keeping every fader at the same height across the row.
    auto faderRow = bounds.removeFromBottom(faderRowHeight_);
    auto togglesBand = faderRow.removeFromTop(MIXER_TOGGLES_HEIGHT);
    auto masterPulseArea = togglesBand.removeFromRight(SCENE_BUTTON_WIDTH);
    if (masterBeatIndicator_)
        masterBeatIndicator_->setBounds(masterPulseArea);
    if (beatBandContainer_) {
        beatBandContainer_->setBounds(togglesBand);
        beatBandContainer_->setTrackLayout(numTracks, trackColumnWidths_, TRACK_SEPARATOR_WIDTH,
                                           trackHeaderScrollOffset);
    }
    auto masterFaderArea = faderRow.removeFromRight(SCENE_BUTTON_WIDTH);
    if (masterStrip_)
        masterStrip_->setBounds(masterFaderArea.reduced(2));
    faderContainer->setBounds(faderRow);
    faderContainer->setTrackLayout(numTracks, trackColumnWidths_, TRACK_SEPARATOR_WIDTH,
                                   trackHeaderScrollOffset);

    // Position mini channel strips within fader container (synced with grid horizontal scroll).
    // Use the container's actual height — faderRowHeight_ counts the toggles
    // band that was carved off the top, so sizing strips to faderRowHeight_ - 2
    // would overhang the container and clip the mute/solo row at the bottom.
    const int miniStripHeight = faderContainer->getHeight() - 2;
    for (int i = 0; i < numTracks && i < static_cast<int>(trackMiniStrips_.size()); ++i) {
        int x = getTrackX(i) - trackHeaderScrollOffset;
        int w = trackColumnWidths_[i];
        trackMiniStrips_[i]->setBounds(x + 1, 1, w - 2, miniStripHeight);
    }

    // Resize handle between IO/stop row and fader row
    auto resizeHandleRow = bounds.removeFromBottom(4);
    faderResizeHandle_->setBounds(resizeHandleRow);

    // I/O routing row (conditional, between stop buttons and resize handle)
    if (ioRowVisible_) {
        auto ioRow = bounds.removeFromBottom(IO_ROW_HEIGHT);
        ioContainer_->setBounds(ioRow);
        ioContainer_->setTrackLayout(numTracks, trackColumnWidths_, TRACK_SEPARATOR_WIDTH,
                                     trackHeaderScrollOffset);
        ioContainer_->setVisible(true);

        for (int i = 0; i < numTracks && i < static_cast<int>(trackIOStrips_.size()); ++i) {
            int x = getTrackX(i) - trackHeaderScrollOffset;
            int w = trackColumnWidths_[i];
            trackIOStrips_[i]->setBounds(x + 1, 1, w - 2, IO_ROW_HEIGHT - 2);
        }
    } else {
        ioContainer_->setVisible(false);
    }

    // Send section (conditional, between stop buttons and IO row)
    if (sendRowVisible_) {
        auto sendRow = bounds.removeFromBottom(sendSectionHeight_);
        auto sendHandleRow = bounds.removeFromBottom(4);
        sendResizeHandle_->setBounds(sendHandleRow);
        sendResizeHandle_->setVisible(true);
        sendSectionContainer_->setBounds(sendRow);
        sendSectionContainer_->setTrackLayout(numTracks, trackColumnWidths_, TRACK_SEPARATOR_WIDTH,
                                              trackHeaderScrollOffset);
        sendSectionContainer_->setVisible(true);

        for (int i = 0; i < numTracks && i < static_cast<int>(trackSendViewports_.size()); ++i) {
            int x = getTrackX(i) - trackHeaderScrollOffset;
            int w = trackColumnWidths_[i];
            trackSendViewports_[i]->setBounds(x + 1, 1, w - 2, sendSectionHeight_ - 2);
            if (i < static_cast<int>(trackSendStrips_.size())) {
                trackSendStrips_[i]->setSize(w - 2, trackSendStrips_[i]->getHeight());
            }
        }
    } else {
        sendSectionContainer_->setVisible(false);
        sendResizeHandle_->setVisible(false);
    }

    // Top row: Master label in scene column corner, headers in tracks area
    auto topRow = bounds.removeFromTop(TRACK_HEADER_HEIGHT);
    auto cornerArea = topRow.removeFromRight(SCENE_BUTTON_WIDTH);
    masterLabel_->setBounds(cornerArea.reduced(2));
    headerContainer->setBounds(topRow);
    headerContainer->setTrackLayout(numTracks, trackColumnWidths_, TRACK_SEPARATOR_WIDTH,
                                    trackHeaderScrollOffset);

    // Position track headers and resize handles within header container (synced with grid scroll)
    for (int i = 0; i < numTracks; ++i) {
        int x = getTrackX(i) - trackHeaderScrollOffset;
        int w = trackColumnWidths_[i];
        trackHeaders[i]->setBounds(x + 2, 2, w - 4, TRACK_HEADER_HEIGHT - 4);

        // Position resize handle at right edge of header
        if (i < static_cast<int>(trackResizeHandles_.size())) {
            trackResizeHandles_[i]->setBounds(x + w - 2, 0, 4, TRACK_HEADER_HEIGHT);
        }
    }

    // Scene container on the right of remaining area
    auto sceneArea = bounds.removeFromRight(SCENE_BUTTON_WIDTH);
    sceneContainer->setBounds(sceneArea);

    // Position scene buttons within scene container (synced with grid scroll)
    for (int i = 0; i < static_cast<int>(sceneButtons.size()); ++i) {
        int y = i * sceneRowHeight - sceneButtonScrollOffset;
        sceneButtons[i]->setBounds(2, y, SCENE_BUTTON_WIDTH - 4, CLIP_SLOT_HEIGHT);
    }

    // Grid viewport takes remaining space (below headers, above stop buttons)
    gridViewport->setBounds(bounds);
    gridViewport->setTrackLayout(numTracks, trackColumnWidths_, TRACK_SEPARATOR_WIDTH);

    // Size the grid content to fit the scenes
    int gridWidth = getTotalTracksWidth();
    int gridHeight = numScenes_ * sceneRowHeight;
    gridContent->setSize(gridWidth, gridHeight);
    gridContent->setTrackWidths(trackColumnWidths_);

    // Position clip slots within grid content
    for (int track = 0; track < numTracks; ++track) {
        int trackX = getTrackX(track);
        int w = trackColumnWidths_[track];
        int numSlotsForTrack = static_cast<int>(clipSlots[track].size());
        for (int scene = 0; scene < numSlotsForTrack; ++scene) {
            int y = scene * sceneRowHeight;
            clipSlots[track][scene]->setBounds(trackX, y, w, CLIP_SLOT_HEIGHT);
        }
    }
}

void SessionView::scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) {
    int numTracks = static_cast<int>(trackHeaders.size());

    if (scrollBar == &gridViewport->getHorizontalScrollBar()) {
        trackHeaderScrollOffset = static_cast<int>(newRangeStart);
        // Reposition headers and resize handles
        for (int i = 0; i < numTracks; ++i) {
            int x = getTrackX(i) - trackHeaderScrollOffset;
            int w = trackColumnWidths_[i];
            trackHeaders[i]->setBounds(x + 2, 2, w - 4, TRACK_HEADER_HEIGHT - 4);
            if (i < static_cast<int>(trackResizeHandles_.size())) {
                trackResizeHandles_[i]->setBounds(x + w - 2, 0, 4, TRACK_HEADER_HEIGHT);
            }
        }
        headerContainer->setTrackLayout(numTracks, trackColumnWidths_, TRACK_SEPARATOR_WIDTH,
                                        trackHeaderScrollOffset);

        // Reposition mini channel strips to sync with horizontal scroll
        const int miniStripHeight = faderContainer->getHeight() - 2;
        for (int i = 0; i < numTracks && i < static_cast<int>(trackMiniStrips_.size()); ++i) {
            int x = getTrackX(i) - trackHeaderScrollOffset;
            int w = trackColumnWidths_[i];
            trackMiniStrips_[i]->setBounds(x + 1, 1, w - 2, miniStripHeight);
        }
        faderContainer->setTrackLayout(numTracks, trackColumnWidths_, TRACK_SEPARATOR_WIDTH,
                                       trackHeaderScrollOffset);

        // Reposition IO strips to sync with horizontal scroll
        if (ioRowVisible_) {
            for (int i = 0; i < numTracks && i < static_cast<int>(trackIOStrips_.size()); ++i) {
                int x = getTrackX(i) - trackHeaderScrollOffset;
                int w = trackColumnWidths_[i];
                trackIOStrips_[i]->setBounds(x + 1, 1, w - 2, IO_ROW_HEIGHT - 2);
            }
            ioContainer_->setTrackLayout(numTracks, trackColumnWidths_, TRACK_SEPARATOR_WIDTH,
                                         trackHeaderScrollOffset);
        }

        // Reposition send section viewports to sync with horizontal scroll
        if (sendRowVisible_) {
            for (int i = 0; i < numTracks && i < static_cast<int>(trackSendViewports_.size());
                 ++i) {
                int x = getTrackX(i) - trackHeaderScrollOffset;
                int w = trackColumnWidths_[i];
                trackSendViewports_[i]->setBounds(x + 1, 1, w - 2, sendSectionHeight_ - 2);
                if (i < static_cast<int>(trackSendStrips_.size())) {
                    trackSendStrips_[i]->setSize(w - 2, trackSendStrips_[i]->getHeight());
                }
            }
            sendSectionContainer_->setTrackLayout(numTracks, trackColumnWidths_,
                                                  TRACK_SEPARATOR_WIDTH, trackHeaderScrollOffset);
        }

        // Update viewport background separators
        gridViewport->setTrackLayout(numTracks, trackColumnWidths_, TRACK_SEPARATOR_WIDTH);
    } else if (scrollBar == &gridViewport->getVerticalScrollBar()) {
        sceneButtonScrollOffset = static_cast<int>(newRangeStart);
        // Reposition scene buttons
        int sceneRowHeight = CLIP_SLOT_HEIGHT + CLIP_SLOT_MARGIN;
        for (int i = 0; i < static_cast<int>(sceneButtons.size()); ++i) {
            int y = i * sceneRowHeight - sceneButtonScrollOffset;
            sceneButtons[i]->setBounds(2, y, SCENE_BUTTON_WIDTH - 4, CLIP_SLOT_HEIGHT);
        }
        sceneContainer->repaint();
        repaint();
    }
}

void SessionView::setupSceneButtons() {
    sceneButtons.clear();

    for (int i = 0; i < numScenes_; ++i) {
        auto btn = std::make_unique<SceneButton>();
        btn->setColour(juce::TextButton::buttonColourId,
                       DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        btn->setColour(juce::TextButton::textColourOffId,
                       DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        btn->setLookAndFeel(&daw::ui::SmallButtonLookAndFeel::getInstance());
        btn->onClick = [this, i]() { onSceneLaunched(i); };
        sceneContainer->addAndMakeVisible(*btn);
        sceneButtons.push_back(std::move(btn));
    }

    syncMixerVisibilityFromConfig();
}

void SessionView::syncMixerVisibilityFromConfig() {
    auto& cfg = Config::getInstance();
    ioRowVisible_ = cfg.getMixerShowRouting();
    sendRowVisible_ = cfg.getMixerShowSends();
    recordMonitorVisible_ = cfg.getMixerShowMonitor();

    for (auto& strip : trackMiniStrips_)
        strip->setShowRecordMonitor(recordMonitorVisible_);
}

void SessionView::addScene() {
    numScenes_++;
    gridContent->setNumScenes(numScenes_);

    // Add a new scene button
    int sceneIndex = numScenes_ - 1;
    auto btn = std::make_unique<SceneButton>();
    btn->setColour(juce::TextButton::buttonColourId, DarkTheme::getColour(DarkTheme::SURFACE));
    btn->setColour(juce::TextButton::textColourOffId,
                   DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    btn->setLookAndFeel(&daw::ui::SmallButtonLookAndFeel::getInstance());
    btn->onClick = [this, sceneIndex]() { onSceneLaunched(sceneIndex); };
    sceneContainer->addAndMakeVisible(*btn);
    sceneButtons.push_back(std::move(btn));

    // Add new clip slots for each track
    int numTracks = static_cast<int>(visibleTrackIds_.size());
    for (int track = 0; track < numTracks; ++track) {
        auto slot = std::make_unique<ClipSlotButton>();
        slot->setButtonText("");
        slot->setColour(juce::TextButton::buttonColourId, DarkTheme::getColour(DarkTheme::SURFACE));
        slot->setColour(juce::TextButton::textColourOffId,
                        DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));

        wireClipSlotCallbacks(*slot, track, sceneIndex);

        gridContent->addAndMakeVisible(*slot);
        clipSlots[track].push_back(std::move(slot));
    }

    resized();
    updateAllClipSlots();

    // Scroll to show the newly added scene
    int sceneRowHeight = CLIP_SLOT_HEIGHT + CLIP_SLOT_MARGIN;
    int newSceneBottom = numScenes_ * sceneRowHeight;
    int viewportHeight = gridViewport->getViewHeight();
    if (newSceneBottom > viewportHeight) {
        gridViewport->setViewPosition(gridViewport->getViewPositionX(),
                                      newSceneBottom - viewportHeight);
    }
}

void SessionView::removeScene() {
    if (numScenes_ <= 1)
        return;

    int lastScene = numScenes_ - 1;

    // Check if any clips exist in the last scene
    auto& clipManager = ClipManager::getInstance();
    bool hasClips = false;
    for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
        ClipId clipId = clipManager.getClipInSlot(visibleTrackIds_[i], lastScene);
        if (clipId != INVALID_CLIP_ID) {
            hasClips = true;
            break;
        }
    }

    if (hasClips) {
        // Show confirmation dialog before deleting a scene with clips
        auto options = juce::MessageBoxOptions()
                           .withIconType(juce::MessageBoxIconType::WarningIcon)
                           .withTitle("Delete Scene")
                           .withMessage("Scene " + juce::String(lastScene + 1) +
                                        " contains clips. Are you sure you want to delete it?")
                           .withButton("Delete")
                           .withButton("Cancel");

        auto safeThis = juce::Component::SafePointer<SessionView>(this);
        juce::AlertWindow::showAsync(options, [safeThis](int result) {
            if (safeThis && result == 1) {
                safeThis->removeSceneAsync(safeThis->numScenes_ - 1);
            }
        });
    } else {
        removeSceneAsync(lastScene);
    }
}

void SessionView::removeSceneAsync(int sceneIndex) {
    // Re-validate: the scene must still be the last one and within bounds
    if (sceneIndex != numScenes_ - 1 || numScenes_ <= 1)
        return;

    // Stop and delete any clips in this scene
    auto& clipManager = ClipManager::getInstance();
    for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
        ClipId clipId = clipManager.getClipInSlot(visibleTrackIds_[i], sceneIndex);
        if (clipId != INVALID_CLIP_ID) {
            clipManager.stopClip(clipId);
            clipManager.deleteClip(clipId);
        }
    }

    // Remove the last scene button
    sceneButtons.pop_back();

    // Remove the last clip slot from each track
    for (auto& trackSlots : clipSlots) {
        if (!trackSlots.empty()) {
            trackSlots.pop_back();
        }
    }

    numScenes_--;
    gridContent->setNumScenes(numScenes_);

    resized();
    updateAllClipSlots();
}

void SessionView::wireClipSlotCallbacks(ClipSlotButton& slot, int trackIndex, int sceneIndex) {
    if (slot.isGroupSlot) {
        // Group slots: play button triggers/stops all descendant clips in this scene
        slot.onPlayButtonClick = [this, trackIndex, sceneIndex]() {
            TrackId groupId = visibleTrackIds_[trackIndex];
            triggerGroupScene(groupId, sceneIndex);
        };
        slot.onAddScene = [this]() { addScene(); };
        slot.onRemoveScene = [this]() { removeScene(); };
        return;
    }

    slot.onSingleClick = [this, trackIndex, sceneIndex](const juce::MouseEvent& event) {
        onClipSlotClicked(trackIndex, sceneIndex, event.mods);
    };
    slot.onPlayButtonClick = [this, trackIndex, sceneIndex]() {
        onPlayButtonClicked(trackIndex, sceneIndex);
    };
    slot.onEmptySlotStopClick = [this, trackIndex]() {
        if (trackIndex < 0 || trackIndex >= static_cast<int>(visibleTrackIds_.size()))
            return;
        if (audioEngine_)
            audioEngine_->stopSessionTrack(visibleTrackIds_[trackIndex]);
    };
    slot.onEmptySlotRecordClick = [this, trackIndex, sceneIndex]() {
        if (trackIndex < 0 || trackIndex >= static_cast<int>(visibleTrackIds_.size()))
            return;
        if (!audioEngine_)
            return;

        const TrackId trackId = visibleTrackIds_[trackIndex];
        audioEngine_->armSessionSlotRecording(trackId, sceneIndex);
        updateClipSlotAppearance(trackIndex, sceneIndex);

        if (!audioEngine_->isSessionSlotRecordArmed(trackId, sceneIndex))
            return;

        if (timelineController_) {
            const auto& state = timelineController_->getState();
            if (state.playhead.isRecording) {
                audioEngine_->beginArmedSessionSlotRecordings();
                if (!audioEngine_->isPlaying()) {
                    audioEngine_->locate(state.playhead.playbackPosition);
                    audioEngine_->play();
                }
            } else {
                timelineController_->dispatch(StartRecordEvent{});
            }
        } else {
            audioEngine_->beginArmedSessionSlotRecordings();
        }

        updateClipSlotAppearance(trackIndex, sceneIndex);
    };
    slot.onDoubleClick = [this, trackIndex, sceneIndex]() {
        openClipEditor(trackIndex, sceneIndex);
    };
    slot.onCreateMidiClip = [this, trackIndex, sceneIndex]() {
        onCreateMidiClipClicked(trackIndex, sceneIndex);
    };
    slot.onDeleteClip = [this, trackIndex, sceneIndex]() {
        TrackId tId = visibleTrackIds_[trackIndex];
        ClipId cId = ClipManager::getInstance().getClipInSlot(tId, sceneIndex);
        if (cId != INVALID_CLIP_ID) {
            auto& selection = SelectionManager::getInstance();
            if (selection.isClipSelected(cId) && selection.getSelectedClipCount() > 1) {
                deleteSelectedSessionClips();
            } else {
                UndoManager::getInstance().executeCommand(std::make_unique<DeleteClipCommand>(cId));
                selection.clearSelection();
            }
        }
    };
    slot.onCopyClip = [this, trackIndex, sceneIndex]() {
        TrackId tId = visibleTrackIds_[trackIndex];
        ClipId cId = ClipManager::getInstance().getClipInSlot(tId, sceneIndex);
        if (cId != INVALID_CLIP_ID) {
            ClipManager::getInstance().copyToClipboard({cId});
        }
    };
    slot.onCutClip = [this, trackIndex, sceneIndex]() {
        TrackId tId = visibleTrackIds_[trackIndex];
        ClipId cId = ClipManager::getInstance().getClipInSlot(tId, sceneIndex);
        if (cId != INVALID_CLIP_ID) {
            ClipManager::getInstance().copyToClipboard({cId});
            UndoManager::getInstance().executeCommand(std::make_unique<DeleteClipCommand>(cId));
        }
    };
    slot.onPasteClip = [this, trackIndex, sceneIndex]() {
        if (!ClipManager::getInstance().hasClipsInClipboard())
            return;
        TrackId tId = visibleTrackIds_[trackIndex];
        auto cmd = std::make_unique<PasteClipCommand>(BeatPosition{0.0}, tId, ClipView::Session,
                                                      sceneIndex);
        UndoManager::getInstance().executeCommand(std::move(cmd));
    };
    slot.onDuplicateClip = [this, trackIndex, sceneIndex]() {
        TrackId tId = visibleTrackIds_[trackIndex];
        ClipId cId = ClipManager::getInstance().getClipInSlot(tId, sceneIndex);
        if (cId != INVALID_CLIP_ID) {
            auto& selection = SelectionManager::getInstance();
            if (selection.isClipSelected(cId) && selection.getSelectedClipCount() > 1)
                duplicateSelectedSessionClips();
            else
                duplicateSessionClipToNextEmptyScene(cId);
        }
    };
    slot.onAddScene = [this]() { addScene(); };
    slot.onRemoveScene = [this]() { removeScene(); };
}

ClipId SessionView::duplicateSessionClipToNextEmptyScene(ClipId clipId) {
    auto& clipManager = ClipManager::getInstance();
    const auto* clip = clipManager.getClip(clipId);
    if (!clip || clip->view != ClipView::Session || clip->sceneIndex < 0)
        return INVALID_CLIP_ID;

    int targetScene = clip->sceneIndex + 1;
    while (targetScene < numScenes_ &&
           clipManager.getClipInSlot(clip->trackId, targetScene) != INVALID_CLIP_ID) {
        ++targetScene;
    }
    while (targetScene >= numScenes_)
        addScene();

    auto cmd = DuplicateClipCommand::forSessionSlot(clipId, INVALID_TRACK_ID, targetScene);
    auto* cmdPtr = cmd.get();
    UndoManager::getInstance().executeCommand(std::move(cmd));
    return cmdPtr->getDuplicatedClipId();
}

bool SessionView::duplicateSelectedSessionClips() {
    auto selectedClips = SelectionManager::getInstance().getSelectedClips();
    if (selectedClips.empty())
        return false;

    std::vector<ClipId> sessionClipIds;
    sessionClipIds.reserve(selectedClips.size());
    auto& clipManager = ClipManager::getInstance();
    for (ClipId clipId : selectedClips) {
        const auto* clip = clipManager.getClip(clipId);
        if (clip && clip->view == ClipView::Session)
            sessionClipIds.push_back(clipId);
    }
    if (sessionClipIds.empty())
        return false;

    std::sort(sessionClipIds.begin(), sessionClipIds.end(), [&clipManager](ClipId a, ClipId b) {
        const auto* clipA = clipManager.getClip(a);
        const auto* clipB = clipManager.getClip(b);
        int sceneA = clipA ? clipA->sceneIndex : 0;
        int sceneB = clipB ? clipB->sceneIndex : 0;
        if (sceneA != sceneB)
            return sceneA < sceneB;
        TrackId trackA = clipA ? clipA->trackId : INVALID_TRACK_ID;
        TrackId trackB = clipB ? clipB->trackId : INVALID_TRACK_ID;
        return trackA < trackB;
    });

    if (sessionClipIds.size() > 1)
        UndoManager::getInstance().beginCompoundOperation("Duplicate Session Clips");

    std::vector<ClipId> duplicatedClipIds;
    for (ClipId clipId : sessionClipIds) {
        ClipId duplicateId = duplicateSessionClipToNextEmptyScene(clipId);
        if (duplicateId != INVALID_CLIP_ID)
            duplicatedClipIds.push_back(duplicateId);
    }

    if (sessionClipIds.size() > 1)
        UndoManager::getInstance().endCompoundOperation();

    if (!duplicatedClipIds.empty()) {
        std::unordered_set<ClipId> newSelection(duplicatedClipIds.begin(), duplicatedClipIds.end());
        SelectionManager::getInstance().selectClips(newSelection);
    }
    return !duplicatedClipIds.empty();
}

bool SessionView::deleteSelectedSessionClips() {
    auto selectedClips = SelectionManager::getInstance().getSelectedClips();
    if (selectedClips.empty())
        return false;

    std::vector<ClipId> sessionClipIds;
    sessionClipIds.reserve(selectedClips.size());
    auto& clipManager = ClipManager::getInstance();
    for (ClipId clipId : selectedClips) {
        const auto* clip = clipManager.getClip(clipId);
        if (clip && clip->view == ClipView::Session)
            sessionClipIds.push_back(clipId);
    }
    if (sessionClipIds.empty())
        return false;

    if (sessionClipIds.size() > 1)
        UndoManager::getInstance().beginCompoundOperation("Delete Session Clips");

    for (ClipId clipId : sessionClipIds) {
        auto cmd = std::make_unique<DeleteClipCommand>(clipId);
        UndoManager::getInstance().executeCommand(std::move(cmd));
    }

    if (sessionClipIds.size() > 1)
        UndoManager::getInstance().endCompoundOperation();

    SelectionManager::getInstance().clearSelection();
    return true;
}

void SessionView::onClipSlotClicked(int trackIndex, int sceneIndex, juce::ModifierKeys mods) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(visibleTrackIds_.size())) {
        return;
    }

    TrackId trackId = visibleTrackIds_[trackIndex];
    ClipId clipId = ClipManager::getInstance().getClipInSlot(trackId, sceneIndex);

    if (clipId != INVALID_CLIP_ID) {
        // Select the clip (update inspector) - no playback change
        if (magda::isToggleSelectClick(mods))
            SelectionManager::getInstance().toggleClipSelection(clipId);
        else if (magda::isRangeSelectClick(mods))
            rangeSelectSlots(trackIndex, sceneIndex, clipId);
        else
            SelectionManager::getInstance().selectClip(clipId);
    } else {
        // Empty slot - select the track
        selectTrack(trackId);
    }
}

void SessionView::rangeSelectSlots(int trackIndex, int sceneIndex, ClipId clickedClipId) {
    auto& sel = SelectionManager::getInstance();
    auto& clipManager = ClipManager::getInstance();

    // Anchor = last single-clicked clip; the range is the rectangle of slots
    // between the anchor's (track, scene) cell and the clicked cell
    const auto* anchorClip = clipManager.getClip(sel.getAnchorClip());
    int anchorTrackIndex = -1;
    if (anchorClip != nullptr && anchorClip->sceneIndex >= 0) {
        for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
            if (visibleTrackIds_[i] == anchorClip->trackId) {
                anchorTrackIndex = static_cast<int>(i);
                break;
            }
        }
    }

    if (anchorTrackIndex < 0) {
        sel.selectClip(clickedClipId);
        return;
    }

    const int loTrack = std::min(anchorTrackIndex, trackIndex);
    const int hiTrack = std::max(anchorTrackIndex, trackIndex);
    const int loScene = std::min(anchorClip->sceneIndex, sceneIndex);
    const int hiScene = std::max(anchorClip->sceneIndex, sceneIndex);

    std::unordered_set<ClipId> clipsInRange;
    for (int t = loTrack; t <= hiTrack; ++t) {
        for (int s = loScene; s <= hiScene; ++s) {
            ClipId id = clipManager.getClipInSlot(visibleTrackIds_[static_cast<size_t>(t)], s);
            if (id != INVALID_CLIP_ID)
                clipsInRange.insert(id);
        }
    }

    if (!clipsInRange.empty())
        sel.selectClips(clipsInRange);
}

void SessionView::onPlayButtonClicked(int trackIndex, int sceneIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(visibleTrackIds_.size())) {
        return;
    }

    TrackId trackId = visibleTrackIds_[trackIndex];
    ClipId clipId = ClipManager::getInstance().getClipInSlot(trackId, sceneIndex);

    if (clipId != INVALID_CLIP_ID) {
        // Filled-slot strip is "trigger this clip". Re-clicking a playing
        // clip re-triggers (or, for Toggle-mode clips, the scheduler still
        // honours toggle — that's a per-clip setting, not a UI default).
        // Stopping is the empty-slot affordance now.
        SelectionManager::getInstance().selectClip(clipId);
        ClipManager::getInstance().triggerClip(clipId);
    }
}

void SessionView::onSceneLaunched(int sceneIndex) {
    SessionLaunchService::launchScene(visibleTrackIds_, sceneIndex);
}

void SessionView::triggerGroupScene(TrackId groupId, int sceneIndex) {
    // Collect all descendant track IDs recursively
    std::vector<TrackId> descendants;
    std::function<void(TrackId)> collectDescendants = [&](TrackId tid) {
        const auto* t = TrackManager::getInstance().getTrack(tid);
        if (!t)
            return;
        if (t->isGroup()) {
            for (auto childId : t->childIds)
                collectDescendants(childId);
        } else {
            descendants.push_back(tid);
        }
    };
    collectDescendants(groupId);

    // Check if any descendant clip in this scene is playing — if so, stop all; else trigger all
    auto& cm = ClipManager::getInstance();
    bool anyPlaying = false;
    for (auto tid : descendants) {
        ClipId cid = cm.getClipInSlot(tid, sceneIndex);
        if (cid != INVALID_CLIP_ID && audioEngine_) {
            auto state = audioEngine_->getSessionClipPlayState(cid);
            if (state == SessionClipPlayState::Playing || state == SessionClipPlayState::Queued) {
                anyPlaying = true;
                break;
            }
        }
    }

    for (auto tid : descendants) {
        ClipId cid = cm.getClipInSlot(tid, sceneIndex);
        if (anyPlaying) {
            // Stop mode: stop all clips in this scene
            if (cid != INVALID_CLIP_ID)
                cm.stopClip(cid);
        } else {
            if (cid != INVALID_CLIP_ID) {
                cm.triggerClip(cid);
            } else if (audioEngine_) {
                // Empty slot: schedule a quantized stop for this track
                audioEngine_->stopSessionTrack(tid);
            }
        }
    }
}

void SessionView::openClipEditor(int trackIndex, int sceneIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(visibleTrackIds_.size())) {
        return;
    }

    TrackId trackId = visibleTrackIds_[trackIndex];
    ClipId clipId = ClipManager::getInstance().getClipInSlot(trackId, sceneIndex);

    if (clipId == INVALID_CLIP_ID) {
        // Empty slot — create a new MIDI clip
        onCreateMidiClipClicked(trackIndex, sceneIndex);
        return;
    }

    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (clip) {
        // Select the clip so the bottom panel picks it up
        ClipManager::getInstance().setSelectedClip(clipId);

        auto& panelController = daw::ui::PanelController::getInstance();

        // Expand bottom panel if collapsed
        bool isCollapsed = panelController.getPanelState(daw::ui::PanelLocation::Bottom).collapsed;
        if (isCollapsed) {
            panelController.setCollapsed(daw::ui::PanelLocation::Bottom, false);
        }

        // For audio clips, explicitly switch to waveform editor.
        // For MIDI clips, BottomPanel's clipSelectionChanged handles the
        // PianoRoll vs DrumGrid choice, respecting the user's preference.
        if (clip->isAudio()) {
            panelController.setActiveTabByType(daw::ui::PanelLocation::Bottom,
                                               daw::ui::PanelContentType::WaveformEditor);
        }
    }
}

void SessionView::onCreateMidiClipClicked(int trackIndex, int sceneIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(visibleTrackIds_.size()))
        return;
    if (sceneIndex < 0 || sceneIndex >= numScenes_)
        return;

    TrackId trackId = visibleTrackIds_[trackIndex];

    // Don't create if slot already has a clip
    if (ClipManager::getInstance().getClipInSlot(trackId, sceneIndex) != INVALID_CLIP_ID)
        return;

    // Create clip through command system for proper undo support
    auto cmd = std::make_unique<CreateClipCommand>(ClipType::MIDI, trackId, BeatPosition{0.0},
                                                   BeatDuration{4.0}, "", ClipView::Session);

    // Get raw pointer before moving to UndoManager
    auto* cmdPtr = cmd.get();
    UndoManager::getInstance().executeCommand(std::move(cmd));

    // Get the created clip ID and set its scene index
    ClipId clipId = cmdPtr->getCreatedClipId();
    if (clipId != INVALID_CLIP_ID) {
        ClipManager::getInstance().setClipSceneIndex(clipId, sceneIndex);
        updateClipSlotAppearance(trackIndex, sceneIndex);
    }
}

void SessionView::trackSelectionChanged(TrackId trackId) {
    juce::ignoreUnused(trackId);
    updateHeaderSelectionVisuals();
}

void SessionView::selectionTypeChanged(SelectionType /*newType*/) {
    updateHeaderSelectionVisuals();
}

void SessionView::multiTrackSelectionChanged(const std::unordered_set<TrackId>& /*trackIds*/) {
    updateHeaderSelectionVisuals();
}

// =============================================================================
// Track Header Drag-and-Drop (reorder / drop into group)
// =============================================================================

void SessionView::calculateHeaderDropTarget(int mouseX) {
    headerDropType_ = HeaderDropType::None;
    headerDropIndex_ = -1;
    int numTracks = static_cast<int>(visibleTrackIds_.size());
    for (int i = 0; i < numTracks; ++i) {
        if (i == headerDragIndex_)
            continue;
        int x = getTrackX(i);
        int w = trackColumnWidths_[i];
        if (mouseX >= x && mouseX < x + w + TRACK_SEPARATOR_WIDTH) {
            int quarter = w / 4;
            if (mouseX < x + quarter) {
                headerDropType_ = HeaderDropType::BetweenTracks;
                headerDropIndex_ = i;
            } else if (mouseX > x + w - quarter) {
                headerDropType_ = HeaderDropType::BetweenTracks;
                headerDropIndex_ = i + 1;
            } else if (canDropIntoGroup(headerDragIndex_, i)) {
                headerDropType_ = HeaderDropType::OntoGroup;
                headerDropIndex_ = i;
            }
            return;
        }
    }
    if (mouseX > getTotalTracksWidth()) {
        headerDropType_ = HeaderDropType::BetweenTracks;
        headerDropIndex_ = numTracks;
    }
}

bool SessionView::canDropIntoGroup(int draggedIndex, int targetIndex) const {
    if (draggedIndex < 0 || targetIndex < 0 ||
        draggedIndex >= static_cast<int>(visibleTrackIds_.size()) ||
        targetIndex >= static_cast<int>(visibleTrackIds_.size()))
        return false;
    if (draggedIndex == targetIndex)
        return false;
    auto& tm = TrackManager::getInstance();
    const auto* target = tm.getTrack(visibleTrackIds_[targetIndex]);
    if (!target || !target->isGroup())
        return false;
    const auto* dragged = tm.getTrack(visibleTrackIds_[draggedIndex]);
    if (dragged && dragged->isGroup()) {
        auto desc = tm.getAllDescendants(dragged->id);
        if (std::find(desc.begin(), desc.end(), target->id) != desc.end())
            return false;
    }
    return true;
}

void SessionView::executeHeaderDrop() {
    if (headerDragIndex_ < 0 || headerDropType_ == HeaderDropType::None)
        return;
    auto& tm = TrackManager::getInstance();
    TrackId draggedId = visibleTrackIds_[headerDragIndex_];
    if (headerDropType_ == HeaderDropType::OntoGroup && headerDropIndex_ >= 0) {
        TrackId groupId = visibleTrackIds_[headerDropIndex_];
        tm.addTrackToGroup(draggedId, groupId);
    } else if (headerDropType_ == HeaderDropType::BetweenTracks && headerDropIndex_ >= 0) {
        TrackId targetParent = INVALID_TRACK_ID;
        if (headerDropIndex_ < static_cast<int>(visibleTrackIds_.size())) {
            const auto* t = tm.getTrack(visibleTrackIds_[headerDropIndex_]);
            if (t)
                targetParent = t->parentId;
        } else if (!visibleTrackIds_.empty()) {
            const auto* t = tm.getTrack(visibleTrackIds_.back());
            if (t)
                targetParent = t->parentId;
        }
        const auto* dragged = tm.getTrack(draggedId);
        if (dragged && dragged->parentId != targetParent) {
            tm.removeTrackFromGroup(draggedId);
            if (targetParent != INVALID_TRACK_ID)
                tm.addTrackToGroup(draggedId, targetParent);
        }
        int targetIdx = headerDropIndex_ < static_cast<int>(visibleTrackIds_.size())
                            ? tm.getTrackIndex(visibleTrackIds_[headerDropIndex_])
                            : tm.getNumTracks();
        int currentIdx = tm.getTrackIndex(draggedId);
        if (currentIdx < targetIdx)
            targetIdx--;
        tm.moveTrack(draggedId, targetIdx);
    }
}

void SessionView::resetHeaderDragState() {
    headerIsDragging_ = false;
    headerDragIndex_ = -1;
    headerDropType_ = HeaderDropType::None;
    headerDropIndex_ = -1;
    headerContainer->repaint();
}

void SessionView::paintHeaderDragFeedback(juce::Graphics& g) {
    if (!headerIsDragging_ || headerDragIndex_ < 0)
        return;
    // Highlight dragged header
    int dx = getTrackX(headerDragIndex_) - trackHeaderScrollOffset;
    int dw = trackColumnWidths_[headerDragIndex_];
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.3f));
    g.fillRect(dx, 0, dw, headerContainer->getHeight());

    if (headerDropType_ == HeaderDropType::BetweenTracks && headerDropIndex_ >= 0) {
        int lineX;
        if (headerDropIndex_ >= static_cast<int>(visibleTrackIds_.size()))
            lineX = getTotalTracksWidth() - trackHeaderScrollOffset;
        else
            lineX = getTrackX(headerDropIndex_) - trackHeaderScrollOffset;
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        g.fillRect(lineX - 2, 0, 4, headerContainer->getHeight());
    } else if (headerDropType_ == HeaderDropType::OntoGroup && headerDropIndex_ >= 0) {
        int gx = getTrackX(headerDropIndex_) - trackHeaderScrollOffset;
        int gw = trackColumnWidths_[headerDropIndex_];
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
        g.drawRect(gx, 0, gw, headerContainer->getHeight(), 3);
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.15f));
        g.fillRect(gx, 0, gw, headerContainer->getHeight());
    }
}

void SessionView::selectTrack(TrackId trackId) {
    SelectionManager::getInstance().selectTrack(trackId);
}

void SessionView::updateHeaderSelectionVisuals() {
    // Reflect the full SelectionManager state — including multi-selection —
    // so every selected header lights up, not just the primary one.
    auto& sel = SelectionManager::getInstance();
    const auto selectedId = sel.getSelectedTrack();

    for (size_t i = 0; i < visibleTrackIds_.size() && i < trackHeaders.size(); ++i) {
        bool isSelected = sel.isTrackSelected(visibleTrackIds_[i]);
        auto* header = trackHeaders[i].get();

        // Get track info for proper coloring
        const auto* track = TrackManager::getInstance().getTrack(visibleTrackIds_[i]);
        if (!track)
            continue;

        if (isSelected) {
            // Selected: white text on black background
            header->setColour(juce::TextButton::buttonColourId, juce::Colours::black);
            header->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        } else {
            // Unselected: track colour background
            header->setColour(juce::TextButton::buttonColourId, track->colour.withAlpha(0.5f));
            header->setColour(juce::TextButton::textColourOffId,
                              DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        }
    }
    // Master label selection
    if (masterLabel_) {
        bool masterSelected = selectedId == MASTER_TRACK_ID;
        if (masterSelected) {
            masterLabel_->setColour(juce::TextButton::buttonColourId, juce::Colours::black);
            masterLabel_->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        } else {
            masterLabel_->setColour(juce::TextButton::buttonColourId,
                                    DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
            masterLabel_->setColour(juce::TextButton::textColourOffId,
                                    DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        }
    }

    repaint();
}

void SessionView::showMixerContextMenu() {
    juce::PopupMenu menu;
    menu.addItem(1, "Show I/O", true, ioRowVisible_);
    menu.addItem(2, "Show Sends", true, sendRowVisible_);
    menu.addItem(3, "Show Record/Monitor", true, recordMonitorVisible_);

    auto safeThis = juce::Component::SafePointer<SessionView>(this);
    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis](int result) {
        if (!safeThis)
            return;
        auto& cfg = Config::getInstance();
        if (result == 1) {
            cfg.setMixerShowRouting(!cfg.getMixerShowRouting());
        } else if (result == 2) {
            cfg.setMixerShowSends(!cfg.getMixerShowSends());
        } else if (result == 3) {
            cfg.setMixerShowMonitor(!cfg.getMixerShowMonitor());
        } else {
            return;
        }
        cfg.save();
        safeThis->syncMixerVisibilityFromConfig();
        if (safeThis->toggleRail_)
            safeThis->toggleRail_->refreshFromConfig();
        safeThis->resized();
    });
}

// ============================================================================
// ClipManagerListener
// ============================================================================

void SessionView::clipsChanged() {
    DBG("SessionView::clipsChanged visibleTracks=" << formatTrackIds(visibleTrackIds_)
                                                   << " sessionClips=" << formatSessionClips());

    // Clear any stale drag overlay state — structural changes (add/remove clip)
    // can interrupt drag operations without proper exit callbacks.
    showPluginDropOverlay_ = false;
    pluginDropTrackIndex_ = -1;
    clearDragHighlight();

    updateAllClipSlots();
}

void SessionView::clipPropertyChanged(ClipId clipId) {
    // Find clip and update its slot
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip || clip->sceneIndex < 0)
        return;

    // Find track index
    int trackIndex = -1;
    for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
        if (visibleTrackIds_[i] == clip->trackId) {
            trackIndex = static_cast<int>(i);
            break;
        }
    }

    if (trackIndex >= 0) {
        updateClipSlotAppearance(trackIndex, clip->sceneIndex);
    }

    // Also update parent group slot
    const auto* track = TrackManager::getInstance().getTrack(clip->trackId);
    if (track && track->hasParent()) {
        for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
            if (visibleTrackIds_[i] == track->parentId) {
                updateClipSlotAppearance(static_cast<int>(i), clip->sceneIndex);
                break;
            }
        }
    }

    updateSceneButtonIcon(clip->sceneIndex);
}

void SessionView::clipSelectionChanged(ClipId /*clipId*/) {
    // Refresh all slots to update selection highlight
    updateAllClipSlots();
}

void SessionView::multiClipSelectionChanged(const std::unordered_set<ClipId>& /*clipIds*/) {
    // Refresh all slots to update multi-selection highlight
    updateAllClipSlots();
}

void SessionView::clipPlaybackStateChanged(ClipId clipId) {
    // Update slot appearance when playback state changes
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip || clip->sceneIndex < 0) {
        DBG("SessionView::clipPlaybackStateChanged: clip " << clipId << " not found or no scene");
        return;
    }

    auto playState = audioEngine_ ? audioEngine_->getSessionClipPlayState(clipId)
                                  : SessionClipPlayState::Stopped;
    DBG("SessionView::clipPlaybackStateChanged: clip "
        << clipId << " playState=" << (int)playState
        << " sessionPlayheadPos=" << clip->sessionPlayheadPos);

    // Find track index
    int trackIndex = -1;
    for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
        if (visibleTrackIds_[i] == clip->trackId) {
            trackIndex = static_cast<int>(i);
            break;
        }
    }

    if (trackIndex >= 0) {
        updateClipSlotAppearance(trackIndex, clip->sceneIndex);
    }

    // Also update parent group slot if this track has a parent
    const auto* track = TrackManager::getInstance().getTrack(clip->trackId);
    if (track && track->hasParent()) {
        for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
            if (visibleTrackIds_[i] == track->parentId) {
                updateClipSlotAppearance(static_cast<int>(i), clip->sceneIndex);
                break;
            }
        }
    }

    updateSceneButtonIcon(clip->sceneIndex);
}

void SessionView::updateClipSlotAppearance(int trackIndex, int sceneIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(clipSlots.size()))
        return;
    if (trackIndex >= static_cast<int>(visibleTrackIds_.size()))
        return;
    if (sceneIndex < 0 || sceneIndex >= static_cast<int>(clipSlots[trackIndex].size()))
        return;

    auto* slot = static_cast<ClipSlotButton*>(clipSlots[trackIndex][sceneIndex].get());
    if (!slot)
        return;

    TrackId trackId = visibleTrackIds_[trackIndex];

    // Always set slot identity for drag-and-drop
    slot->trackId = trackId;
    slot->sceneIndex = sceneIndex;

    // Mirror record-arm state so empty slots can render the record glyph.
    if (const auto* trackInfo = TrackManager::getInstance().getTrack(trackId))
        slot->trackIsRecordArmed = trackInfo->recordArmed;
    else
        slot->trackIsRecordArmed = false;

    // Group slot: check if any descendant has a clip in this scene
    if (slot->isGroupSlot) {
        bool anyClips = false;
        bool anyPlaying = false;

        std::function<void(TrackId)> checkDescendants = [&](TrackId tid) {
            const auto* t = TrackManager::getInstance().getTrack(tid);
            if (!t)
                return;
            if (t->isGroup()) {
                for (auto childId : t->childIds)
                    checkDescendants(childId);
            } else {
                ClipId cid = ClipManager::getInstance().getClipInSlot(tid, sceneIndex);
                if (cid != INVALID_CLIP_ID) {
                    anyClips = true;
                    if (audioEngine_) {
                        auto state = audioEngine_->getSessionClipPlayState(cid);
                        if (state == SessionClipPlayState::Playing ||
                            state == SessionClipPlayState::Queued)
                            anyPlaying = true;
                    }
                }
            }
        };
        checkDescendants(trackId);

        slot->hasChildClips = anyClips;
        slot->childClipIsPlaying = anyPlaying;
        slot->hasClip = false;
        slot->slotRecordArmed = false;
        slot->slotIsRecording = false;
        slot->setButtonText("");
        slot->setColour(juce::TextButton::buttonColourId, DarkTheme::getColour(DarkTheme::SURFACE));
        slot->repaint();
        return;
    }

    ClipId clipId = ClipManager::getInstance().getClipInSlot(trackId, sceneIndex);

    if (clipId != INVALID_CLIP_ID) {
        const auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip) {
            DBG("SessionView::slotOccupied trackIndex="
                << trackIndex << " trackId=" << trackId << " scene=" << sceneIndex << " clipId="
                << clipId << " clipTrackId=" << clip->trackId << " clipScene=" << clip->sceneIndex
                << " visibleTracks=" << formatTrackIds(visibleTrackIds_));

            // Query play state from the scheduler (single source of truth)
            auto playState = audioEngine_ ? audioEngine_->getSessionClipPlayState(clipId)
                                          : SessionClipPlayState::Stopped;

            // Update slot state for custom painting
            slot->hasClip = true;
            slot->clipId = clipId;
            slot->clipIsPlaying = (playState == SessionClipPlayState::Playing);
            slot->clipIsQueued = (playState == SessionClipPlayState::Queued);
            slot->slotRecordArmed = false;
            slot->slotIsRecording = false;
            slot->isSelected = SelectionManager::getInstance().isClipSelected(clipId);
            // Issue #1157: read through the accessor — for autoTempo clips
            // this computes beats × 60 / projectBPM live, so the slot progress
            // overlay stays correct after a project-tempo change even before the
            // seconds cache is refreshed. Use the LOOP length (what the playhead
            // wraps at), not the clip placement length: reinterpreting a clip's
            // source BPM changes loopLengthBeats without touching
            // placement.lengthBeats, so getTimelineLength() would leave the bar
            // out of sync with the playhead position.
            {
                double sessionBPM =
                    timelineController_ ? timelineController_->getState().tempo.bpm : 120.0;
                slot->clipLength = clip->getTimelineLoopLength(sessionBPM);
            }
            {
                auto posIt = clipPlayheadPositions_.find(clipId);
                slot->sessionPlayheadPos =
                    (slot->clipIsPlaying && posIt != clipPlayheadPositions_.end()) ? posIt->second
                                                                                   : -1.0;
            }

            slot->setButtonText(clip->name);

            // Clip always shows its own colour; play state is shown via the play/stop icon
            slot->setColour(juce::TextButton::buttonColourId, clip->colour.withAlpha(0.7f));
            slot->setColour(juce::TextButton::textColourOffId,
                            DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        }
    } else {
        // Empty slot
        if (slot->hasClip || slot->clipId != INVALID_CLIP_ID) {
            DBG("SessionView::slotCleared trackIndex="
                << trackIndex << " trackId=" << trackId << " scene=" << sceneIndex << " oldClipId="
                << slot->clipId << " visibleTracks=" << formatTrackIds(visibleTrackIds_));
        }

        slot->hasClip = false;
        slot->clipId = INVALID_CLIP_ID;
        slot->clipIsPlaying = false;
        slot->clipIsQueued = false;
        slot->slotRecordArmed =
            audioEngine_ != nullptr && audioEngine_->isSessionSlotRecordArmed(trackId, sceneIndex);
        slot->slotIsRecording =
            audioEngine_ != nullptr && audioEngine_->isSessionSlotRecording(trackId, sceneIndex);
        slot->stopIsQueued =
            audioEngine_ != nullptr && audioEngine_->isSessionTrackStopPending(trackId);
        slot->isSelected = false;
        slot->clipLength = 0.0;
        slot->sessionPlayheadPos = -1.0;
        slot->setButtonText("");
        slot->setColour(juce::TextButton::buttonColourId, DarkTheme::getColour(DarkTheme::SURFACE));
        slot->setColour(juce::TextButton::textColourOffId,
                        DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    }

    slot->repaint();
}

void SessionView::updateAllClipSlots() {
    int numTracks = static_cast<int>(visibleTrackIds_.size());
    for (int trackIndex = 0; trackIndex < numTracks; ++trackIndex) {
        int numSlots = static_cast<int>(clipSlots[trackIndex].size());
        for (int sceneIndex = 0; sceneIndex < numSlots; ++sceneIndex) {
            updateClipSlotAppearance(trackIndex, sceneIndex);
        }
    }
    updateAllSceneButtonIcons();
}

void SessionView::updateSceneButtonIcon(int sceneIndex) {
    if (sceneIndex < 0 || sceneIndex >= static_cast<int>(sceneButtons.size()))
        return;

    bool anyClips = false;
    bool anyPlaying = false;
    auto& cm = ClipManager::getInstance();
    for (TrackId tid : visibleTrackIds_) {
        ClipId cid = cm.getClipInSlot(tid, sceneIndex);
        if (cid == INVALID_CLIP_ID)
            continue;
        anyClips = true;
        if (audioEngine_) {
            auto state = audioEngine_->getSessionClipPlayState(cid);
            if (state == SessionClipPlayState::Playing || state == SessionClipPlayState::Queued) {
                anyPlaying = true;
                break;
            }
        }
    }

    if (auto* sb = dynamic_cast<SceneButton*>(sceneButtons[sceneIndex].get())) {
        if (sb->hasAnyClip != anyClips || sb->hasAnyPlaying != anyPlaying) {
            sb->hasAnyClip = anyClips;
            sb->hasAnyPlaying = anyPlaying;
            sb->repaint();
        }
    }
}

void SessionView::updateAllSceneButtonIcons() {
    for (int i = 0; i < static_cast<int>(sceneButtons.size()); ++i)
        updateSceneButtonIcon(i);
}

// ============================================================================
// Beat indicator band — per-track rate + menu
// ============================================================================

SessionView::BeatRate SessionView::getTrackBeatRate(TrackId trackId) const {
    auto it = trackBeatRates_.find(trackId);
    return it != trackBeatRates_.end() ? it->second : BeatRate::Quarter;
}

void SessionView::setTrackBeatRate(TrackId trackId, BeatRate rate) {
    trackBeatRates_[trackId] = rate;
    if (beatBandContainer_)
        beatBandContainer_->repaint();
}

bool SessionView::isBeatHidden(TrackId trackId) const {
    return beatHiddenTracks_.count(trackId) != 0;
}

void SessionView::toggleBeatHidden(TrackId trackId) {
    if (!beatHiddenTracks_.insert(trackId).second)
        beatHiddenTracks_.erase(trackId);
    if (beatBandContainer_)
        beatBandContainer_->repaint();
}

void SessionView::showBeatRateMenuFor(TrackId trackId) {
    const auto current = getTrackBeatRate(trackId);
    juce::PopupMenu menu;
    auto addItem = [&](int id, const char* label, BeatRate r) {
        menu.addItem(id, label, true, current == r);
    };
    addItem(1, "1", BeatRate::Whole);
    addItem(2, "1/2", BeatRate::Half);
    addItem(3, "1/4", BeatRate::Quarter);
    addItem(4, "1/8", BeatRate::Eighth);

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, trackId](int result) {
        switch (result) {
            case 1:
                setTrackBeatRate(trackId, BeatRate::Whole);
                break;
            case 2:
                setTrackBeatRate(trackId, BeatRate::Half);
                break;
            case 3:
                setTrackBeatRate(trackId, BeatRate::Quarter);
                break;
            case 4:
                setTrackBeatRate(trackId, BeatRate::Eighth);
                break;
            default:
                break;
        }
    });
}

// ============================================================================
// Session Playhead
// ============================================================================

void SessionView::setSessionPlayheadPositions(const std::unordered_map<ClipId, double>& positions) {
    clipPlayheadPositions_ = positions;

    // Update playhead positions and reset slots that stopped playing
    for (auto& trackSlots : clipSlots) {
        for (auto& slotBtn : trackSlots) {
            auto* slot = dynamic_cast<ClipSlotButton*>(slotBtn.get());
            if (!slot || !slot->hasClip)
                continue;

            double prev = slot->sessionPlayheadPos;
            if (slot->clipIsPlaying) {
                auto it = positions.find(slot->clipId);
                slot->sessionPlayheadPos = (it != positions.end()) ? it->second : -1.0;
            } else {
                slot->sessionPlayheadPos = -1.0;
            }

            if (slot->sessionPlayheadPos != prev)
                slot->repaint();
        }
    }
}

// ============================================================================
// Audio Engine & Metering
// ============================================================================

void SessionView::setAudioEngine(AudioEngine* engine) {
    // Unregister from old engine
    if (audioEngine_) {
        if (auto* mb = audioEngine_->getMidiBridge())
            mb->removeMidiDeviceListListener(this);
    }
    audioEngine_ = engine;
    if (audioEngine_) {
        if (auto* mb = audioEngine_->getMidiBridge())
            mb->addMidiDeviceListListener(this);
        startTimerHz(30);  // 30Hz meter refresh
    } else {
        stopTimer();
    }
}

void SessionView::midiDeviceListChanged() {
    auto safeThis = juce::Component::SafePointer<SessionView>(this);
    juce::MessageManager::callAsync([safeThis]() {
        if (auto* self = safeThis.getComponent())
            self->tracksChanged();
    });
}

void SessionView::timerCallback() {
    updateControllerSceneWindowHighlight();

    if (!audioEngine_)
        return;

    auto* teWrapper = dynamic_cast<TracktionEngineWrapper*>(audioEngine_);
    if (!teWrapper)
        return;

    auto* bridge = teWrapper->getAudioBridge();
    if (!bridge)
        return;

    auto& meteringBuffer = bridge->getMeteringBuffer();

    // Update track strip meters (peek, don't consume — MixerView also reads these)
    for (auto& strip : trackMiniStrips_) {
        int trackId = strip->getTrackId();
        MeterData data;
        if (meteringBuffer.peekLatest(trackId, data)) {
            strip->setMeterLevels(data.peakL, data.peakR);
        }
    }

    // Update blink state for queued clips — blink on the beat
    {
        bool newBlinkOn = false;
        double posBeats = 0.0;
        bool transportPlaying = false;
        if (auto* edit = teWrapper->getEdit()) {
            auto& transport = edit->getTransport();
            if (transport.isPlaying()) {
                transportPlaying = true;
                double bpm = edit->tempoSequence.getBpmAt(tracktion::TimePosition());
                // Prefer the audio-thread sampled transport position so the
                // beat indicator stays phase-locked with what's actually
                // being played. Falls back to the message-thread read if the
                // audio thread hasn't ticked yet.
                double atPos = audioEngine_->getAudioThreadTransportSeconds();
                double pos = (atPos >= 0.0) ? atPos : transport.getPosition().inSeconds();
                const double projectBpm = isValidBpm(bpm) ? bpm : DEFAULT_BPM;
                double beatDuration = 60.0 / projectBpm;
                double beatPhase = std::fmod(pos, beatDuration) / beatDuration;
                newBlinkOn = (beatPhase < 0.5);
                posBeats = pos * projectBpm / 60.0;
            }
        }

        // Per-track beat phases for the indicator band. Each track's segment
        // pulses at its own subdivision relative to the project bar grid.
        if (beatBandContainer_) {
            std::vector<double> phases(visibleTrackIds_.size(), 0.0);
            int tsNum = 4, tsDen = 4;
            teWrapper->getTimeSignature(tsNum, tsDen);
            if (tsNum <= 0)
                tsNum = 4;
            for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
                if (!transportPlaying) {
                    phases[i] = 1.0;  // fully decayed = no pulse drawn
                    continue;
                }
                double period = 1.0;
                switch (getTrackBeatRate(visibleTrackIds_[i])) {
                    case BeatRate::Whole:
                        period = static_cast<double>(tsNum);
                        break;
                    case BeatRate::Half:
                        period = static_cast<double>(tsNum) * 0.5;
                        break;
                    case BeatRate::Quarter:
                        period = 1.0;
                        break;
                    case BeatRate::Eighth:
                        period = 0.5;
                        break;
                }
                phases[i] = std::fmod(posBeats, period) / period;
            }
            beatBandContainer_->setTrackBeatPhases(std::move(phases));
        }
        if (masterBeatIndicator_)
            masterBeatIndicator_->setBeatPhase(transportPlaying ? std::fmod(posBeats, 1.0) : 1.0);
        bool anyTrackStopPending = false;
        for (size_t trackIdx = 0; trackIdx < clipSlots.size(); ++trackIdx) {
            // Re-poll stop-pending state per track. The scheduler doesn't
            // notify when the orphan sweep retires the handle, so empty
            // slots need to drop the blink on their own. Same per-tick cost
            // as clipIsQueued repaint.
            const bool stopPending =
                trackIdx < visibleTrackIds_.size() &&
                audioEngine_->isSessionTrackStopPending(visibleTrackIds_[trackIdx]);
            if (stopPending)
                anyTrackStopPending = true;
            for (auto& slotBtn : clipSlots[trackIdx]) {
                auto* slot = dynamic_cast<ClipSlotButton*>(slotBtn.get());
                if (!slot)
                    continue;
                if (slot->clipIsQueued) {
                    slot->blinkOn = newBlinkOn;
                    slot->repaint();
                } else if (!slot->hasClip && slot->slotIsRecording) {
                    slot->blinkOn = newBlinkOn;
                    slot->repaint();
                } else if (!slot->hasClip && !slot->trackIsRecordArmed) {
                    if (slot->stopIsQueued != stopPending) {
                        slot->stopIsQueued = stopPending;
                        slot->blinkOn = newBlinkOn;
                        slot->repaint();
                    } else if (stopPending) {
                        slot->blinkOn = newBlinkOn;
                        slot->repaint();
                    }
                }
            }
        }

        // Scene buttons (master column): the stop-mode button (empty row)
        // blinks while any track has a quantized stop pending — clicking it
        // queues stops across the whole row, so the same affordance the user
        // just hit should mirror the wait.
        for (auto& sceneBtn : sceneButtons) {
            auto* sb = dynamic_cast<SceneButton*>(sceneBtn.get());
            if (!sb || sb->hasAnyClip)
                continue;
            if (sb->stopIsQueued != anyTrackStopPending) {
                sb->stopIsQueued = anyTrackStopPending;
                sb->blinkOn = newBlinkOn;
                sb->repaint();
            } else if (anyTrackStopPending) {
                sb->blinkOn = newBlinkOn;
                sb->repaint();
            }
        }
    }

    // Update master strip meters
    if (masterStrip_) {
        float masterPeakL = bridge->getMasterPeakL();
        float masterPeakR = bridge->getMasterPeakR();
        masterStrip_->setMeterLevels(masterPeakL, masterPeakR);
    }
}

// ============================================================================
// File Drag & Drop
// ============================================================================

bool SessionView::isInterestedInFileDrag(const juce::StringArray& files) {
    // Accept if at least one file is an audio file
    for (const auto& file : files) {
        if (isAudioFile(file)) {
            return true;
        }
    }
    return false;
}

void SessionView::fileDragEnter(const juce::StringArray& files, int x, int y) {
    updateDragHighlight(x, y);

    // Show ghost preview if hovering over valid slot or new-track area
    if (dragHoverSceneIndex_ >= 0) {
        updateDragGhost(files, dragHoverTrackIndex_, dragHoverSceneIndex_);
    }
}

void SessionView::fileDragMove(const juce::StringArray& files, int x, int y) {
    int oldTrackIndex = dragHoverTrackIndex_;
    int oldSceneIndex = dragHoverSceneIndex_;

    updateDragHighlight(x, y);

    // Update ghost if slot changed
    if (dragHoverTrackIndex_ != oldTrackIndex || dragHoverSceneIndex_ != oldSceneIndex) {
        if (dragHoverSceneIndex_ >= 0) {
            updateDragGhost(files, dragHoverTrackIndex_, dragHoverSceneIndex_);
        } else {
            clearDragGhost();
        }
    }
}

void SessionView::fileDragExit(const juce::StringArray& /*files*/) {
    clearDragHighlight();
    clearDragGhost();
}

void SessionView::filesDropped(const juce::StringArray& files, int x, int y) {
    clearDragHighlight();
    clearDragGhost();

    auto gridLocalPoint = gridViewport->getLocalPoint(this, juce::Point<int>(x, y));
    gridLocalPoint +=
        juce::Point<int>(gridViewport->getViewPositionX(), gridViewport->getViewPositionY());

    int sceneRowHeight = CLIP_SLOT_HEIGHT + CLIP_SLOT_MARGIN;
    int trackIndex = getTrackIndexAtX(gridLocalPoint.getX());
    int sceneIndex = gridLocalPoint.getY() / sceneRowHeight;

    if (sceneIndex < 0 || sceneIndex >= numScenes_)
        return;

    juce::StringArray audioFiles;
    for (const auto& f : files) {
        if (isAudioFile(f))
            audioFiles.add(f);
    }
    if (audioFiles.isEmpty())
        return;

    TrackId hoveredTrackId = INVALID_TRACK_ID;
    if (trackIndex >= 0 && trackIndex < static_cast<int>(visibleTrackIds_.size()))
        hoveredTrackId = visibleTrackIds_[trackIndex];

    // Drop target decides: empty area → one new track per sample, existing
    // track → stack clips down the scene slots of that track.
    const bool expand = (hoveredTrackId == INVALID_TRACK_ID);

    auto& clipManager = ClipManager::getInstance();

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    double bpm = 120.0;
    if (timelineController_)
        bpm = timelineController_->getState().tempo.bpm;

    auto createClipOnTrack = [&](TrackId trackId, int sceneSlot, const juce::String& filePath) {
        juce::File audioFile(filePath);
        double fileDuration = 4.0;
        {
            std::unique_ptr<juce::AudioFormatReader> reader(
                formatManager.createReaderFor(audioFile));
            if (reader)
                fileDuration = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
        }

        ClipId newClipId = clipManager.createAudioClip(trackId, 0.0, fileDuration, filePath,
                                                       ClipView::Session, bpm);
        if (newClipId != INVALID_CLIP_ID) {
            UndoManager::getInstance().executeCommand(std::make_unique<SetClipNameCommand>(
                newClipId, audioFile.getFileNameWithoutExtension()));
            clipManager.setClipLoopEnabled(newClipId, true, bpm);
            clipManager.setClipSceneIndex(newClipId, sceneSlot);
        }
    };

    auto nextEmptySlot = [&](TrackId trackId, int startSlot) {
        while (startSlot < numScenes_ &&
               clipManager.getClipInSlot(trackId, startSlot) != INVALID_CLIP_ID) {
            ++startSlot;
        }
        return startSlot;
    };

    if (expand) {
        // Empty-area drop: one new track per sample, inserted in order.
        TrackId insertAfter = INVALID_TRACK_ID;

        for (const auto& filePath : audioFiles) {
            juce::String trackName = juce::File(filePath).getFileNameWithoutExtension();
            auto cmd =
                std::make_unique<CreateTrackCommand>(TrackType::Audio, trackName, insertAfter);
            auto* cmdPtr = cmd.get();
            UndoManager::getInstance().executeCommand(std::move(cmd));
            TrackId trackId = cmdPtr->getCreatedTrackId();
            if (trackId == INVALID_TRACK_ID)
                break;

            int slot = nextEmptySlot(trackId, sceneIndex);
            if (slot < numScenes_)
                createClipOnTrack(trackId, slot, filePath);

            insertAfter = trackId;
        }
    } else {
        // Append mode: stack all clips down the scenes of the hovered track.
        int slot = sceneIndex;
        for (const auto& filePath : audioFiles) {
            slot = nextEmptySlot(hoveredTrackId, slot);
            if (slot >= numScenes_)
                break;

            createClipOnTrack(hoveredTrackId, slot, filePath);
            ++slot;
        }
    }
}

void SessionView::updateDragHighlight(int x, int y) {
    // Convert to grid coordinates
    auto gridLocalPoint = gridViewport->getLocalPoint(this, juce::Point<int>(x, y));
    gridLocalPoint +=
        juce::Point<int>(gridViewport->getViewPositionX(), gridViewport->getViewPositionY());

    int sceneRowHeight = CLIP_SLOT_HEIGHT + CLIP_SLOT_MARGIN;

    int trackIndex = getTrackIndexAtX(gridLocalPoint.getX());
    int sceneIndex = gridLocalPoint.getY() / sceneRowHeight;

    // Validate scene index
    if (sceneIndex < 0 || sceneIndex >= numScenes_) {
        sceneIndex = -1;
    }

    // trackIndex == -1 is valid: means "past last track" (create new track zone)
    // Only clamp to -1 if truly out of bounds on the left
    if (trackIndex >= static_cast<int>(visibleTrackIds_.size())) {
        trackIndex = -1;
    }

    // Update highlight if slot changed
    if (trackIndex != dragHoverTrackIndex_ || sceneIndex != dragHoverSceneIndex_) {
        // Clear old highlight on previous slot
        if (dragHoverTrackIndex_ >= 0 && dragHoverSceneIndex_ >= 0) {
            updateClipSlotAppearance(dragHoverTrackIndex_, dragHoverSceneIndex_);
        }

        // Set new highlight
        dragHoverTrackIndex_ = trackIndex;
        dragHoverSceneIndex_ = sceneIndex;

        if (dragHoverTrackIndex_ >= 0 && dragHoverSceneIndex_ >= 0 &&
            dragHoverTrackIndex_ < static_cast<int>(clipSlots.size()) &&
            dragHoverSceneIndex_ < static_cast<int>(clipSlots[dragHoverTrackIndex_].size())) {
            auto* slot = clipSlots[dragHoverTrackIndex_][dragHoverSceneIndex_].get();
            if (slot) {
                // Highlight with accent color
                slot->setColour(juce::TextButton::buttonColourId,
                                DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.5f));
            }
        }

        // Repaint to update the "new track" overlay when hovering past last track
        if (dragHoverTrackIndex_ == -1)
            repaint();
    }
}

void SessionView::clearDragHighlight() {
    if (dragHoverTrackIndex_ >= 0 && dragHoverSceneIndex_ >= 0) {
        updateClipSlotAppearance(dragHoverTrackIndex_, dragHoverSceneIndex_);
    }
    dragHoverTrackIndex_ = -1;
    dragHoverSceneIndex_ = -1;

    // Always repaint — the "new track" overlay may have been painted in a
    // previous frame even if the current drag state doesn't show it (the mouse
    // can move from past-last-track to on-a-track between paint frames).
    repaint();
    if (gridViewport)
        gridViewport->repaint();
}

void SessionView::updateDragGhost(const juce::StringArray& files, int trackIndex, int sceneIndex) {
    if (files.isEmpty() || sceneIndex < 0) {
        clearDragGhost();
        return;
    }

    // Get first audio file from the list
    juce::String firstAudioFile;
    for (const auto& file : files) {
        if (isAudioFile(file)) {
            firstAudioFile = file;
            break;
        }
    }

    if (firstAudioFile.isEmpty()) {
        clearDragGhost();
        return;
    }

    // Extract filename without extension
    juce::File audioFile(firstAudioFile);
    juce::String filename = audioFile.getFileNameWithoutExtension();

    // Add count indicator if multiple files
    int audioFileCount = 0;
    for (const auto& file : files) {
        if (isAudioFile(file))
            audioFileCount++;
    }

    if (audioFileCount > 1) {
        filename += juce::String(" (+") + juce::String(audioFileCount - 1) + ")";
    }

    // Position ghost at the target slot (in grid coordinates)
    int sceneRowHeight = CLIP_SLOT_HEIGHT + CLIP_SLOT_MARGIN;

    int ghostX, ghostW;
    if (trackIndex >= 0) {
        ghostX = getTrackX(trackIndex);
        ghostW = (trackIndex < static_cast<int>(trackColumnWidths_.size()))
                     ? trackColumnWidths_[trackIndex]
                     : DEFAULT_CLIP_SLOT_WIDTH;
    } else {
        // Past last track — position ghost in "new track" column
        ghostX = getTotalTracksWidth();
        ghostW = DEFAULT_CLIP_SLOT_WIDTH;
    }
    int ghostY = sceneIndex * sceneRowHeight;

    // Update ghost label
    dragGhostLabel_->setText(filename, juce::dontSendNotification);
    dragGhostLabel_->setBounds(ghostX, ghostY, ghostW, CLIP_SLOT_HEIGHT);
    dragGhostLabel_->setVisible(true);
    dragGhostLabel_->toFront(false);
}

void SessionView::clearDragGhost() {
    if (dragGhostLabel_) {
        dragGhostLabel_->setVisible(false);
    }
}

bool SessionView::isAudioFile(const juce::String& filename) const {
    static const juce::StringArray audioExtensions = {".wav",  ".aiff", ".aif", ".mp3", ".ogg",
                                                      ".flac", ".m4a",  ".wma", ".opus"};

    for (const auto& ext : audioExtensions) {
        if (filename.endsWithIgnoreCase(ext)) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// DragAndDropTarget implementation (internal JUCE drags: plugins, clip slots)
// ============================================================================

#if JUCE_LINUX
namespace {
bool isInternalFilesDrag(const juce::DragAndDropTarget::SourceDetails& details) {
    if (auto* obj = details.description.getDynamicObject())
        return obj->getProperty("type").toString() == "files";
    return false;
}

juce::StringArray extractFilePathsFromDescription(const juce::var& description) {
    juce::StringArray paths;
    if (auto* obj = description.getDynamicObject()) {
        if (auto* arr = obj->getProperty("paths").getArray()) {
            for (const auto& v : *arr)
                paths.add(v.toString());
        }
    }
    return paths;
}
}  // namespace
#endif

// See SessionView.hpp for why this bridge exists and why it is Linux-only.
bool SessionView::acceptsInternalFilesDrag(const SourceDetails& details) {
#if JUCE_LINUX
    if (isInternalFilesDrag(details))
        return isInterestedInFileDrag(extractFilePathsFromDescription(details.description));
#endif
    juce::ignoreUnused(details);
    return false;
}

bool SessionView::handleInternalFilesDragEnter(const SourceDetails& details) {
#if JUCE_LINUX
    if (isInternalFilesDrag(details)) {
        fileDragEnter(extractFilePathsFromDescription(details.description),
                      details.localPosition.getX(), details.localPosition.getY());
        return true;
    }
#endif
    juce::ignoreUnused(details);
    return false;
}

bool SessionView::handleInternalFilesDragMove(const SourceDetails& details) {
#if JUCE_LINUX
    if (isInternalFilesDrag(details)) {
        fileDragMove(extractFilePathsFromDescription(details.description),
                     details.localPosition.getX(), details.localPosition.getY());
        return true;
    }
#endif
    juce::ignoreUnused(details);
    return false;
}

bool SessionView::handleInternalFilesDragExit(const SourceDetails& details) {
#if JUCE_LINUX
    if (isInternalFilesDrag(details)) {
        fileDragExit({});
        return true;
    }
#endif
    juce::ignoreUnused(details);
    return false;
}

bool SessionView::handleInternalFilesDrop(const SourceDetails& details) {
#if JUCE_LINUX
    if (isInternalFilesDrag(details)) {
        filesDropped(extractFilePathsFromDescription(details.description),
                     details.localPosition.getX(), details.localPosition.getY());
        return true;
    }
#endif
    juce::ignoreUnused(details);
    return false;
}

bool SessionView::isInterestedInDragSource(const SourceDetails& details) {
    if (auto* obj = details.description.getDynamicObject()) {
        auto type = obj->getProperty("type").toString();
        if (type == "plugin" || type == "sessionClip")
            return true;
    }
    return acceptsInternalFilesDrag(details);
}

void SessionView::itemDragEnter(const SourceDetails& details) {
    if (handleInternalFilesDragEnter(details))
        return;

    auto* obj = details.description.getDynamicObject();
    if (!obj)
        return;

    auto type = obj->getProperty("type").toString();
    if (type == "plugin") {
        showPluginDropOverlay_ = true;
        // Determine which track column is being hovered
        auto gridLocalPoint = gridViewport->getLocalPoint(this, details.localPosition);
        int hitX = gridLocalPoint.getX() + gridViewport->getViewPositionX();
        pluginDropTrackIndex_ = getTrackIndexAtX(hitX);
        repaint();
    } else if (type == "sessionClip") {
        // Highlight target slot
        updateDragHighlight(details.localPosition.getX(), details.localPosition.getY());
    }
}

void SessionView::itemDragMove(const SourceDetails& details) {
    if (handleInternalFilesDragMove(details))
        return;

    auto* obj = details.description.getDynamicObject();
    if (!obj)
        return;

    auto type = obj->getProperty("type").toString();
    if (type == "plugin") {
        auto gridLocalPoint = gridViewport->getLocalPoint(this, details.localPosition);
        int hitX = gridLocalPoint.getX() + gridViewport->getViewPositionX();
        int oldIndex = pluginDropTrackIndex_;
        pluginDropTrackIndex_ = getTrackIndexAtX(hitX);
        if (pluginDropTrackIndex_ != oldIndex)
            repaint();
    } else if (type == "sessionClip") {
        updateDragHighlight(details.localPosition.getX(), details.localPosition.getY());
    }
}

void SessionView::itemDragExit(const SourceDetails& details) {
    if (handleInternalFilesDragExit(details))
        return;

    showPluginDropOverlay_ = false;
    pluginDropTrackIndex_ = -1;
    clearDragHighlight();
    repaint();
}

void SessionView::itemDropped(const SourceDetails& details) {
    if (handleInternalFilesDrop(details))
        return;

    showPluginDropOverlay_ = false;
    pluginDropTrackIndex_ = -1;
    clearDragHighlight();
    repaint();

    auto* obj = details.description.getDynamicObject();
    if (!obj)
        return;

    auto type = obj->getProperty("type").toString();

    if (type == "plugin") {
        auto device = TrackManager::deviceInfoFromPluginObject(*obj);

        // Determine target track from drop position
        auto gridLocalPoint = gridViewport->getLocalPoint(this, details.localPosition);
        int hitX = gridLocalPoint.getX() + gridViewport->getViewPositionX();
        int trackIndex = getTrackIndexAtX(hitX);

        if (trackIndex >= 0 && trackIndex < static_cast<int>(visibleTrackIds_.size())) {
            // Drop on existing track — add plugin to chain
            TrackId trackId = visibleTrackIds_[trackIndex];
            TrackManager::getInstance().addDeviceToTrack(trackId, device);
        } else {
            // Drop past last track — create new track with plugin
            TrackType trackType = TrackType::Audio;
            juce::String pluginName = obj->getProperty("name").toString();
            auto cmd =
                std::make_unique<CreateTrackWithDeviceCommand>(pluginName, trackType, device);
            UndoManager::getInstance().executeCommand(std::move(cmd));
        }
    } else if (type == "sessionClip") {
        // Clip slot drag — handle in Phase 3
        ClipId clipId = static_cast<ClipId>(static_cast<int>(obj->getProperty("clipId")));
        TrackId sourceTrackId = static_cast<TrackId>(static_cast<int>(obj->getProperty("trackId")));
        int sourceSceneIndex = static_cast<int>(obj->getProperty("sceneIndex"));

        // Calculate target from drop coordinates
        auto gridLocalPoint = gridViewport->getLocalPoint(this, details.localPosition);
        gridLocalPoint +=
            juce::Point<int>(gridViewport->getViewPositionX(), gridViewport->getViewPositionY());

        int sceneRowHeight = CLIP_SLOT_HEIGHT + CLIP_SLOT_MARGIN;
        int targetTrackIndex = getTrackIndexAtX(gridLocalPoint.getX());
        int targetSceneIndex = gridLocalPoint.getY() / sceneRowHeight;

        // Validate
        if (targetTrackIndex < 0 || targetTrackIndex >= static_cast<int>(visibleTrackIds_.size()))
            return;
        if (targetSceneIndex < 0 || targetSceneIndex >= numScenes_)
            return;

        TrackId targetTrackId = visibleTrackIds_[targetTrackIndex];

        // Skip if dropping on same slot
        if (targetTrackId == sourceTrackId && targetSceneIndex == sourceSceneIndex)
            return;

        // Check if target slot is occupied
        auto& clipManager = ClipManager::getInstance();
        if (clipManager.getClipInSlot(targetTrackId, targetSceneIndex) != INVALID_CLIP_ID)
            return;  // Target occupied, reject

        bool isAltHeld = juce::ModifierKeys::getCurrentModifiers().isAltDown();
        if (isAltHeld) {
            // Alt+drag = duplicate clip to target slot
            auto cmd =
                DuplicateClipCommand::forSessionSlot(clipId, targetTrackId, targetSceneIndex);
            UndoManager::getInstance().executeCommand(std::move(cmd));
        } else {
            // Regular drag = move clip to target slot
            auto cmd =
                std::make_unique<MoveSessionClipCommand>(clipId, targetTrackId, targetSceneIndex);
            UndoManager::getInstance().executeCommand(std::move(cmd));
        }
    }
}

}  // namespace magda
