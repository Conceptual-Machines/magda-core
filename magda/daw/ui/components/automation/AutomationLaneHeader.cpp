#include "AutomationLaneHeader.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "../../../core/AutomationCommands.hpp"
#include "../../../core/AutomationManager.hpp"
#include "../../../core/ParameterUtils.hpp"
#include "../../../core/TrackManager.hpp"
#include "../../../core/UndoManager.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "AutomationLaneComponent.hpp"
#include "BinaryData.h"

namespace magda {

namespace {

// Shared base for the automation-lane header buttons (snap beat grid / snap value
// / arm / bypass / delete). All five share the same rounded-rect chrome —
// same corner radius, fill, and 1px darker border as SmallButtonLookAndFeel
// — so they read as a single unified strip. Subclasses only supply the glyph.
// Off: SURFACE background with a neutral-grey glyph.
// On:  activeColour background with a white glyph (high contrast reversal).
class LaneHeaderButton : public juce::Button {
  public:
    LaneHeaderButton(const juce::String& name, juce::Colour activeColour)
        : juce::Button(name), activeColour_(activeColour) {}

    void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override {
        auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        constexpr float corner = 3.0f;

        const bool on = getToggleState();
        const auto surface = DarkTheme::getColour(DarkTheme::SURFACE);
        // Blended scheme: the active state is a muted accent (mixed well toward
        // the panel surface) rather than a saturated fill, so the buttons sit
        // quietly in the header instead of standing out against the dark bg.
        juce::Colour bg = on ? activeColour_.interpolatedWith(surface, 0.62f) : surface;
        if (isButtonDown)
            bg = bg.darker(0.2f);
        else if (isMouseOver)
            bg = bg.brighter(0.1f);

        g.setColour(bg);
        g.fillRoundedRectangle(bounds, corner);
        g.setColour(bg.darker(0.15f));
        g.drawRoundedRectangle(bounds, corner, 1.0f);

        // Glyph: a soft accent tint when on (reads as active without a loud
        // fill), neutral grey when off.
        juce::Colour glyph = on ? activeColour_.brighter(0.5f) : juce::Colour(0xFFB3B3B3);
        paintGlyph(g, glyph);
    }

  protected:
    virtual void paintGlyph(juce::Graphics& g, juce::Colour colour) = 0;

  private:
    juce::Colour activeColour_;
};

// Snap toggles: show an SVG glyph. The SVG authors its shapes in #B3B3B3, so
// we recolour-replace into whatever glyph colour the base class dictates.
class SnapIconLaneButton : public LaneHeaderButton {
  public:
    SnapIconLaneButton(const juce::String& name, const void* svgData, int svgSize)
        : LaneHeaderButton(name, DarkTheme::getColour(DarkTheme::ACCENT_BLUE)) {
        setClickingTogglesState(true);
        drawable_ = juce::Drawable::createFromImageData(svgData, svgSize);
    }

    void paintGlyph(juce::Graphics& g, juce::Colour colour) override {
        if (drawable_ == nullptr)
            return;
        auto copy = drawable_->createCopy();
        copy->replaceColour(juce::Colour(0xFFB3B3B3), colour);
        copy->drawWithin(g, getLocalBounds().toFloat().reduced(1.0f),
                         juce::RectanglePlacement::centred, 1.0f);
    }

  private:
    std::unique_ptr<juce::Drawable> drawable_;
};

// Delete button: always uses the reddish-purple "danger" fill whether toggled
// or not (matches the device-header × button in NodeComponent). Overrides the
// base background so the user immediately reads it as destructive.
class DeleteLaneButton : public LaneHeaderButton {
  public:
    DeleteLaneButton()
        : LaneHeaderButton(
              "Delete", DarkTheme::getColour(DarkTheme::ACCENT_PURPLE)
                            .interpolatedWith(DarkTheme::getColour(DarkTheme::STATUS_ERROR), 0.5f)
                            .darker(0.2f)) {}

    void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override {
        auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        constexpr float corner = 3.0f;

        // Blended scheme: sit on the panel surface like the toggles; the X
        // glyph carries a muted purple/red tint so it still reads as the
        // destructive action without a loud fill.
        const auto surface = DarkTheme::getColour(DarkTheme::SURFACE);
        const auto accent =
            DarkTheme::getColour(DarkTheme::ACCENT_PURPLE)
                .interpolatedWith(DarkTheme::getColour(DarkTheme::STATUS_ERROR), 0.5f);
        juce::Colour bg = surface;
        if (isButtonDown)
            bg = accent.interpolatedWith(surface, 0.62f);
        else if (isMouseOver)
            bg = accent.interpolatedWith(surface, 0.78f);

        g.setColour(bg);
        g.fillRoundedRectangle(bounds, corner);
        g.setColour(bg.darker(0.15f));
        g.drawRoundedRectangle(bounds, corner, 1.0f);

        paintGlyph(g, accent.brighter(0.4f));
    }

