#include "custom_ui/AdsrGraph.hpp"

#include "core/ParameterUtils.hpp"
#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

namespace {
constexpr float kPad = 6.0f;
constexpr float kHandleR = 4.0f;
constexpr float kSustainFrac = 0.18f;  // sustain plateau as fraction of width
}  // namespace

AdsrGraph::AdsrGraph() {
    setInterceptsMouseClicks(true, false);
}

void AdsrGraph::setStage(Stage stage, int paramIndex, const magda::ParameterInfo& info,
                         float displayValue) {
    auto& s = stages_[static_cast<size_t>(stage)];
    s.paramIndex = paramIndex;
    s.info = info;
    s.value = displayValue;
    s.set = true;
    repaint();
}

void AdsrGraph::setStageValue(Stage stage, float displayValue) {
    auto& s = stages_[static_cast<size_t>(stage)];
    if (!s.set || juce::approximatelyEqual(s.value, displayValue))
        return;
    s.value = displayValue;
    repaint();
}

float AdsrGraph::frac(Stage stage) const {
    const auto& s = stages_[static_cast<size_t>(stage)];
    if (!s.set)
        return 0.0f;
    return juce::jlimit(0.0f, 1.0f, magda::ParameterUtils::realToNormalized(s.value, s.info));
}

void AdsrGraph::commitFrac(Stage stage, float newFrac) {
    auto& s = stages_[static_cast<size_t>(stage)];
    if (!s.set)
        return;
    newFrac = juce::jlimit(0.0f, 1.0f, newFrac);
    const float display = magda::ParameterUtils::normalizedToReal(newFrac, s.info);
    if (juce::approximatelyEqual(s.value, display))
        return;
    s.value = display;
    repaint();
    if (onStageChanged)
        onStageChanged(s.paramIndex, display);
}

AdsrGraph::Geometry AdsrGraph::computeGeometry() const {
    auto b = getLocalBounds().toFloat().reduced(kPad);
    Geometry g;
    g.x0 = b.getX();
    g.yTop = b.getY();
    g.yBot = b.getBottom();

    g.sustainW = b.getWidth() * kSustainFrac;
    g.segMax = (b.getWidth() - g.sustainW) / 3.0f;

    const float xA = g.x0 + frac(Attack) * g.segMax;
    const float xD = xA + frac(Decay) * g.segMax;
    const float xS = xD + g.sustainW;
    const float xR = xS + frac(Release) * g.segMax;
    const float ySus = g.yBot - frac(Sustain) * (g.yBot - g.yTop);

    g.peak = {xA, g.yTop};
    g.sustainCorner = {xD, ySus};
    g.releaseEnd = {xR, g.yBot};
    return g;
}

void AdsrGraph::paint(juce::Graphics& gfx) {
    const auto bg = DarkTheme::getColour(DarkTheme::BACKGROUND).darker(0.25f);
    const auto line = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);
    const auto fillC = line.withAlpha(0.18f);
    const auto handleC = line.brighter(0.2f);

    auto area = getLocalBounds().toFloat().reduced(1.0f);
    gfx.setColour(bg);
    gfx.fillRoundedRectangle(area, 3.0f);

    const auto g = computeGeometry();
    const juce::Point<float> start{g.x0, g.yBot};
    const juce::Point<float> sustainEnd{g.sustainCorner.x + g.sustainW, g.sustainCorner.y};

    juce::Path curve;
    curve.startNewSubPath(start);
    curve.lineTo(g.peak);
    curve.lineTo(g.sustainCorner);
    curve.lineTo(sustainEnd);
    curve.lineTo(g.releaseEnd);

    // Filled region under the curve.
    juce::Path fill = curve;
    fill.lineTo({g.releaseEnd.x, g.yBot});
    fill.closeSubPath();
    gfx.setColour(fillC);
    gfx.fillPath(fill);

    gfx.setColour(line);
    gfx.strokePath(curve, juce::PathStrokeType(1.5f));

    // Drag handles.
    gfx.setColour(handleC);
    for (const auto& p : {g.peak, g.sustainCorner, g.releaseEnd})
        gfx.fillEllipse(p.x - kHandleR, p.y - kHandleR, kHandleR * 2.0f, kHandleR * 2.0f);
}

void AdsrGraph::mouseDown(const juce::MouseEvent& e) {
    const auto g = computeGeometry();
    const juce::Point<float> pos = e.position;
    const std::array<juce::Point<float>, 3> handles{g.peak, g.sustainCorner, g.releaseEnd};
    const std::array<int, 3> handleStage{Attack, Decay,
                                         Release};  // Decay corner also drags Sustain

    float best = 1.0e9f;
    dragStage_ = -1;
    for (int i = 0; i < 3; ++i) {
        const float d = pos.getDistanceFrom(handles[static_cast<size_t>(i)]);
        if (d < best && d <= 14.0f) {
            best = d;
            dragStage_ = handleStage[static_cast<size_t>(i)];
        }
    }
}

void AdsrGraph::mouseDrag(const juce::MouseEvent& e) {
    if (dragStage_ < 0)
        return;
    const auto g = computeGeometry();
    if (g.segMax <= 0.0f)
        return;
    const juce::Point<float> pos = e.position;

    switch (dragStage_) {
        case Attack:
            commitFrac(Attack, (pos.x - g.x0) / g.segMax);
            break;
        case Decay: {
            // The sustain corner drags both decay (x, relative to the peak) and
            // sustain (y).
            commitFrac(Decay, (pos.x - g.peak.x) / g.segMax);
            commitFrac(Sustain, (g.yBot - pos.y) / (g.yBot - g.yTop));
            break;
        }
        case Release: {
            const float xS = g.sustainCorner.x + g.sustainW;
            commitFrac(Release, (pos.x - xS) / g.segMax);
            break;
        }
        default:
            break;
    }
}

void AdsrGraph::mouseUp(const juce::MouseEvent&) {
    dragStage_ = -1;
}

}  // namespace magda::daw::ui
