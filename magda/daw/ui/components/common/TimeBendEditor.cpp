#include "ui/components/common/TimeBendEditor.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "BinaryData.h"
#include "DisplayListGraphics.hpp"
#include "magda/sdk/curveedit/CurveEditor.hpp"
#include "magda/sdk/display/DisplayList.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SdkColourRoles.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

namespace {

using sdk::sequencer::TimeBend;
using sdk::sequencer::TimeBendShape;

constexpr int kHeaderHeight = 20;
/// Narrower than this, AMOUNT and CYCLES move to a second header row.
constexpr int kOneRowWidth = 500;
constexpr int kGap = 4;
constexpr std::array<int, 3> kSpans{1, 2, 4};
/// The GRID menu: a label and a division in beats.
constexpr std::array<std::pair<const char*, double>, 7> kDivisions{{{"1/4", 1.0},
                                                                    {"1/8", 0.5},
                                                                    {"1/8T", 1.0 / 3.0},
                                                                    {"1/16", 0.25},
                                                                    {"1/16T", 1.0 / 6.0},
                                                                    {"1/32", 0.125},
                                                                    {"1/64", 0.0625}}};
constexpr std::array<TimeBendShape, 7> kShapes{TimeBendShape::Straight,
                                               TimeBendShape::Rush,
                                               TimeBendShape::Drag,
                                               TimeBendShape::RushThenDrag,
                                               TimeBendShape::HoldThenCatchUp,
                                               TimeBendShape::Backwards,
                                               TimeBendShape::ThereAndBack};
/// Samples of the heard curve per cycle, for its line and the step crossings.
constexpr int kCurveSamples = 256;

sdk::CurveEditorModifiers modifiersOf(const juce::ModifierKeys& mods) {
    return {mods.isShiftDown(), mods.isCommandDown(), mods.isAltDown(), mods.isPopupMenu()};
}

sdk::CurveEditorPointer pointerOf(const juce::MouseEvent& e) {
    return {e.position.x, e.position.y, modifiersOf(e.mods), e.mods.isLeftButtonDown()};
}

/// The app's small toggle chip: its font, with accent fill and text while on.
void styleChip(juce::TextButton& button) {
    const auto accent = ActiveTheme::getAccentColour();
    button.setClickingTogglesState(false);
    button.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    button.setColour(juce::TextButton::buttonColourId,
                     ActiveTheme::getColour(ActiveTheme::SURFACE));
    button.setColour(juce::TextButton::buttonOnColourId, accent.withAlpha(0.22f));
    button.setColour(juce::TextButton::textColourOffId, ActiveTheme::getSecondaryTextColour());
    button.setColour(juce::TextButton::textColourOnId, accent);
}

void styleLabel(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(FontManager::getInstance().getUIFont(9.0f));
    label.setColour(juce::Label::textColourId, ActiveTheme::getSecondaryTextColour());
    label.setJustificationType(juce::Justification::centredRight);
}

}  // namespace

/** @brief The curve field: the SDK editor core, and what the bend draws over it. */
class TimeBendEditor::Canvas : public juce::Component {
  public:
    explicit Canvas(TimeBendEditor& owner) : owner_(owner) {
        setWantsKeyboardFocus(true);
        core_.setAllowSteps(false);
        core_.setGrid(4, 4);
        core_.setPadding(6);
        core_.setTextMeasure([](std::string_view text, float fontSize) {
            const auto font = FontManager::getInstance().getUIFont(fontSize);
            return static_cast<float>(juce::GlyphArrangement::getStringWidthInt(
                font, juce::String::fromUTF8(text.data(), static_cast<int>(text.size()))));
        });
    }

    void load(const TimeBend& bend) {
        const auto curve = bend.curve();
        if (!core_.editorPoints().empty() && core_.editorPoints().size() == curve.size())
            core_.refreshPoints(curve);
        else
            core_.setPoints(curve);
        repaint();
    }

    /// The grid is the division's steps both ways, so a snapped point sits on a step; beats and
    /// bars are drawn over it.
    void setGrid(double cycleBeats, double beatsPerBar, double division) {
        cycleBeats_ = cycleBeats;
        beatsPerBar_ = beatsPerBar;
        steps_ = division > 0.0 ? cycleBeats / division : 0.0;
        const int divisions = steps_ >= 1.0 && steps_ <= 128.0 ? juce::roundToInt(steps_) : 4;
        core_.setGrid(divisions, divisions);
        repaint();
    }

    void setSnap(bool snap) {
        core_.setSnap(snap, snap, false);
    }

