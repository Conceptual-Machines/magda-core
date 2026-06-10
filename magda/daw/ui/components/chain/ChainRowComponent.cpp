#include "ChainRowComponent.hpp"

#include <BinaryData.h>

#include <algorithm>

#include "../../utils/SelectionPolicy.hpp"
#include "RackComponent.hpp"
#include "core/SelectionManager.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

ChainRowComponent::ChainRowComponent(RackComponent& owner, magda::TrackId trackId,
                                     magda::RackId rackId, const magda::ChainInfo& chain)
    : owner_(owner), trackId_(trackId), rackId_(rackId), chainId_(chain.id) {
    // Set up node path for centralized selection
    nodePath_ = magda::ChainNodePath::chain(trackId, rackId, chain.id);

    // Register as SelectionManager listener (highlight) and TrackManager
    // listener (live refresh when a sibling's multi-edit changes this chain).
    magda::SelectionManager::getInstance().addListener(this);
    magda::TrackManager::getInstance().addListener(this);
    // Name label - double-click to rename; a plain single click selects the
    // chain (see ChainNameLabel). editOnSingleClick=false so a single click
    // never opens the editor.
    nameLabel_.setText(chain.name, juce::dontSendNotification);
    nameLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    nameLabel_.setColour(juce::Label::backgroundWhenEditingColourId,
                         DarkTheme::getColour(DarkTheme::SURFACE));
    nameLabel_.setColour(juce::Label::textWhenEditingColourId, DarkTheme::getTextColour());
    nameLabel_.setJustificationType(juce::Justification::centredLeft);
    nameLabel_.setEditable(false, true, false);  // editOnDoubleClick only
    nameLabel_.onSelect = [this](const juce::MouseEvent& e) { applySelectionForClick(e.mods); };
    nameLabel_.onTextChange = [this]() {
        auto& tm = magda::TrackManager::getInstance();
        tm.setChainName(trackId_, rackId_, chainId_, nameLabel_.getText());
        // Re-sync from the model in case the edit was rejected (empty/unchanged),
        // so the label never shows a name the model didn't accept.
        if (const auto* chain = tm.getChain(trackId_, rackId_, chainId_))
            nameLabel_.setText(chain->name, juce::dontSendNotification);
    };
    addAndMakeVisible(nameLabel_);

    // Gain label (dB format, draggable)
    gainLabel_.setFormat(magda::DraggableValueLabel::Format::Decibels);
    gainLabel_.setRange(-60.0, 6.0, 0.0);
    gainLabel_.setValue(chain.volume, juce::dontSendNotification);
    gainLabel_.setFontSize(9.0f);
    gainLabel_.setFillColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.2f));
    // Capture each target chain's base gain at drag start so a multi-chain drag
    // shifts every selected chain by the same dB delta from its own value.
    gainLabel_.onDragStart = [this]() {
        dragStartGainDb_ = gainLabel_.getValue();
        dragBaseGains_.clear();
        auto& tm = magda::TrackManager::getInstance();
        for (const auto& path : editTargets())
            if (const auto* c = tm.getChainByPath(path))
                dragBaseGains_.emplace_back(path, c->volume);
    };
    gainLabel_.onValueChange = [this]() {
        auto& tm = magda::TrackManager::getInstance();
        if (dragBaseGains_.empty()) {
            // Non-drag change (or single target): set this chain directly.
            tm.setChainVolume(nodePath_, static_cast<float>(gainLabel_.getValue()));
            return;
        }
        const double delta = gainLabel_.getValue() - dragStartGainDb_;
        for (const auto& [path, base] : dragBaseGains_)
            tm.setChainVolume(path, static_cast<float>(base + delta));
    };
    gainLabel_.onDragEnd = [this](double) { dragBaseGains_.clear(); };
    addAndMakeVisible(gainLabel_);

    // Pan label (L/C/R format, draggable)
    panLabel_.setFormat(magda::DraggableValueLabel::Format::Pan);
    panLabel_.setRange(-1.0, 1.0, 0.0);
    panLabel_.setValue(chain.pan, juce::dontSendNotification);
    panLabel_.setFontSize(9.0f);
    panLabel_.setFillColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.2f));
    panLabel_.onDragStart = [this]() {
        dragStartPan_ = panLabel_.getValue();
        dragBasePans_.clear();
        auto& tm = magda::TrackManager::getInstance();
        for (const auto& path : editTargets())
            if (const auto* c = tm.getChainByPath(path))
                dragBasePans_.emplace_back(path, c->pan);
    };
    panLabel_.onValueChange = [this]() {
        auto& tm = magda::TrackManager::getInstance();
        if (dragBasePans_.empty()) {
            tm.setChainPan(nodePath_, static_cast<float>(panLabel_.getValue()));
            return;
        }
        const double delta = panLabel_.getValue() - dragStartPan_;
        for (const auto& [path, base] : dragBasePans_)
            tm.setChainPan(path, static_cast<float>(base + delta));
    };
    panLabel_.onDragEnd = [this](double) { dragBasePans_.clear(); };
    addAndMakeVisible(panLabel_);

    // Mute button
    muteButton_.setButtonText("M");
    muteButton_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    muteButton_.setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::STATUS_WARNING));
    muteButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    muteButton_.setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND));
    muteButton_.setClickingTogglesState(true);
    muteButton_.setToggleState(chain.muted, juce::dontSendNotification);
    muteButton_.onClick = [this]() { onMuteClicked(); };
    muteButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addAndMakeVisible(muteButton_);

    // Solo button
    soloButton_.setButtonText("S");
    soloButton_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    soloButton_.setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    soloButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    soloButton_.setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND));
    soloButton_.setClickingTogglesState(true);
    soloButton_.setToggleState(chain.solo, juce::dontSendNotification);
    soloButton_.onClick = [this]() { onSoloClicked(); };
    soloButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addAndMakeVisible(soloButton_);

    // On/bypass button (power icon)
    onButton_ = std::make_unique<magda::SvgButton>("Power", BinaryData::power_on_svg,
                                                   BinaryData::power_on_svgSize);
    onButton_->setClickingTogglesState(true);
    onButton_->setToggleState(!chain.bypassed, juce::dontSendNotification);  // On = not bypassed
    onButton_->setNormalColor(DarkTheme::getColour(DarkTheme::STATUS_ERROR));
    onButton_->setActiveColor(juce::Colours::white);
    onButton_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_GREEN).darker(0.3f));
    onButton_->setActive(!chain.bypassed);
    onButton_->onClick = [this]() {
        onButton_->setActive(onButton_->getToggleState());
        onBypassClicked();
    };
    addAndMakeVisible(*onButton_);

    // Delete button (reddish-purple background)
    deleteButton_.setButtonText(juce::String::fromUTF8("\xc3\x97"));  // × symbol
    deleteButton_.setColour(
        juce::TextButton::buttonColourId,
        DarkTheme::getColour(DarkTheme::ACCENT_PURPLE)
            .interpolatedWith(DarkTheme::getColour(DarkTheme::STATUS_ERROR), 0.5f)
            .darker(0.2f));
    deleteButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    deleteButton_.onClick = [this]() { onDeleteClicked(); };
    deleteButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addAndMakeVisible(deleteButton_);

    // Dim controls if chain starts bypassed
    if (chain.bypassed) {
        float alpha = 0.35f;
        nameLabel_.setAlpha(alpha);
        muteButton_.setAlpha(alpha);
        soloButton_.setAlpha(alpha);
        gainLabel_.setAlpha(alpha);
        panLabel_.setAlpha(alpha);
    }
}

