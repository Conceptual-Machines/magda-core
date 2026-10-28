#include "AbletonLinkChip.hpp"

#include <cmath>

#include "../themes/ActiveTheme.hpp"
#include "BinaryData.h"
#include "TransportLayout.hpp"
#include "core/TechnicalText.hpp"

namespace magda::daw::ui {

namespace {

using namespace transport::link_chip;

constexpr float kCornerRadius = 4.0f;
constexpr int kFrameHz = 30;
/// How much of a beat the LED stays lit for.
constexpr double kFlashShare = 0.2;

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
    waiting_ = waiting;
}

void AbletonLinkChip::setOn(bool on) {
    if (on_ == on)
        return;
    on_ = on;
    if (on)
        startTimerHz(kFrameHz);
    else
        stopTimer();
    led_ = Led::Steady;
    repaint();
}

AbletonLinkChip::Led AbletonLinkChip::ledNow() const {
    if (waiting_)
        return (juce::Time::getMillisecondCounter() / kBlinkIntervalMs) % 2 == 0 ? Led::Steady
                                                                                 : Led::Dim;
    const auto phase = barPhase ? barPhase() : std::nullopt;
    if (!phase)
        return Led::Steady;
    const auto beat = std::floor(*phase);
    if (*phase - beat >= kFlashShare)
        return Led::Dim;
    return beat == 0.0 ? Led::Downbeat : Led::Beat;
}

void AbletonLinkChip::timerCallback() {
    if (const auto led = ledNow(); led != led_) {
        led_ = led;
        repaint();
    }
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
    if (on_ && led_ != Led::Dim) {
        const bool downbeat = led_ == Led::Downbeat;
        g.setColour(accent.withAlpha(downbeat ? 0.45f : 0.25f));
        g.fillEllipse(ledBounds.expanded(led * (downbeat ? 0.7f : 0.4f)));
        g.setColour(downbeat ? accent.brighter(0.5f) : accent);
    } else {
        g.setColour(on_ ? accent.withAlpha(0.35f)
                        : ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY).withAlpha(0.5f));
    }
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