    void setPlayhead(float phase) {
        if (phase == playhead_)
            return;
        playhead_ = phase;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        core_.setSize(getWidth(), getHeight());
        core_.render(displayList_);
        sdk::juce_host::drawDisplayList(
            g, displayList_,
            [](sdk::display::ColourRole role) {
                if (role == sdk::display::ColourRole::Background)
                    return ActiveTheme::getColour(ActiveTheme::CURVE_BACKGROUND);
                if (role == sdk::display::ColourRole::Curve)
                    return ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY);
                return sdkRoleColour(role);
            },
            [](float size) { return FontManager::getInstance().getUIFont(size); });
        paintBendOverlay(g);
    }

    void resized() override {
        core_.setSize(getWidth(), getHeight());
    }

    void mouseDown(const juce::MouseEvent& e) override {
        grabKeyboardFocus();
        apply(core_.pointerDown(pointerOf(e)));
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        apply(core_.pointerDrag(pointerOf(e)));
    }
    void mouseUp(const juce::MouseEvent& e) override {
        apply(core_.pointerUp(pointerOf(e)));
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override {
        apply(core_.doubleClick(pointerOf(e)));
    }
    void mouseMove(const juce::MouseEvent& e) override {
        apply(core_.pointerMove(pointerOf(e)));
        updateCursor(e.mods);
    }
    void mouseExit(const juce::MouseEvent&) override {
        apply(core_.pointerExit());
    }

    bool keyPressed(const juce::KeyPress& key) override {
        sdk::CurveEditorKey coreKey;
        coreKey.mods = modifiersOf(key.getModifiers());
        if (key.isKeyCode(juce::KeyPress::deleteKey))
            coreKey.code = sdk::CurveEditorKey::Code::Delete;
        else if (key.isKeyCode(juce::KeyPress::backspaceKey))
            coreKey.code = sdk::CurveEditorKey::Code::Backspace;
        else if (key.getTextCharacter() != 0) {
            coreKey.code = sdk::CurveEditorKey::Code::Character;
            coreKey.character = static_cast<char32_t>(
                juce::CharacterFunctions::toLowerCase(key.getTextCharacter()));
        }
        const auto response = core_.keyPressed(coreKey);
        apply(response);
        return response.handled;
    }

  private:
    void apply(const sdk::CurveEditorResponse& response) {
        if (response.preview) {
            auto previewed = owner_.bend_;
            const auto points = core_.effectivePoints();
            previewed.setCurve(points);
            previewed.on = true;
            if (owner_.onPreview)
                owner_.onPreview(previewed);
        }
        if (response.commit != sdk::CurveEditorResponse::Commit::None) {
            auto after = owner_.bend_;
            const auto points = core_.points();
            after.setCurve(points);
            owner_.commit(after);
        }
        if (response.repaint || response.preview ||
            response.commit != sdk::CurveEditorResponse::Commit::None)
            repaint();
    }

    void updateCursor(const juce::ModifierKeys& mods) {
        switch (core_.cursor(modifiersOf(mods))) {
            case sdk::CurveEditorCursor::PointingHand:
                setMouseCursor(juce::MouseCursor::PointingHandCursor);
                break;
            case sdk::CurveEditorCursor::Copying:
                setMouseCursor(juce::MouseCursor::CopyingCursor);
                break;
            case sdk::CurveEditorCursor::DraggingHand:
                setMouseCursor(juce::MouseCursor::DraggingHandCursor);
                break;
            case sdk::CurveEditorCursor::Normal:
                setMouseCursor(juce::MouseCursor::NormalCursor);
                break;
        }
    }

    juce::Point<float> toPixel(double x, double y) const {
        return {static_cast<float>(core_.xToPixelF(x)), static_cast<float>(core_.yToPixelF(y))};
    }

    void paintBendOverlay(juce::Graphics& g) {
        const auto& bend = owner_.bend_;
        const auto faint = ActiveTheme::getSecondaryTextColour().withAlpha(0.35f);

        // Beats, and brighter, bars, across the cycle.
        if (cycleBeats_ > 0.0 && cycleBeats_ <= 64.0) {
            for (int beat = 1; static_cast<double>(beat) < cycleBeats_; ++beat) {
                const bool bar =
                    beatsPerBar_ > 0.0 && std::abs(std::fmod(beat, beatsPerBar_)) < 1.0e-9;
                g.setColour(faint.withAlpha(bar ? 0.45f : 0.2f));
                g.drawVerticalLine(juce::roundToInt(toPixel(beat / cycleBeats_, 0.0).x),
                                   toPixel(0.0, 1.0).y, toPixel(0.0, 0.0).y);
            }
        }

        // A row per pattern step, so a crossing reads as the step it plays.
        if (steps_ > 0.0 && steps_ <= 128.0) {
            g.setColour(faint.withAlpha(0.12f));
            for (int k = 1; k < static_cast<int>(steps_); ++k) {
                const auto y = toPixel(0.0, k / steps_).y;
                g.drawHorizontalLine(juce::roundToInt(y), toPixel(0.0, 0.0).x, toPixel(1.0, 0.0).x);
            }
        }

        // The pattern as written.
        juce::Path diagonal;
        diagonal.startNewSubPath(toPixel(0.0, 0.0));
        diagonal.lineTo(toPixel(1.0, 1.0));
        const float dashes[] = {4.0f, 4.0f};
        juce::Path dashed;
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, diagonal, dashes, 2);
        g.setColour(faint);
        g.fillPath(dashed);

        // The curve heard, once AMOUNT pulls it toward the diagonal.
        const bool heardDiffers = bend.amount < 0.999f;
        std::array<double, kCurveSamples + 1> heard{};
        for (int i = 0; i <= kCurveSamples; ++i)
            heard[static_cast<size_t>(i)] = bend.read(static_cast<double>(i) / kCurveSamples);
        if (heardDiffers) {
            juce::Path line;
            for (int i = 0; i <= kCurveSamples; ++i) {
                const auto p =
                    toPixel(static_cast<double>(i) / kCurveSamples, heard[static_cast<size_t>(i)]);
                i == 0 ? line.startNewSubPath(p) : line.lineTo(p);
            }
            g.setColour(ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY).withAlpha(0.55f));
            g.strokePath(line, juce::PathStrokeType(1.5f));
        }

        // A dot wherever the head crosses a step, which is where that step plays.
        if (steps_ > 0.0 && steps_ <= 128.0 && bend.on) {
            g.setColour(ActiveTheme::getColour(ActiveTheme::CURVE_POINT));
            for (int i = 0; i < kCurveSamples; ++i) {
                const double ya = heard[static_cast<size_t>(i)] * steps_;
                const double yb = heard[static_cast<size_t>(i + 1)] * steps_;
                if (std::abs(yb - ya) < 1.0e-9)
                    continue;
                const bool rising = yb > ya;
                const auto lo = static_cast<int>(std::ceil(std::min(ya, yb)));
                const auto hi = static_cast<int>(std::floor(std::max(ya, yb)));
                for (int k = lo; k <= hi; ++k) {
                    const double kd = k;
                    if (rising ? !(kd >= ya && kd < yb) : !(kd > yb && kd <= ya))
                        continue;
                    const double x = (i + (kd - ya) / (yb - ya)) / kCurveSamples;
                    const auto p = toPixel(x, kd / steps_);
                    g.fillEllipse(p.x - 2.0f, p.y - 2.0f, 4.0f, 4.0f);
                }
            }
        }

        if (playhead_ >= 0.0f) {
            const auto x = toPixel(playhead_, 0.0).x;
            g.setColour(ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY).withAlpha(0.8f));
            g.drawVerticalLine(juce::roundToInt(x), toPixel(0.0, 1.0).y, toPixel(0.0, 0.0).y);
        }
    }

    TimeBendEditor& owner_;
    sdk::CurveEditor core_;
    sdk::display::DisplayList displayList_;
    double steps_ = 0.0;
    double cycleBeats_ = 4.0;
    double beatsPerBar_ = 4.0;
    float playhead_ = -1.0f;
};

