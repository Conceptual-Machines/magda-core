#include "params/ParamHostComponent.hpp"

#include "ui/components/chain/layout/DeviceSlotHeaderLayout.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

void applyFilled(ParamSlotComponent& slot, const magda::ParameterInfo& param, const ParamCell& cell,
                 const std::function<void(int paramIndex, double value)>& onValueChanged) {
    slot.setParamIndex(cell.targetParamIndex);
    slot.setParamName(param.name);
    slot.setParameterInfo(param);
    slot.setParamValue(param.currentValue);
    slot.setShowEmptyText(false);
    slot.setEnabled(cell.enabled);
    slot.setVisible(true);

    if (onValueChanged) {
        const int target = cell.targetParamIndex;
        slot.onValueChanged = [onValueChanged, target](double value) {
            onValueChanged(target, value);
        };
    } else {
        slot.onValueChanged = nullptr;
    }
}

void applyPlaceholder(ParamSlotComponent& slot) {
    slot.cancelGesture();
    slot.setParamName("-");
    slot.setShowEmptyText(true);
    slot.setEnabled(false);
    slot.setVisible(true);
    slot.onValueChanged = nullptr;
}

void applyHidden(ParamSlotComponent& slot) {
    slot.cancelGesture();
    slot.setVisible(false);
    slot.onValueChanged = nullptr;
}

}  // namespace

ParamHostComponent::ParamHostComponent(std::unique_ptr<DeviceParamLayout> layout)
    : layout_(std::move(layout)) {
    jassert(layout_ != nullptr);
    cellCount_ = layout_->cellCount();
    cellsPerRow_ = layout_->cellsPerRow();
    jassert(cellCount_ > 0 && cellCount_ <= kMaxCells);
    jassert(cellsPerRow_ > 0);

    prevPageButton_ = makeNavArrowButton("prev", 0.5f);
    prevPageButton_->onClick = [this]() {
        if (onPrevPage)
            onPrevPage();
    };
    addAndMakeVisible(*prevPageButton_);

    nextPageButton_ = makeNavArrowButton("next", 0.0f);
    nextPageButton_->onClick = [this]() {
        if (onNextPage)
            onNextPage();
    };
    addAndMakeVisible(*nextPageButton_);

    pageLabel_ = std::make_unique<juce::Label>();
    pageLabel_->setFont(FontManager::getInstance().getUIFont(9.0f));
    pageLabel_->setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    pageLabel_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*pageLabel_);

    for (int i = 0; i < cellCount_; ++i) {
        paramSlots_[i] = std::make_unique<ParamSlotComponent>(i);
        addAndMakeVisible(*paramSlots_[i]);
    }
}

ParamHostComponent::~ParamHostComponent() = default;

void ParamHostComponent::updateParameterSlots(
    const magda::DeviceInfo& device, int currentPage,
    std::function<void(int paramIndex, double value)> onValueChanged) {
    for (int i = 0; i < cellCount_; ++i) {
        const auto cell = layout_->cellFor(device, i, currentPage);
        switch (cell.mode) {
            case ParamCell::Mode::Filled: {
                if (cell.paramArrayIndex < 0 ||
                    cell.paramArrayIndex >= static_cast<int>(device.parameters.size())) {
                    applyPlaceholder(*paramSlots_[i]);
                    break;
                }
                const auto& param = device.parameters[static_cast<size_t>(cell.paramArrayIndex)];
                applyFilled(*paramSlots_[i], param, cell, onValueChanged);
                break;
            }
            case ParamCell::Mode::Placeholder:
                applyPlaceholder(*paramSlots_[i]);
                break;
            case ParamCell::Mode::Hidden:
                applyHidden(*paramSlots_[i]);
                break;
        }
    }
}

void ParamHostComponent::updateParameterValues(const magda::DeviceInfo& device, int currentPage) {
    for (int i = 0; i < cellCount_; ++i) {
        const auto cell = layout_->cellFor(device, i, currentPage);
        if (cell.mode != ParamCell::Mode::Filled)
            continue;
        if (cell.paramArrayIndex < 0 ||
            cell.paramArrayIndex >= static_cast<int>(device.parameters.size()))
            continue;
        const auto& param = device.parameters[static_cast<size_t>(cell.paramArrayIndex)];
        paramSlots_[i]->setParamValue(param.currentValue);
        paramSlots_[i]->setEnabled(cell.enabled);
    }
}

void ParamHostComponent::refreshEnabledStates(const magda::DeviceInfo& device, int currentPage) {
    for (int i = 0; i < cellCount_; ++i) {
        const auto cell = layout_->cellFor(device, i, currentPage);
        if (cell.mode != ParamCell::Mode::Filled)
            continue;
        paramSlots_[i]->setEnabled(cell.enabled);
    }
}

