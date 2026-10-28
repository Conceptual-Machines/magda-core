#include "AbletonLinkChip.hpp"

#include "../themes/ActiveTheme.hpp"
#include "BinaryData.h"
#include "TransportLayout.hpp"
#include "core/TechnicalText.hpp"

namespace magda::daw::ui {

namespace {

using namespace transport::link_chip;

constexpr float kCornerRadius = 4.0f;
constexpr int kBlinkIntervalMs = 150;

}  // namespace

AbletonLinkChip::AbletonLinkChip() : juce::Button("AbletonLink") {
    if (auto svg = juce::Drawable::createFromImageData(BinaryData::ableton_link_svg,
                                                       BinaryData::ableton_link_svgSize))
        lettering_ = svg->getOutlineAsPath();
    setTooltip(magda::technicalText(magda::TechnicalTextToken::AbletonLink));
    setWantsKeyboardFocus(false);
}

AbletonLinkChip::~AbletonLinkChip() {
    stopTimer();
}

void AbletonLinkChip::setWaiting(bool waiting) {
    if (waiting) {
        startTimer(kBlinkIntervalMs);
        return;
    }
    stopTimer();
    if (ledDim_) {
        ledDim_ = false;
        repaint();
    }
}

void AbletonLinkChip::timerCallback() {
    ledDim_ = !ledDim_;
    repaint();
}

void AbletonLinkChip::setOn(bool on) {
    if (on_ == on)
        return;
    on_ = on;
    repaint();
}

void AbletonLinkChip::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    const auto accent = ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY);
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const float h = bounds.getHeight();
    const float gap = h * kGapShare;

    g.setColour(on_ ? accent.withAlpha(0.14f)
                    : ActiveTheme::getColour(ActiveTheme::TRANSPORT_WELL));
    g.fillRoundedRectangle(bounds, kCornerRadius);
    auto border =
        on_ ? accent.withAlpha(0.5f) : ActiveTheme::getColour(ActiveTheme::TRANSPORT_WELL_BORDER);
    if (highlighted || down)
        border = border.brighter(0.4f);
    g.setColour(border);
    g.drawRoundedRectangle(bounds, kCornerRadius, 1.0f);

    const float led = h * kLedShare;
    const auto ledBounds = juce::Rectangle<float>(bounds.getX() + (h * kPadShare),
                                                  bounds.getCentreY() - (led / 2), led, led);
    const bool lit = on_ && !ledDim_;
    if (lit) {
        g.setColour(accent.withAlpha(0.25f));
        g.fillEllipse(ledBounds.expanded(led * 0.4f));
    }
    g.setColour(lit ? accent : ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY).withAlpha(0.5f));
    g.fillEllipse(ledBounds);

    const float letterH = h * kLetteringShare;
    const auto letterBox =
        juce::Rectangle<float>(ledBounds.getRight() + gap, bounds.getCentreY() - (letterH / 2),
                               letterH * kLetteringAspect, letterH);
    const auto ink = on_ ? accent
                         : ActiveTheme::getColour(highlighted ? ActiveTheme::TEXT_PRIMARY
                                                              : ActiveTheme::TEXT_SECONDARY);
    g.setColour(ink);
    g.fillPath(lettering_, lettering_.getTransformToScaleToFit(letterBox, true));
}

}  // namespace magda::daw::ui