TimeBendEditor::TimeBendEditor()
    : powerButton_("TimeBendOn", BinaryData::power_svg, BinaryData::power_svgSize),
      canvas_(std::make_unique<Canvas>(*this)) {
    setName("TimeBendEditor");

    powerButton_.setOriginalColor(juce::Colour(0xFFE6E6E6));
    powerButton_.setTooltip("Play through the bend");
    powerButton_.onClick = [this] {
        auto after = bend_;
        after.on = !after.on;
        commit(after);
    };
    addAndMakeVisible(powerButton_);

    spanLabel_.setText("SPAN", juce::dontSendNotification);
    spanLabel_.setTooltip("How long one pass of the curve lasts");
    addAndMakeVisible(spanLabel_);

    gridButton_.setTooltip("The grid's division: its columns, rows and snap");
    gridButton_.onClick = [this] { showGridMenu(); };
    addAndMakeVisible(gridButton_);

    snapButton_.setTooltip("Snap points to the grid");
    snapButton_.onClick = [this] {
        snap_ = !snap_;
        snapButton_.setToggleState(snap_, juce::dontSendNotification);
        canvas_->setSnap(snap_);
    };
    addAndMakeVisible(snapButton_);

    shapeButton_.setTooltip("Start from a shape");
    shapeButton_.onClick = [this] { showShapeMenu(); };
    addAndMakeVisible(shapeButton_);

    for (size_t i = 0; i < spanButtons_.size(); ++i) {
        auto& button = spanButtons_[i];
        button.setButtonText(juce::String(kSpans[i]));
        button.setTooltip("One pass of the curve lasts " + juce::String(kSpans[i]) + " bar" +
                          (kSpans[i] == 1 ? "" : "s") + ", over its cycles");
        button.onClick = [this, i] {
            auto after = bend_;
            after.bars = kSpans[i];
            commit(after);
        };
        addAndMakeVisible(button);
    }

    styleLabel(amountLabel_, "AMOUNT");
    addAndMakeVisible(amountLabel_);
    amount_.setRange(0.0, 1.0, 0.01);
    amount_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + "%"; });
    amount_.setValueParser(
        [](const juce::String& t) { return t.trimCharactersAtEnd("%").getDoubleValue() / 100.0; });
    amount_.onValueChanged = [this](double value) {
        auto after = bend_;
        after.amount = static_cast<float>(value);
        commit(after);
    };
    addAndMakeVisible(amount_);

    styleLabel(cyclesLabel_, "CYCLES");
    addAndMakeVisible(cyclesLabel_);
    cycles_.setRange(1.0, sdk::sequencer::kMaxTimeBendCycles, 1.0);
    cycles_.setValueFormatter([](double v) { return juce::String(juce::roundToInt(v)) + "x"; });
    cycles_.setValueParser(
        [](const juce::String& t) { return t.trimCharactersAtEnd("x").getDoubleValue(); });
    cycles_.onValueChanged = [this](double value) {
        auto after = bend_;
        after.cycles = juce::roundToInt(value);
        commit(after);
    };
    addAndMakeVisible(cycles_);

    addAndMakeVisible(*canvas_);
    canvas_->setSnap(snap_);
    snapButton_.setToggleState(snap_, juce::dontSendNotification);
    applyGrid();
    lookAndFeelChanged();
    setBend({});
}