ChainRowComponent::~ChainRowComponent() {
    magda::SelectionManager::getInstance().removeListener(this);
    magda::TrackManager::getInstance().removeListener(this);
}

void ChainRowComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // Background - highlight if selected
    if (selected_) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.2f));
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
    }
    g.fillRoundedRectangle(bounds.toFloat(), 2.0f);

    // Border - accent color if selected
    if (selected_) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    }
    g.drawRoundedRectangle(bounds.toFloat(), 2.0f, 1.0f);
}

void ChainRowComponent::mouseDown(const juce::MouseEvent& /*event*/) {
    // Just visual feedback - actual selection happens on mouseUp to avoid
    // issues with multiple mouseDown events during layout changes
}

void ChainRowComponent::mouseUp(const juce::MouseEvent& event) {
    // Only handle if mouse is still over this component (user didn't drag away)
    if (!contains(event.getPosition())) {
        return;
    }
    applySelectionForClick(event.mods);
}

void ChainRowComponent::mouseDoubleClick(const juce::MouseEvent& /*event*/) {
    // Double-click toggles expand/collapse of this chain
    if (onDoubleClick) {
        onDoubleClick(chainId_);
    }
}

void ChainRowComponent::selectionTypeChanged(magda::SelectionType newType) {
    // Drop the highlight when selection moves to a non-chain context (e.g. a
    // clip): chainNodeSelectionChanged only fires for chain-node changes.
    if (newType != magda::SelectionType::ChainNode &&
        newType != magda::SelectionType::MultiChainNode) {
        setSelected(false);
    }
}

