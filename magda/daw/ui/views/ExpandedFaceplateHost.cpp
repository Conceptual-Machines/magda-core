#include "ExpandedFaceplateHost.hpp"

#include "BinaryData.h"
#include "ui/state/ExpandedFaceplate.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda {

namespace {
constexpr int kTitleHeight = 32;
}

ExpandedFaceplateHost::ExpandedFaceplateHost()
    : collapseButton_("Collapse", BinaryData::exit_fs_svg, BinaryData::exit_fs_svgSize) {
    collapseButton_.setTooltip("Put the faceplate back in its device slot (Esc)");
    collapseButton_.setNormalColor(ActiveTheme::DEVICE_ICON);
    collapseButton_.setHoverColor(ActiveTheme::DEVICE_ICON_HOVER);
    collapseButton_.onClick = [] { ExpandedFaceplate::getInstance().collapse(); };
    addAndMakeVisible(collapseButton_);
    setWantsKeyboardFocus(true);
    ExpandedFaceplate::getInstance().addChangeListener(this);
}

ExpandedFaceplateHost::~ExpandedFaceplateHost() {
    ExpandedFaceplate::getInstance().removeChangeListener(this);
}

bool ExpandedFaceplateHost::isShowing() const {
    return ExpandedFaceplate::getInstance().isActive();
}

void ExpandedFaceplateHost::changeListenerCallback(juce::ChangeBroadcaster*) {
    adoptFaceplate();
    if (onShowingChanged)
        onShowingChanged();
}

void ExpandedFaceplateHost::adoptFaceplate() {
    if (auto* faceplate = ExpandedFaceplate::getInstance().faceplate();
        faceplate != nullptr && faceplate->getParentComponent() != this) {
        addAndMakeVisible(*faceplate);
        resized();
    }
    repaint();
}

void ExpandedFaceplateHost::paint(juce::Graphics& g) {
    g.fillAll(ActiveTheme::getColour(ActiveTheme::DEVICE_BG));
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE));
    g.drawHorizontalLine(kTitleHeight - 1, 0.0f, static_cast<float>(getWidth()));
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_TITLE));
    g.setFont(FontManager::getInstance().getUIFontMedium(14.0f));
    g.drawText(ExpandedFaceplate::getInstance().title(), titleArea_,
               juce::Justification::centredLeft, true);
}

void ExpandedFaceplateHost::resized() {
    auto area = getLocalBounds();
    auto title = area.removeFromTop(kTitleHeight).reduced(14, 0);
    collapseButton_.setBounds(title.removeFromRight(24).withSizeKeepingCentre(22, 22));
    titleArea_ = title;
    if (auto* faceplate = ExpandedFaceplate::getInstance().faceplate();
        faceplate != nullptr && faceplate->getParentComponent() == this)
        faceplate->setBounds(area.reduced(6));
}

bool ExpandedFaceplateHost::keyPressed(const juce::KeyPress& key) {
    if (key != juce::KeyPress::escapeKey)
        return false;
    ExpandedFaceplate::getInstance().collapse();
    return true;
}

}  // namespace magda
