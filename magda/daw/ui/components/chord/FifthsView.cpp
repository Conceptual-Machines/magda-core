// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "FifthsView.hpp"

#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

using magda::music::ChordQuality;
using magda::music::ChordRoot;

/// Below this the wheel's labels stop reading, so the rows take over.
constexpr int kMinWheelSide = 180;
constexpr int kMaxWheelSide = 360;
constexpr int kMaxStripHeight = 96;

bool sameSource(const std::optional<FifthsSource>& a, const std::optional<FifthsSource>& b) {
    if (a.has_value() != b.has_value())
        return false;
    if (!a.has_value())
        return true;
    const auto sameKey =
        a->key.has_value() == b->key.has_value() &&
        (!a->key.has_value() || (a->key->root == b->key->root && a->key->minor == b->key->minor));
    return a->root == b->root && a->quality == b->quality && a->name == b->name &&
           a->octave == b->octave && sameKey;
}

}  // namespace

FifthsView::FifthsView() {
    addChildComponent(wheel_);
    addChildComponent(strip_);

    const auto start = [this](ChordRoot root, ChordQuality quality) {
        dragHandedOut_ = false;
        if (onAuditionStart)
            onAuditionStart(root, quality);
    };
    const auto stop = [this] {
        if (onAuditionStop)
            onAuditionStop();
    };
    const auto dragOut = [this](ChordRoot root, ChordQuality quality) {
        if (dragHandedOut_)
            return;
        dragHandedOut_ = true;
        if (onDragOut)
            onDragOut(root, quality);
    };
    wheel_.onAuditionStart = start;
    strip_.onAuditionStart = start;
    wheel_.onAuditionStop = stop;
    strip_.onAuditionStop = stop;
    wheel_.onDragMove = [dragOut](ChordRoot root, ChordQuality quality, juce::Point<int>) {
        dragOut(root, quality);
    };
    strip_.onDragMove = dragOut;
}

void FifthsView::setSource(std::optional<FifthsSource> source) {
    // Re-centring on an unchanged chord would throw away where the user has scrolled to.
    if (sameSource(source, source_))
        return;
    source_ = std::move(source);
    if (source_.has_value()) {
        wheel_.setSource(source_->root, source_->quality, source_->name, source_->key);
        strip_.setSource(source_->root, source_->quality, source_->key);
    }
    resized();
    repaint();
}

void FifthsView::paint(juce::Graphics& g) {
    if (source_.has_value())
        return;
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2));
    g.setFont(FontManager::getInstance().getMonoFont(11.0f));
    g.drawText("Select a chord on the chord track, or play one",
               getLocalBounds().withTrimmedTop(16).withHeight(20), juce::Justification::centredLeft,
               true);
}

void FifthsView::resized() {
    const auto bounds = getLocalBounds();
    const int side = std::min({bounds.getWidth(), bounds.getHeight(), kMaxWheelSide});
    const bool roomForWheel = side >= kMinWheelSide;
    wheel_.setVisible(source_.has_value() && roomForWheel);
    strip_.setVisible(source_.has_value() && !roomForWheel);
    wheel_.setBounds(juce::Rectangle<int>(side, side)
                         .withCentre({bounds.getCentreX(), bounds.getY() + side / 2}));
    strip_.setBounds(bounds.withHeight(std::min(bounds.getHeight(), kMaxStripHeight)));
}

}  // namespace magda::daw::ui