void ChainRowComponent::applySelectionForClick(const juce::ModifierKeys& mods) {
    auto& sel = magda::SelectionManager::getInstance();
    if (magda::isRangeSelectClick(mods))
        rangeSelectFromAnchor();
    else if (magda::isToggleSelectClick(mods))
        sel.toggleChainNodeSelection(nodePath_);
    else
        sel.selectChainNode(nodePath_);
}

void ChainRowComponent::rangeSelectFromAnchor() {
    auto& sel = magda::SelectionManager::getInstance();
    const auto& anchor = sel.getAnchorChainNode();
    const auto rackPath = nodePath_.parent();
    const auto* rack = magda::TrackManager::getInstance().getRackByPath(rackPath);
    if (!anchor.isValid() || rack == nullptr) {
        sel.selectChainNode(nodePath_);
        return;
    }

    // Sibling chains in model order, so Shift+click selects the contiguous run
    // between the anchor chain and this one.
    std::vector<magda::ChainNodePath> paths;
    int anchorIdx = -1, clickedIdx = -1;
    for (const auto& chain : rack->chains) {
        auto p = rackPath.withChain(chain.id);
        if (p == anchor)
            anchorIdx = static_cast<int>(paths.size());
        if (p == nodePath_)
            clickedIdx = static_cast<int>(paths.size());
        paths.push_back(p);
    }
    if (anchorIdx < 0 || clickedIdx < 0) {
        sel.selectChainNode(nodePath_);
        return;
    }
    const int lo = std::min(anchorIdx, clickedIdx);
    const int hi = std::max(anchorIdx, clickedIdx);
    sel.selectChainNodes({paths.begin() + lo, paths.begin() + hi + 1});
}

std::vector<magda::ChainNodePath> ChainRowComponent::editTargets() const {
    auto& sel = magda::SelectionManager::getInstance();
    if (sel.isChainNodeSelected(nodePath_) && sel.getSelectedChainNodes().size() > 1)
        return sel.getSelectedChainNodes();
    return {nodePath_};
}

void ChainRowComponent::chainNodeSelectionChanged(const magda::ChainNodePath& /*path*/) {
    // Highlight whenever this chain is part of the (possibly multi-) selection.
    // This fires for every chain-node selection change, so all rows recompute.
    setSelected(magda::SelectionManager::getInstance().isChainNodeSelected(nodePath_));
}

void ChainRowComponent::trackPropertyChanged(int trackId) {
    if (trackId == trackId_)
        refreshFromModel();
}

void ChainRowComponent::trackDevicesChanged(int trackId) {
    if (trackId == trackId_)
        refreshFromModel();
}

void ChainRowComponent::refreshFromModel() {
    if (const auto* chain = magda::TrackManager::getInstance().getChainByPath(nodePath_))
        updateFromChain(*chain);
}

