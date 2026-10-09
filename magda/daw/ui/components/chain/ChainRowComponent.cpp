#include "ChainRowComponent.hpp"

#include <BinaryData.h>

#include <algorithm>
#include <cmath>
#include <iterator>

#include "../../utils/SelectionPolicy.hpp"
#include "RackComponent.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackCommands.hpp"
#include "layout/DeviceShellPainter.hpp"
#include "layout/NodeHeaderStyles.hpp"
#include "ui/components/common/MasterSpeakerButton.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {
constexpr float kDisabledAlpha = 0.55f;
constexpr double kMinGainDb = -60.0;
}  // namespace

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
    gainLabel_.setRange(kMinGainDb, 6.0, 0.0);
    gainLabel_.setValue(chain.volume, juce::dontSendNotification);
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

    muteButton_.setTooltip("Mute chain");
    syncMuteGlyph(muteButton_, chain.muted);
    muteButton_.onClick = [this]() { onMuteClicked(); };
    addAndMakeVisible(muteButton_);

    soloButton_.setTooltip("Solo chain");
    soloButton_.setToggleState(chain.solo, juce::dontSendNotification);
    soloButton_.onClick = [this]() { onSoloClicked(); };
    addAndMakeVisible(soloButton_);

    // On/bypass button (power icon)
    onButton_ = std::make_unique<magda::SvgButton>("Power", BinaryData::power_svg,
                                                   BinaryData::power_svgSize);
    onButton_->setClickingTogglesState(true);
    onButton_->setToggleState(!chain.bypassed, juce::dontSendNotification);  // On = not bypassed
    onButton_->setActive(!chain.bypassed);
    onButton_->setTooltip("Chain power");
    onButton_->onClick = [this]() {
        onButton_->setActive(onButton_->getToggleState());
        onBypassClicked();
    };
    addAndMakeVisible(*onButton_);

    deleteButton_ = std::make_unique<magda::SvgButton>("Close", BinaryData::close_svg,
                                                       BinaryData::close_svgSize);
    deleteButton_->setTooltip("Remove chain");
    deleteButton_->onClick = [this]() { onDeleteClicked(); };
    addAndMakeVisible(*deleteButton_);

    zones_ = chain.zones;
    zoneBar_.setZones(chain.zones);
    zoneBar_.onCommit = [this](const magda::ChainZones& zones) { commitZones(zones); };
    addChildComponent(zoneBar_);

    roundRobinButton_.setClickingTogglesState(true);
    roundRobinButton_.setToggleState(chain.zones.roundRobin, juce::dontSendNotification);
    roundRobinButton_.setTooltip("Round robin: take turns with the other RR chains, one note each");
    roundRobinButton_.onClick = [this]() {
        auto zones = zones_;
        zones.roundRobin = roundRobinButton_.getToggleState();
        commitZones(zones);
    };
    addChildComponent(roundRobinButton_);

    styleControls();
    setAlpha(chain.bypassed ? kDisabledAlpha : 1.0f);

    // Hover covers the children too, so the row highlights under its controls.
    addMouseListener(this, true);
}

ChainRowComponent::~ChainRowComponent() {
    roundRobinButton_.setLookAndFeel(nullptr);
    removeMouseListener(this);
    magda::SelectionManager::getInstance().removeListener(this);
    magda::TrackManager::getInstance().removeListener(this);
}

void ChainRowComponent::styleControls() {
    using node_header::DeviceIcon;
    nameLabel_.setFont(FontManager::getInstance().getUIFontMedium(12.0f));
    nameLabel_.setColour(juce::Label::textColourId,
                         ActiveTheme::getColour(ActiveTheme::DEVICE_VALUE_TEXT));
    nameLabel_.setColour(juce::Label::backgroundWhenEditingColourId,
                         ActiveTheme::getColour(ActiveTheme::DEVICE_WELL));
    nameLabel_.setColour(juce::Label::textWhenEditingColourId,
                         ActiveTheme::getColour(ActiveTheme::DEVICE_VALUE_TEXT));
    nameLabel_.setBorderSize({});

    for (auto* value : {&gainLabel_, &panLabel_}) {
        value->setDrawBackground(false);
        value->setDrawBorder(false);
        value->setShowFillIndicator(false);
        value->setShowText(false);
    }

    // The same mute / solo as every other view, in the chipless device style.
    node_header::applyDeviceMuteStyle(muteButton_, BUTTON_HEIGHT);
    node_header::applyDeviceSoloStyle(soloButton_, BUTTON_HEIGHT);

    // The device header's glyphs: power 12px and close 10px.
    node_header::applyDeviceIconStyle(*onButton_, DeviceIcon::Power, juce::Colour(0xFFE6E6E6),
                                      ActiveTheme::DEVICE_GREEN, BUTTON_HEIGHT);
    onButton_->setIconPadding((BUTTON_HEIGHT - 12.0f) / 2.0f);
    node_header::applyDeviceIconStyle(*deleteButton_, DeviceIcon::Close, juce::Colour(0xFFB3B3B3),
                                      ActiveTheme::DEVICE_BLUE, BUTTON_HEIGHT);
    deleteButton_->setIconPadding((BUTTON_HEIGHT - 10.0f) / 2.0f);
    roundRobinButton_.setLookAndFeel(&node_header::GlyphToggleLookAndFeel::getInstance());
    roundRobinButton_.setColour(juce::TextButton::textColourOnId,
                                ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE));
}

