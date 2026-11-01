// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Mascot.hpp"

#include <cmath>

#include "BinaryData.h"
#include "ui/themes/ActiveTheme.hpp"

namespace magda {

namespace {
/// The soft edge and the ring's glow, as fractions of her height on screen.
constexpr float kEdgeWidth = 0.05f;
constexpr float kRingGlow = 0.04f;

/// The headphone ring flashes on the beat at 120 BPM and decays like a meter.
constexpr double kBeatSeconds = 0.5;
constexpr double kAttackSeconds = 0.05;
constexpr double kDecaySeconds = 0.12;

/** @brief How lit the ring is at @p seconds, from 0 to 1. */
float ringLevel(double seconds) {
    const auto t = std::fmod(seconds, kBeatSeconds);
    if (t < kAttackSeconds)
        return static_cast<float>(t / kAttackSeconds);
    return static_cast<float>(std::exp((kAttackSeconds - t) / kDecaySeconds));
}

/** @brief The element with @p id anywhere under @p tree, or nullptr. */
juce::XmlElement* findById(juce::XmlElement& tree, juce::StringRef id) {
    if (tree.getStringAttribute("id") == id)
        return &tree;
    for (auto* child : tree.getChildIterator())
        if (auto* found = findById(*child, id))
            return found;
    return nullptr;
}

/** @brief Every path in @p group as one shape, with the group's fill rule. */
juce::Path shapeOf(const juce::XmlElement& group) {
    juce::Path shape;
    for (auto* path : group.getChildWithTagNameIterator("path"))
        shape.addPath(juce::Drawable::parseSVGPath(path->getStringAttribute("d")));
    shape.setUsingNonZeroWinding(group.getStringAttribute("fill-rule") != "evenodd");
    return shape;
}
}  // namespace

Mascot::Layer::Layer(std::function<void(juce::Graphics&)> paintLayer, bool still)
    : paintLayer_(std::move(paintLayer)) {
    setBufferedToImage(still);
    setInterceptsMouseClicks(false, false);
}

Mascot::Mascot() {
    setInterceptsMouseClicks(false, false);

    auto svg = juce::XmlDocument::parse(juce::String::fromUTF8(
        BinaryData::magdalisasplash_svg, BinaryData::magdalisasplash_svgSize));
    auto* silhouette = svg != nullptr ? findById(*svg, "silhouette") : nullptr;
    auto* ring = svg != nullptr ? findById(*svg, "headphone-accent") : nullptr;
    if (silhouette == nullptr || ring == nullptr)
        return;

    silhouette_ = shapeOf(*silhouette);
    ring_ = shapeOf(*ring);
    ring->setAttribute("fill", "none");
    art_ = juce::Drawable::createFromSVGString(svg->toString());

    for (auto* layer : {&artLayer_, &ringLayer_, &edgeLayer_})
        addAndMakeVisible(layer);
}

void Mascot::paint(juce::Graphics& g) {
    // A soft light behind her, so her dark hair reads against the panel.
    const auto light = ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY);
    const auto centre = getLocalBounds().toFloat().getCentre();
    g.setGradientFill(juce::ColourGradient(light.withAlpha(0.22f), centre, light.withAlpha(0.0f),
                                           centre.withY(0.0f), true));
    g.fillAll();
}

void Mascot::resized() {
    if (art_ == nullptr)
        return;

    placement_ = juce::RectanglePlacement(juce::RectanglePlacement::centred)
                     .getTransformToFit(silhouette_.getBounds(), getLocalBounds().toFloat());
    placedSilhouette_ = placed(silhouette_);
    placedRing_ = placed(ring_);

    const auto height = placedSilhouette_.getBounds().getHeight();
    edge_.clear();
    juce::PathStrokeType(kEdgeWidth * height).createStrokedPath(edge_, placedSilhouette_);
    edgeBlur_ = std::max(1, juce::roundToInt(kEdgeWidth * height));
    ringGlow_ = std::max(1, juce::roundToInt(kRingGlow * height));
    ringArea_ = placedRing_.getBounds()
                    .expanded(static_cast<float>(ringGlow_ + 1))
                    .getSmallestIntegerContainer();

    for (auto* layer : {&artLayer_, &ringLayer_, &edgeLayer_})
        layer->setBounds(getLocalBounds());
}

juce::Path Mascot::placed(juce::Path shape) const {
    shape.applyTransform(placement_);
    return shape;
}

void Mascot::paintArt(juce::Graphics& g) const {
    g.reduceClipRegion(placedSilhouette_);
    art_->draw(g, 1.0f, placement_);
}

void Mascot::paintRing(juce::Graphics& g) const {
    const auto rest = ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY);
    const auto peak = ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY_SOFT);
    if (level_ > 0.01f)
        juce::DropShadow(peak.withMultipliedAlpha(level_), ringGlow_, {})
            .drawForPath(g, placedRing_);
    g.setColour(rest.interpolatedWith(peak, level_));
    g.fillPath(placedRing_);
}

void Mascot::paintEdge(juce::Graphics& g) const {
    g.reduceClipRegion(placedSilhouette_);
    juce::DropShadow(ActiveTheme::getColour(ActiveTheme::PANEL_BACKGROUND), edgeBlur_, {})
        .drawForPath(g, edge_);
}

void Mascot::pulse(double seconds) {
    level_ = ringLevel(seconds);
    ringLayer_.repaint(ringArea_);
}

}  // namespace magda