void ChainRowComponent::setSelected(bool selected) {
    if (selected_ != selected) {
        selected_ = selected;
        repaint();
    }
}

void ChainRowComponent::setNodePath(const magda::ChainNodePath& path) {
    nodePath_ = path;

    // Reflect current selection state (handles selection made before the row
    // existed, and multi-selection where this chain is one of several).
    setSelected(magda::SelectionManager::getInstance().isChainNodeSelected(nodePath_));
}

void ChainRowComponent::resized() {
    auto bounds = getLocalBounds().reduced(3, 2);

    // Layout: [Name] [Gain] [Pan] ... [M] [S] [On] [X]
    // Spread across full width with right-side buttons anchored to the right

    // Right side buttons (from right to left)
    deleteButton_.setBounds(bounds.removeFromRight(16));
    bounds.removeFromRight(2);

    onButton_->setBounds(bounds.removeFromRight(16));
    bounds.removeFromRight(2);

    soloButton_.setBounds(bounds.removeFromRight(16));
    bounds.removeFromRight(2);

    muteButton_.setBounds(bounds.removeFromRight(16));
    bounds.removeFromRight(8);

    // Left side elements
    nameLabel_.setBounds(bounds.removeFromLeft(50));
    bounds.removeFromLeft(4);

    // Remaining space for gain and pan (70/30 split)
    int remainingWidth = bounds.getWidth();
    int gainWidth = (remainingWidth - 8) * 70 / 100;
    int panWidth = remainingWidth - 8 - gainWidth;

    gainLabel_.setBounds(bounds.removeFromLeft(gainWidth));
    bounds.removeFromLeft(8);

    panLabel_.setBounds(bounds.removeFromLeft(panWidth));
}

int ChainRowComponent::getPreferredHeight() const {
    return ROW_HEIGHT;
}

void ChainRowComponent::updateFromChain(const magda::ChainInfo& chain) {
    // Don't clobber an in-progress rename if a property change arrives mid-edit.
    if (!nameLabel_.isBeingEdited())
        nameLabel_.setText(chain.name, juce::dontSendNotification);
    muteButton_.setToggleState(chain.muted, juce::dontSendNotification);
    soloButton_.setToggleState(chain.solo, juce::dontSendNotification);
    gainLabel_.setValue(chain.volume, juce::dontSendNotification);
    panLabel_.setValue(chain.pan, juce::dontSendNotification);
    onButton_->setToggleState(!chain.bypassed, juce::dontSendNotification);
    onButton_->setActive(!chain.bypassed);

    // Dim controls when chain is bypassed
    float alpha = chain.bypassed ? 0.35f : 1.0f;
    nameLabel_.setAlpha(alpha);
    muteButton_.setAlpha(alpha);
    soloButton_.setAlpha(alpha);
    gainLabel_.setAlpha(alpha);
    panLabel_.setAlpha(alpha);
}

void ChainRowComponent::onMuteClicked() {
    auto& tm = magda::TrackManager::getInstance();
    const bool muted = muteButton_.getToggleState();
    for (const auto& path : editTargets())
        tm.setChainMuted(path, muted);
}

void ChainRowComponent::onSoloClicked() {
    auto& tm = magda::TrackManager::getInstance();
    const bool solo = soloButton_.getToggleState();
    for (const auto& path : editTargets())
        tm.setChainSolo(path, solo);
}

void ChainRowComponent::onBypassClicked() {
    auto& tm = magda::TrackManager::getInstance();
    const bool bypassed = !onButton_->getToggleState();
    for (const auto& path : editTargets())
        tm.setChainBypassed(path, bypassed);
}

void ChainRowComponent::onDeleteClicked() {
    // Use path-based removal to support nested chains
    if (nodePath_.isValid()) {
        magda::TrackManager::getInstance().removeChainByPath(nodePath_);
    } else {
        // Fallback to flat ID removal for top-level chains
        magda::TrackManager::getInstance().removeChainFromRack(trackId_, rackId_, chainId_);
    }
}

}  // namespace magda::daw::ui