void ChainRowComponent::lookAndFeelChanged() {
    styleControls();
    repaint();
}

ChainRowComponent::Columns ChainRowComponent::columnsFor(juce::Rectangle<int> row) {
    auto area = row.reduced(10, 0);
    Columns columns;
    columns.name = area.removeFromLeft(100);
    area.removeFromLeft(COLUMN_GAP);
    columns.buttons = area.removeFromRight(4 * BUTTON_WIDTH + 3 * 4);
    area.removeFromRight(COLUMN_GAP);
    columns.pan = area.removeFromRight(64);
    area.removeFromRight(COLUMN_GAP);
    columns.gain = area;
    return columns;
}

void ChainRowComponent::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const auto fill = selected_  ? ActiveTheme::DEVICE_ROW_SELECTED
                      : hovered_ ? ActiveTheme::DEVICE_ROW_HOVER
                                 : ActiveTheme::DEVICE_FIELD;
    const auto border = selected_  ? ActiveTheme::DEVICE_ROW_SELECTED_BORDER
                        : hovered_ ? ActiveTheme::DEVICE_LINE
                                   : ActiveTheme::DEVICE_FIELD_BORDER;
    g.setColour(ActiveTheme::getColour(fill));
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(ActiveTheme::getColour(border));
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
    if (selected_) {
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE));
        g.fillRect(juce::Rectangle<float>(0.0f, 4.0f, 2.0f, bounds.getHeight() - 7.0f));
    }

    const auto name = columnsFor(getLocalBounds()).name;
    g.setColour(device_shell::chainColour(colourIndex_));
    g.fillEllipse(name.withWidth(8).withSizeKeepingCentre(8, 8).toFloat());
    if (nameLabel_.getText().isEmpty() && !nameLabel_.isBeingEdited()) {
        // A band name is the band's real name until the user gives it another.
        const bool bandName = placeholderName_ != "Chain";
        g.setColour(ActiveTheme::getColour(bandName ? ActiveTheme::DEVICE_VALUE_TEXT
                                                    : ActiveTheme::DEVICE_DIM2));
        const auto font = FontManager::getInstance().getUIFont(12.0f);
        g.setFont(bandName ? font : font.italicised());
        g.drawText(placeholderName_, nameLabel_.getBounds(), juce::Justification::centredLeft,
                   false);
    }

    if (view_ == View::Mix) {
        device_shell::paintGainSlider(g, gainLabel_.getBounds(), gainLabel_.getValue(), kMinGainDb);
        device_shell::paintPanSlider(g, panLabel_.getBounds(), panLabel_.getValue());
    }
}

void ChainRowComponent::mouseEnter(const juce::MouseEvent& /*event*/) {
    if (!hovered_) {
        hovered_ = true;
        repaint();
    }
}

void ChainRowComponent::mouseExit(const juce::MouseEvent& /*event*/) {
    const bool over = isMouseOver(true);
    if (hovered_ != over) {
        hovered_ = over;
        repaint();
    }
}

void ChainRowComponent::mouseDown(const juce::MouseEvent& /*event*/) {
    // Just visual feedback - actual selection happens on mouseUp to avoid
    // issues with multiple mouseDown events during layout changes
}

void ChainRowComponent::mouseUp(const juce::MouseEvent& event) {
    // Only handle if mouse is still over this component (user didn't drag away)
    if (event.eventComponent != this || !contains(event.getPosition())) {
        return;
    }
    applySelectionForClick(event.mods);
}