TimeBendEditor::~TimeBendEditor() {
    for (juce::TextButton* button : {&shapeButton_, &gridButton_, &snapButton_})
        button->setLookAndFeel(nullptr);
    for (auto& button : spanButtons_)
        button.setLookAndFeel(nullptr);
}

void TimeBendEditor::setBend(const TimeBend& bend) {
    bend_ = bend;
    canvas_->load(bend_);
    syncControls();
}

void TimeBendEditor::setCycle(double cycleBeats, double beatsPerBar) {
    if (cycleBeats == cycleBeats_ && beatsPerBar == beatsPerBar_)
        return;
    cycleBeats_ = cycleBeats;
    beatsPerBar_ = beatsPerBar;
    applyGrid();
}

void TimeBendEditor::setDefaultDivision(double beats) {
    if (divisionPicked_ || beats <= 0.0 || beats == division_)
        return;
    division_ = beats;
    applyGrid();
}

void TimeBendEditor::applyGrid() {
    canvas_->setGrid(cycleBeats_, beatsPerBar_, division_);
    juce::String label = juce::String(division_, 3);
    for (const auto& [name, beats] : kDivisions)
        if (std::abs(beats - division_) < 1.0e-9)
            label = name;
    gridButton_.setButtonText(label);
}

void TimeBendEditor::showGridMenu() {
    juce::PopupMenu menu;
    for (size_t i = 0; i < kDivisions.size(); ++i)
        menu.addItem(static_cast<int>(i) + 1, kDivisions[i].first, true,
                     std::abs(kDivisions[i].second - division_) < 1.0e-9);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&gridButton_),
                       [safe = juce::Component::SafePointer<TimeBendEditor>(this)](int result) {
                           if (safe == nullptr || result <= 0)
                               return;
                           safe->division_ = kDivisions[static_cast<size_t>(result - 1)].second;
                           safe->divisionPicked_ = true;
                           safe->applyGrid();
                       });
}

void TimeBendEditor::setPlayheadPhase(float phase) {
    canvas_->setPlayhead(phase);
}

