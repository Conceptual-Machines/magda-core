// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PanelFocusTracker.hpp"

#include "../themes/FocusHighlight.hpp"

namespace magda::daw::ui {

class PanelFocusTracker::Outline : public juce::Component {
  public:
    Outline() {
        setInterceptsMouseClicks(false, false);
        setAlwaysOnTop(true);
    }

    void paint(juce::Graphics& g) override {
        paintFocusHighlight(g, getLocalBounds(), kGlow);
    }

  private:
    static constexpr float kGlow = 1.0f;
};

PanelFocusTracker::PanelFocusTracker(juce::Component& host)
    : host_(host), outline_(std::make_unique<Outline>()) {
    host_.addChildComponent(*outline_);
    juce::Desktop::getInstance().addFocusChangeListener(this);
    juce::Desktop::getInstance().addGlobalMouseListener(this);
}

PanelFocusTracker::~PanelFocusTracker() {
    juce::Desktop::getInstance().removeGlobalMouseListener(this);
    juce::Desktop::getInstance().removeFocusChangeListener(this);
    for (auto& entry : regions_)
        if (entry.component != nullptr)
            entry.component->removeComponentListener(this);
    host_.removeChildComponent(outline_.get());
}

void PanelFocusTracker::addRegion(juce::Component& component, FocusRegion region) {
    jassert(component.getParentComponent() == &host_);
    // A focusable region catches a click whose target and ancestors don't want focus.
    component.setWantsKeyboardFocus(true);
    component.addComponentListener(this);
    regions_.push_back({&component, region});
}

juce::Component* PanelFocusTracker::regionComponentOf(const juce::Component* component) const {
    if (component == nullptr)
        return nullptr;
    for (const auto& entry : regions_) {
        auto* c = entry.component.getComponent();
        if (c != nullptr && (c == component || c->isParentOf(component)))
            return c;
    }
    return nullptr;
}

FocusRegion PanelFocusTracker::regionOf(const juce::Component* component) const {
    if (auto* c = regionComponentOf(component))
        for (const auto& entry : regions_)
            if (entry.component == c)
                return entry.region;
    return FocusRegion::None;
}

FocusRegion PanelFocusTracker::getActiveRegion() const {
    return regionOf(juce::Component::getCurrentlyFocusedComponent());
}

const juce::Component& PanelFocusTracker::getOutline() const {
    return *outline_;
}

void PanelFocusTracker::update() {
    auto* region = regionComponentOf(juce::Component::getCurrentlyFocusedComponent());
    if (region == nullptr || !region->isVisible()) {
        outline_->setVisible(false);
        return;
    }
    outline_->setBounds(region->getBounds());
    outline_->setVisible(true);
    outline_->toFront(false);
}

void PanelFocusTracker::globalFocusChanged(juce::Component* focusedComponent) {
    if (auto* region = regionComponentOf(focusedComponent)) {
        lastFocusedRegion_ = region;
    } else if (focusedComponent == nullptr && lastFocusedRegion_ != nullptr) {
        // Panels drop focus with unfocusAllComponents() before a rebuild; keep the
        // region while the window itself still has focus.
        auto* peer = host_.getPeer();
        if (peer != nullptr && peer->isFocused() && lastFocusedRegion_->isShowing())
            lastFocusedRegion_->grabKeyboardFocus();
    }
    update();
}

void PanelFocusTracker::mouseDown(const juce::MouseEvent& event) {
    auto* region = regionComponentOf(event.eventComponent);
    if (region != nullptr && region->isShowing() && !region->hasKeyboardFocus(true))
        region->grabKeyboardFocus();
}

void PanelFocusTracker::componentMovedOrResized(juce::Component&, bool, bool) {
    update();
}

void PanelFocusTracker::componentVisibilityChanged(juce::Component&) {
    update();
}

}  // namespace magda::daw::ui