    void paintGlyph(juce::Graphics& g, juce::Colour colour) override {
        g.setColour(colour);
        g.setFont(FontManager::getInstance().getUIFontBold(13.0f));
        g.drawText(juce::String::fromUTF8("\xc3\x97"), getLocalBounds().toFloat(),
                   juce::Justification::centred, false);
    }
};

// Bypass button: broken-ring power glyph drawn procedurally, so we don't need
// a separate SVG and the stroke stays crisp at the 20px button size. Off =
// grey on SURFACE, On = white on cyan — same colour rules as snap toggles.
class PowerGlyphButton : public LaneHeaderButton {
  public:
    PowerGlyphButton() : LaneHeaderButton("Bypass", DarkTheme::getColour(DarkTheme::ACCENT_BLUE)) {
        setClickingTogglesState(true);
    }

    void paintGlyph(juce::Graphics& g, juce::Colour colour) override {
        auto bounds = getLocalBounds().toFloat();
        // Leave padding so the glyph doesn't kiss the border.
        auto glyph = bounds.reduced(bounds.getWidth() * 0.22f, bounds.getHeight() * 0.22f);

        const float stroke = juce::jmax(1.4f, glyph.getWidth() * 0.12f);
        const float cx = glyph.getCentreX();
        const float cy = glyph.getCentreY();
        const float radius = glyph.getWidth() * 0.5f - stroke * 0.5f;

        // Broken ring: arc from ~20° past top going clockwise all the way
        // around, leaving a gap at the top where the vertical stem passes
        // through. Angles in JUCE are radians, 0 = 12 o'clock, clockwise.
        constexpr float gap = 0.6f;  // Half-angle of the top gap, radians.
        juce::Path ring;
        ring.addCentredArc(cx, cy, radius, radius, 0.0f,
                           gap,                                      // start angle
                           juce::MathConstants<float>::twoPi - gap,  // end angle
                           true);

        g.setColour(colour);
        g.strokePath(ring, juce::PathStrokeType(stroke, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));

        // Vertical stem through the gap.
        const float stemTop = cy - radius - stroke * 0.3f;
        const float stemBottom = cy - radius * 0.15f;
        g.drawLine(cx, stemTop, cx, stemBottom, stroke);
    }
};

}  // namespace

std::unique_ptr<AutoLaneHeaderButtons> makeAutoLaneHeaderButtons(AutomationLaneId laneId,
                                                                 juce::Component& host) {
    auto entry = std::make_unique<AutoLaneHeaderButtons>();
    entry->laneId = laneId;

    entry->snapEditGridBtn =
        std::make_unique<SnapIconLaneButton>("snapEditsToBeatGrid", BinaryData::horizontal_snap_svg,
                                             BinaryData::horizontal_snap_svgSize);
    entry->snapEditGridBtn->setTooltip("Snap edits to beat grid");
    host.addAndMakeVisible(*entry->snapEditGridBtn);

    entry->snapValueBtn = std::make_unique<SnapIconLaneButton>(
        "snapValue", BinaryData::vertical_snap_svg, BinaryData::vertical_snap_svgSize);
    entry->snapValueBtn->setTooltip("Snap values to parameter grid");
    host.addAndMakeVisible(*entry->snapValueBtn);

    // Bypass button uses a power glyph, so the "on" visual (cyan fill)
    // represents automation-enabled — not automation-bypassed. Toggle
    // state is the inverse of lane->bypass, and the click handler flips
    // the underlying bypass flag accordingly.
    entry->bypassBtn = std::make_unique<PowerGlyphButton>();
    entry->bypassBtn->setTooltip("Automation on/off");
    host.addAndMakeVisible(*entry->bypassBtn);

    // Delete button: matches the device-header × in NodeComponent — same
    // reddish-purple fill, same × glyph. Replaces the old "lane options"
    // menu; clearing points is handled via the Backspace key instead.
    entry->deleteBtn = std::make_unique<DeleteLaneButton>();
    entry->deleteBtn->setTooltip("Delete automation lane");
    host.addAndMakeVisible(*entry->deleteBtn);

    // Wire click handlers. Capture laneId by value so the lambda survives
    // rebuilds (the raw pointer would dangle if the entry is later destroyed,
    // but the laneId lookup is safe).
    AutomationLaneId id = laneId;
    entry->snapEditGridBtn->onClick = [id]() {
        auto& mgr = AutomationManager::getInstance();
        if (const auto* lane = mgr.getLane(id))
            mgr.setLaneSnapEditsToBeatGrid(id, !lane->snapEditsToBeatGrid);
    };
    entry->snapValueBtn->onClick = [id]() {
        auto& mgr = AutomationManager::getInstance();
        if (const auto* lane = mgr.getLane(id))
            mgr.setLaneSnapValue(id, !lane->snapValue);
    };
    entry->bypassBtn->onClick = [id]() {
        auto& mgr = AutomationManager::getInstance();
        if (const auto* lane = mgr.getLane(id))
            mgr.setLaneBypass(id, !lane->bypass);
    };
    entry->deleteBtn->onClick = [id]() {
        UndoManager::getInstance().executeCommand(
            std::make_unique<DeleteAutomationLaneCommand>(id));
    };

    return entry;
}

void syncAutoLaneHeaderButtonStates(AutoLaneHeaderButtons& buttons,
                                    const AutomationLaneInfo& lane) {
    buttons.snapEditGridBtn->setToggleState(lane.snapEditsToBeatGrid, juce::dontSendNotification);
    buttons.snapValueBtn->setToggleState(lane.snapValue, juce::dontSendNotification);
    // Power glyph: inverted — "on" means automation active, not bypassed.
    buttons.bypassBtn->setToggleState(!lane.bypass, juce::dontSendNotification);
}

void layoutAutoLaneHeaderButtons(AutoLaneHeaderButtons& buttons, const AutomationLaneInfo& lane,
                                 int laneTopY, int topInset) {
    constexpr int kBtnSize = 20;
    constexpr int kBtnGap = 3;
    constexpr int kLeftMargin = 6;
    constexpr int kTopMargin = 4;

    const bool inView = lane.expanded;
    buttons.snapEditGridBtn->setVisible(inView);
    buttons.snapValueBtn->setVisible(inView);
    buttons.bypassBtn->setVisible(inView);
    buttons.deleteBtn->setVisible(inView);

    if (!inView)
        return;

    int btnY = laneTopY + topInset + AutomationLaneComponent::HEADER_HEIGHT + kTopMargin;
    int x = kLeftMargin;
    auto place = [&](juce::Button& b) {
        b.setBounds(x, btnY, kBtnSize, kBtnSize);
        x += (kBtnSize + kBtnGap);
    };
    place(*buttons.snapEditGridBtn);
    place(*buttons.snapValueBtn);
    place(*buttons.bypassBtn);
    place(*buttons.deleteBtn);
}

void paintAutomationLaneHeader(juce::Graphics& g, const AutomationLaneInfo& lane, int laneTopY,
                               int width, int laneHeight, int topInset) {
    const int y = laneTopY + topInset;

    // Header area for this automation lane
    auto headerArea = juce::Rectangle<int>(0, y, width, AutomationLaneComponent::HEADER_HEIGHT);

    // Header background
    g.setColour(juce::Colour(0xFF252525));
    g.fillRect(headerArea);

    // Header border
    g.setColour(juce::Colour(0xFF333333));
    g.drawHorizontalLine(headerArea.getBottom() - 1, static_cast<float>(headerArea.getX()),
                         static_cast<float>(headerArea.getRight()));

    // Parameter name
    g.setColour(juce::Colour(0xFFCCCCCC));
    g.setFont(FontManager::getInstance().getUIFont(11.0f));
    auto nameArea = headerArea.reduced(4, 2);
    g.drawText(lane.getDisplayName(), nameArea, juce::Justification::centredLeft);

    // Track-name watermark on the right edge so a lane reads which track /
    // device it belongs to (e.g. "Gain ........ Master") without crowding the
    // param name. Faint so it sits behind the active content.
    if (const auto* track = TrackManager::getInstance().getTrack(lane.target.devicePath.trackId)) {
        if (track->name.isNotEmpty()) {
            g.setColour(juce::Colour(0xFFCCCCCC).withAlpha(0.32f));
            g.setFont(FontManager::getInstance().getUIFont(10.0f));
            g.drawText(track->name, nameArea, juce::Justification::centredRight);
        }
    }

    // Value tick marks and labels in the lane content area
    {
        // Match CurveEditorBase's padding (5px) so ticks align with grid lines
        constexpr int curvePadding = 5;
        int contentTop = y + AutomationLaneComponent::HEADER_HEIGHT + curvePadding;
        int contentBottom =
            y + laneHeight - AutomationLaneComponent::RESIZE_HANDLE_HEIGHT - curvePadding;
        int contentHeight = contentBottom - contentTop;
        float rightEdge = static_cast<float>(width);
        constexpr float tickLen = 5.0f;

        if (contentHeight > 20) {
            auto paramInfo = getParameterInfoForTarget(lane.target);

            // Build grid values: pairs of (normalized, label)
            std::vector<std::pair<double, juce::String>> gridValues;
            if (paramInfo.scale == ParameterScale::FaderDB) {
                const std::pair<double, const char*> dbValues[] = {
                    {6.0, "6"},     {3.0, "3"},     {0.0, "0"},     {-6.0, "-6"},  {-12.0, "-12"},
                    {-18.0, "-18"}, {-24.0, "-24"}, {-36.0, "-36"}, {-48.0, "-48"}};
                for (const auto& [db, label] : dbValues) {
                    float norm =
                        ParameterUtils::realToNormalized(static_cast<float>(db), paramInfo);
                    gridValues.push_back({static_cast<double>(norm), label});
                }
            } else if (lane.target.kind == ControlTarget::Kind::TrackPan) {
                gridValues.push_back(
                    {static_cast<double>(ParameterUtils::realToNormalized(1.0f, paramInfo)), "R"});
                gridValues.push_back(
                    {static_cast<double>(ParameterUtils::realToNormalized(0.5f, paramInfo)),
                     "50R"});
                gridValues.push_back(
                    {static_cast<double>(ParameterUtils::realToNormalized(0.0f, paramInfo)), "C"});
                gridValues.push_back(
                    {static_cast<double>(ParameterUtils::realToNormalized(-0.5f, paramInfo)),
                     "50L"});
                gridValues.push_back(
                    {static_cast<double>(ParameterUtils::realToNormalized(-1.0f, paramInfo)), "L"});
            } else if (paramInfo.isBipolar()) {
                // Bipolar params (EQ gain, pitch, etc): symmetric labels
                // around zero so the 0 line lands mid-lane. Use the larger
                // |bound| so extremes land on both ends regardless of
                // asymmetry.
                float absMax = std::max(std::abs(paramInfo.minValue), std::abs(paramInfo.maxValue));
                const double realTicks[] = {absMax, absMax * 0.5, 0.0, -absMax * 0.5, -absMax};
                for (double real : realTicks) {
                    float norm =
                        ParameterUtils::realToNormalized(static_cast<float>(real), paramInfo);
                    juce::String label;
                    int rounded = static_cast<int>(std::round(real));
                    if (rounded > 0)
                        label = "+" + juce::String(rounded);
                    else
                        label = juce::String(rounded);
                    label += paramInfo.unit;
                    gridValues.push_back({static_cast<double>(norm), label});
                }
            } else if (paramInfo.scale == ParameterScale::Discrete && !paramInfo.choices.empty()) {
                // Discrete: use the choices array as label source. Each
                // index maps to a real value (0..N-1) — sample evenly so
                // the lane shows musical labels (e.g. "1 Bar", "1/4",
                // "1/8") instead of falling through to the 10% fallback.
                // If the parameter curated a sparse labelTicks set (e.g.
                // sync division skipping the triplet/dotted entries that
                // snap on playback), use it directly so the thinner can't
                // re-introduce the labels we deliberately excluded.
                if (!paramInfo.labelTicks.empty()) {
                    for (const auto& [realValue, label] : paramInfo.labelTicks) {
                        float norm = ParameterUtils::realToNormalized(realValue, paramInfo);
                        gridValues.push_back({static_cast<double>(norm), label});
                    }
                } else {
                    int numChoices = static_cast<int>(paramInfo.choices.size());
                    for (int i = 0; i < numChoices; ++i) {
                        float norm =
                            ParameterUtils::realToNormalized(static_cast<float>(i), paramInfo);
                        gridValues.push_back(
                            {static_cast<double>(norm), paramInfo.choices[static_cast<size_t>(i)]});
                    }
                }
            } else if (paramInfo.unit.isNotEmpty()) {
                // Unipolar with unit: evenly spaced in normalized space,
                // labelled with the real value in the parameter's own unit.
                for (double norm : {0.0, 0.25, 0.5, 0.75, 1.0}) {
                    float real =
                        ParameterUtils::normalizedToReal(static_cast<float>(norm), paramInfo);
                    juce::String label =
                        juce::String(static_cast<int>(std::round(real))) + paramInfo.unit;
                    gridValues.push_back({norm, label});
                }
            } else if (paramInfo.displayText) {
                // displayText wraps TE's valueToString, which expects a
                // plugin-native value — NOT normalized [0,1]. Sample the
                // REAL value at each visual position so any scaleAnchor
                // skew is honoured, then project from info-range onto the
                // TE-native range so the provider sees what it expects.
                const float teSpan = paramInfo.teMaxValue - paramInfo.teMinValue;
                const float infoSpan = paramInfo.maxValue - paramInfo.minValue;
                for (double norm : {0.0, 0.25, 0.5, 0.75, 1.0}) {
                    float teRaw;
                    if (infoSpan > 0.0f) {
                        float real =
                            ParameterUtils::normalizedToReal(static_cast<float>(norm), paramInfo);
                        float normInInfo = (real - paramInfo.minValue) / infoSpan;
                        teRaw = paramInfo.teMinValue + normInInfo * teSpan;
                    } else {
                        teRaw = paramInfo.teMinValue + static_cast<float>(norm) * teSpan;
                    }
                    auto text = paramInfo.displayText->format(teRaw);
                    gridValues.push_back(
                        {norm, text.isNotEmpty()
                                   ? text
                                   : juce::String(static_cast<int>(norm * 100)) + "%"});
                }
            } else if (!paramInfo.valueTable.empty()) {
                for (double norm : {0.0, 0.25, 0.5, 0.75, 1.0}) {
                    int idx = juce::jlimit(
                        0, static_cast<int>(paramInfo.valueTable.size()) - 1,
                        static_cast<int>(std::round(norm * (paramInfo.valueTable.size() - 1))));
                    gridValues.push_back(
                        {norm, paramInfo.valueTable[static_cast<size_t>(idx)].trim()});
                }
            } else {
                for (int i = 1; i < 10; ++i)
                    gridValues.push_back({i / 10.0, juce::String(i * 10) + "%"});
            }

            g.setFont(FontManager::getInstance().getUIFont(8.0f));
            constexpr int labelH = 10;
            // Thin labels to what vertically fits. Very short lanes collapse to
            // a single centre label; otherwise evenly spaced samples are picked
            // across the range (endpoints included whenever at least two labels
            // fit), while tall lanes show every sample.
            constexpr int labelSpacing = labelH + 6;
            int maxLabels = juce::jmax(1, contentHeight / labelSpacing);
            if (maxLabels < static_cast<int>(gridValues.size())) {
                std::vector<std::pair<double, juce::String>> thinned;
                thinned.reserve(static_cast<size_t>(maxLabels));
                if (maxLabels == 1) {
                    thinned.push_back(gridValues[gridValues.size() / 2]);
                } else {
                    const double srcMax = static_cast<double>(gridValues.size() - 1);
                    for (int i = 0; i < maxLabels; ++i) {
                        int srcIdx = static_cast<int>(
                            std::round(static_cast<double>(i) * srcMax / (maxLabels - 1)));
                        thinned.push_back(gridValues[static_cast<size_t>(srcIdx)]);
                    }
                }
                gridValues = std::move(thinned);
            }
            for (const auto& [norm, label] : gridValues) {
                int tickY = contentTop + static_cast<int>((1.0 - norm) * contentHeight);
                // Tick
                g.setColour(juce::Colour(0x66FFFFFF));
                g.drawHorizontalLine(tickY, rightEdge - tickLen, rightEdge);
                // Label — clamp vertically so min/max labels flush against the
                // lane edges instead of clipping against the header / resize
                // handle.
                g.setColour(juce::Colour(0xFF777777));
                int labelTop = juce::jlimit(contentTop, contentBottom - labelH, tickY - 5);
                auto labelBounds = juce::Rectangle<int>(2, labelTop, width - 10, labelH);
                g.drawText(label, labelBounds, juce::Justification::centredRight);
            }
        }
    }

    // Bottom border — matches the resize handle area on the content side
    int borderY = y + laneHeight - AutomationLaneComponent::RESIZE_HANDLE_HEIGHT;
    g.setColour(juce::Colour(0xFF333333));
    g.fillRect(0, borderY, width, AutomationLaneComponent::RESIZE_HANDLE_HEIGHT);
    g.setColour(juce::Colour(0xFF444444));
    g.drawHorizontalLine(borderY, 0.0f, static_cast<float>(width));
}

}  // namespace magda