void TimeBendEditor::setShowsAmountAndCycles(bool shows) {
    showsAmountAndCycles_ = shows;
    for (juce::Component* c :
         std::initializer_list<juce::Component*>{&amountLabel_, &amount_, &cyclesLabel_, &cycles_})
        c->setVisible(shows);
    resized();
}

void TimeBendEditor::commit(const TimeBend& after) {
    auto sanitised = after;
    sanitised.sanitise();
    // Shaping the bend is asking to hear it; only the BEND button turns it off.
    const bool shaped = !std::ranges::equal(sanitised.curve(), bend_.curve()) ||
                        sanitised.bars != bend_.bars || sanitised.cycles != bend_.cycles ||
                        sanitised.amount != bend_.amount;
    if (shaped)
        sanitised.on = true;
    if (sanitised == bend_)
        return;
    const auto before = bend_;
    setBend(sanitised);
    if (onCommit)
        onCommit(before, bend_);
}

void TimeBendEditor::showShapeMenu() {
    juce::PopupMenu menu;
    for (size_t i = 0; i < kShapes.size(); ++i) {
        const auto name = sdk::sequencer::timeBendShapeName(kShapes[i]);
        menu.addItem(static_cast<int>(i) + 1,
                     juce::String::fromUTF8(name.data(), static_cast<int>(name.size())));
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&shapeButton_),
                       [safe = juce::Component::SafePointer<TimeBendEditor>(this)](int result) {
                           if (safe == nullptr || result <= 0)
                               return;
                           auto after = safe->bend_;
                           const auto shaped = sdk::sequencer::makeTimeBend(
                               kShapes[static_cast<size_t>(result - 1)]);
                           after.setCurve(shaped.curve());
                           // Picking a shape is asking to hear it.
                           after.on = true;
                           safe->commit(after);
                       });
}

void TimeBendEditor::syncControls() {
    powerButton_.setActive(bend_.on);
    for (size_t i = 0; i < spanButtons_.size(); ++i)
        spanButtons_[i].setToggleState(bend_.bars == kSpans[i], juce::dontSendNotification);
    amount_.setValue(bend_.amount, juce::dontSendNotification);
    cycles_.setValue(bend_.cycles, juce::dontSendNotification);
    canvas_->setAlpha(bend_.on ? 1.0f : 0.5f);
}

void TimeBendEditor::paint(juce::Graphics& g) {
    g.fillAll(ActiveTheme::getColour(ActiveTheme::SURFACE));
}

bool TimeBendEditor::headerWraps() const {
    return showsAmountAndCycles_ && getWidth() < kOneRowWidth;
}

void TimeBendEditor::resized() {
    auto area = getLocalBounds();
    auto header = area.removeFromTop(kHeaderHeight);
    area.removeFromTop(kGap);
    auto values = header;
    if (headerWraps()) {
        values = area.removeFromTop(kHeaderHeight);
        area.removeFromTop(kGap);
    }

    powerButton_.setBounds(header.removeFromLeft(kHeaderHeight));
    header.removeFromLeft(kGap);
    shapeButton_.setBounds(header.removeFromLeft(52));
    header.removeFromLeft(kGap * 2);
    spanLabel_.setBounds(header.removeFromLeft(32));
    for (auto& button : spanButtons_) {
        button.setBounds(header.removeFromLeft(22));
        header.removeFromLeft(2);
    }
    header.removeFromLeft(kGap * 2);
    gridButton_.setBounds(header.removeFromLeft(44));
    header.removeFromLeft(2);
    snapButton_.setBounds(header.removeFromLeft(40));

    if (showsAmountAndCycles_) {
        if (!headerWraps())
            values = header;
        cycles_.setBounds(values.removeFromRight(34));
        cyclesLabel_.setBounds(values.removeFromRight(44));
        values.removeFromRight(kGap);
        amount_.setBounds(values.removeFromRight(40));
        amountLabel_.setBounds(values.removeFromRight(48));
    }

    canvas_->setBounds(area);
}

void TimeBendEditor::lookAndFeelChanged() {
    for (juce::TextButton* button : {&shapeButton_, &gridButton_, &snapButton_})
        styleChip(*button);
    styleLabel(spanLabel_, "SPAN");
    powerButton_.setNormalColor(ActiveTheme::getSecondaryTextColour());
    powerButton_.setHoverColor(ActiveTheme::getTextColour());
    powerButton_.setActiveColor(ActiveTheme::getAccentColour());
    for (auto& button : spanButtons_)
        styleChip(button);
    styleLabel(amountLabel_, "AMOUNT");
    styleLabel(cyclesLabel_, "CYCLES");
    repaint();
}

}  // namespace magda::daw::ui