void ParamHostComponent::updateParamModulation(
    const magda::ModArray* mods, const magda::MacroArray* macros, const magda::ModArray* rackMods,
    const magda::MacroArray* rackMacros, const magda::ModArray* trackMods,
    const magda::MacroArray* trackMacros, magda::DeviceId deviceId,
    const magda::ChainNodePath& devicePath, int selectedModIndex, int selectedMacroIndex) {
    for (int i = 0; i < cellCount_; ++i) {
        paramSlots_[i]->setDeviceId(deviceId);
        paramSlots_[i]->setDevicePath(devicePath);
        paramSlots_[i]->setAvailableMods(mods);
        paramSlots_[i]->setAvailableRackMods(rackMods);
        paramSlots_[i]->setAvailableTrackMods(trackMods);
        paramSlots_[i]->setAvailableMacros(macros);
        paramSlots_[i]->setAvailableRackMacros(rackMacros);
        paramSlots_[i]->setAvailableTrackMacros(trackMacros);
        paramSlots_[i]->setSelectedModIndex(selectedModIndex);
        paramSlots_[i]->setSelectedMacroIndex(selectedMacroIndex);
        paramSlots_[i]->repaint();
    }
}

void ParamHostComponent::updatePageControls(int currentPage, int totalPages) {
    currentPage_ = currentPage;
    totalPages_ = totalPages;
    pageLabel_->setText(juce::String(currentPage_ + 1) + "/" + juce::String(totalPages_),
                        juce::dontSendNotification);
    prevPageButton_->setEnabled(currentPage_ > 0);
    nextPageButton_->setEnabled(currentPage_ < totalPages_ - 1);
}

void ParamHostComponent::setGridVisible(bool visible) {
    for (int i = 0; i < cellCount_; ++i)
        paramSlots_[i]->setVisible(visible);
}

void ParamHostComponent::setPaginationVisible(bool visible) {
    const bool effective = visible && layout_->wantsPagination();
    prevPageButton_->setVisible(effective);
    nextPageButton_->setVisible(effective);
    pageLabel_->setVisible(effective);
}

void ParamHostComponent::setLearnMode(bool active) {
    learnMode_ = active;
    if (!active)
        clearHighlight();
}

void ParamHostComponent::highlightSlot(int slotIndex) {
    if (highlightedSlot_ >= 0 && highlightedSlot_ < cellCount_)
        paramSlots_[highlightedSlot_]->setSelected(false);
    highlightedSlot_ = slotIndex;
    if (slotIndex >= 0 && slotIndex < cellCount_)
        paramSlots_[slotIndex]->setSelected(true);
}

void ParamHostComponent::clearHighlight() {
    if (highlightedSlot_ >= 0 && highlightedSlot_ < cellCount_)
        paramSlots_[highlightedSlot_]->setSelected(false);
    highlightedSlot_ = -1;
}

void ParamHostComponent::setSlotFonts(int slotIndex, const juce::Font& labelFont,
                                      const juce::Font& valueFont) {
    jassert(slotIndex >= 0 && slotIndex < cellCount_);
    paramSlots_[slotIndex]->setFonts(labelFont, valueFont);
}

void ParamHostComponent::setAllSlotsSelected(bool selected) {
    for (int i = 0; i < cellCount_; ++i)
        paramSlots_[i]->setSelected(selected);
}

void ParamHostComponent::setSlotSelected(int slotIndex, bool selected) {
    jassert(slotIndex >= 0 && slotIndex < cellCount_);
    paramSlots_[slotIndex]->setSelected(selected);
}

void ParamHostComponent::layoutContent(const juce::Font& labelFont, const juce::Font& valueFont) {
    auto area = getLocalBounds();

    area.removeFromTop(2);
    juce::Rectangle<int> paginationArea;
    if (layout_->wantsPagination()) {
        paginationArea = area.removeFromTop(PAGINATION_HEIGHT);
        area.removeFromTop(4);
    }

    if (layout_->wantsPagination()) {
        placeNavArrow(*prevPageButton_, paginationArea, true);
        placeNavArrow(*nextPageButton_, paginationArea, false);
        pageLabel_->setBounds(paginationArea);
    }

    area = area.reduced(2, 0);
    const int numRows = (cellCount_ + cellsPerRow_ - 1) / cellsPerRow_;
    const int cellWidth = area.getWidth() / cellsPerRow_;
    const int cellHeight = numRows > 0 ? area.getHeight() / numRows : area.getHeight();

    for (int i = 0; i < cellCount_; ++i) {
        const int row = i / cellsPerRow_;
        const int col = i % cellsPerRow_;
        const int x = area.getX() + col * cellWidth + 2;
        const int y = area.getY() + row * cellHeight + 2;

        paramSlots_[i]->setFonts(labelFont, valueFont);
        paramSlots_[i]->setBounds(x, y, cellWidth - 4, cellHeight - 4);
        // Visibility is owned by updateParameterSlots() via the layout —
        // don't override it on layout passes.
    }

    setPaginationVisible(true);
}

void ParamHostComponent::resized() {
    // Layout is driven by layoutContent() which is called from DeviceSlotComponent
    // after setBounds() is set with the appropriate region.
}

}  // namespace magda::daw::ui