void ChainRowComponent::mouseDoubleClick(const juce::MouseEvent& event) {
    // Double-click toggles expand/collapse of this chain
    if (event.eventComponent == this && onDoubleClick) {
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

void ChainRowComponent::setPlaceholderName(const juce::String& name) {
    if (placeholderName_ != name) {
        placeholderName_ = name;
        repaint();
    }
}

void ChainRowComponent::setColourIndex(int index) {
    if (colourIndex_ != index) {
        colourIndex_ = index;
        repaint();
    }
}

void ChainRowComponent::resized() {
    const auto columns = columnsFor(getLocalBounds());
    nameLabel_.setBounds(columns.name.withTrimmedLeft(16));
    gainLabel_.setBounds(columns.gain.withSizeKeepingCentre(columns.gain.getWidth(), 20));
    panLabel_.setBounds(columns.pan.withSizeKeepingCentre(columns.pan.getWidth(), 20));

    auto zoneArea = columns.gain.getUnion(columns.buttons);
    if (view_ == View::Velocity)
        roundRobinButton_.setBounds(
            zoneArea.removeFromRight(BUTTON_WIDTH).withSizeKeepingCentre(BUTTON_WIDTH, 20));
    zoneBar_.setBounds(zoneArea.reduced(0, 6));

    auto buttons = columns.buttons;
    for (juce::Component* button :
         {static_cast<juce::Component*>(&muteButton_), static_cast<juce::Component*>(&soloButton_),
          static_cast<juce::Component*>(onButton_.get()),
          static_cast<juce::Component*>(deleteButton_.get())}) {
        button->setBounds(
            buttons.removeFromLeft(BUTTON_WIDTH)
                .withSizeKeepingCentre(BUTTON_WIDTH, static_cast<int>(BUTTON_HEIGHT)));
        buttons.removeFromLeft(4);
    }
}

int ChainRowComponent::getPreferredHeight() {
    return ROW_HEIGHT;
}

void ChainRowComponent::updateFromChain(const magda::ChainInfo& chain) {
    // Don't clobber an in-progress rename if a property change arrives mid-edit.
    if (!nameLabel_.isBeingEdited())
        nameLabel_.setText(chain.name, juce::dontSendNotification);
    syncMuteGlyph(muteButton_, chain.muted);
    soloButton_.setToggleState(chain.solo, juce::dontSendNotification);
    gainLabel_.setValue(chain.volume, juce::dontSendNotification);
    panLabel_.setValue(chain.pan, juce::dontSendNotification);
    onButton_->setToggleState(!chain.bypassed, juce::dontSendNotification);
    onButton_->setActive(!chain.bypassed);
    zones_ = chain.zones;
    zoneBar_.setZones(chain.zones);
    roundRobinButton_.setToggleState(chain.zones.roundRobin, juce::dontSendNotification);
    if (const auto* rack = magda::TrackManager::getInstance().getRackByPath(nodePath_.parent()))
        zoneBar_.setMarker(modulatedSelector_.value_or(rack->chainSelector));

    setAlpha(chain.bypassed ? kDisabledAlpha : 1.0f);
    repaint();
}

void ChainRowComponent::setModulatedSelector(std::optional<float> value) {
    modulatedSelector_ = value;
    if (const auto* rack = magda::TrackManager::getInstance().getRackByPath(nodePath_.parent()))
        zoneBar_.setMarker(value.value_or(rack->chainSelector));
}

void ChainRowComponent::setView(View view) {
    view_ = view;
    const bool mix = view == View::Mix;
    for (juce::Component* control :
         {static_cast<juce::Component*>(&gainLabel_), static_cast<juce::Component*>(&panLabel_),
          static_cast<juce::Component*>(&muteButton_), static_cast<juce::Component*>(&soloButton_),
          static_cast<juce::Component*>(onButton_.get()),
          static_cast<juce::Component*>(deleteButton_.get())})
        control->setVisible(mix);
    zoneBar_.setVisible(!mix);
    zoneBar_.setAxis(view == View::Key        ? ZoneBar::Axis::Key
                     : view == View::Velocity ? ZoneBar::Axis::Velocity
                                              : ZoneBar::Axis::Selector);
    zoneBar_.setShoulderFades(true);
    roundRobinButton_.setVisible(view == View::Velocity);
    resized();
    repaint();
}

// Deferred and undoable: a zone change recompiles the rack and refreshes this row.
void ChainRowComponent::commitZones(const magda::ChainZones& zones) {
    magda::ChainPropertyPatch patch;
    patch.zones = zones;
    juce::MessageManager::callAsync([path = nodePath_, patch]() {
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetChainPropertiesByPathCommand>(path, patch));
    });
}

void ChainRowComponent::onMuteClicked() {
    auto& tm = magda::TrackManager::getInstance();
    const bool muted = muteButton_.getToggleState();
    syncMuteGlyph(muteButton_, muted);
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
    // Deferred and undoable: the removal notifies synchronously and the rebuild
    // it triggers destroys this row while its own click is still on the stack,
    // and a chain carries every device in it (#2232).
    const auto chainPath = nodePath_.isValid()
                               ? nodePath_
                               : magda::ChainNodePath::rack(trackId_, rackId_).withChain(chainId_);
    juce::MessageManager::callAsync([chainPath]() {
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::RemoveChainByPathCommand>(chainPath));
    });
}

}  // namespace magda::daw::ui
