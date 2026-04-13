#include "ParamGridComponent.hpp"

#include "ui/components/chain/DeviceSlotHeaderLayout.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

ParamGridComponent::ParamGridComponent() {
    // Pagination controls
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

    // Create parameter slots
    for (int i = 0; i < NUM_PARAMS; ++i) {
        paramSlots_[i] = std::make_unique<ParamSlotComponent>(i);
        addAndMakeVisible(*paramSlots_[i]);
    }
}

ParamGridComponent::~ParamGridComponent() = default;

void ParamGridComponent::updateParameterSlots(
    const magda::DeviceInfo& device, int currentPage,
    std::function<void(int paramIndex, double value)> onValueChanged) {
    const int paramsPerPage = getParamsPerPage();
    const int pageOffset = currentPage * paramsPerPage;

    const bool useVisibilityFilter = !device.visibleParameters.empty();
    const int visibleCount = useVisibilityFilter ? static_cast<int>(device.visibleParameters.size())
                                                 : static_cast<int>(device.parameters.size());

    for (int i = 0; i < NUM_PARAMS; ++i) {
        const int slotIndex = pageOffset + i;

        if (slotIndex < visibleCount) {
            int paramIndex;
            if (useVisibilityFilter) {
                paramIndex = device.visibleParameters[static_cast<size_t>(slotIndex)];
            } else {
                paramIndex = slotIndex;
            }

            if (paramIndex >= 0 && paramIndex < static_cast<int>(device.parameters.size())) {
                const auto& param = device.parameters[static_cast<size_t>(paramIndex)];
                paramSlots_[i]->setParamIndex(paramIndex);
                paramSlots_[i]->setParamName(param.name);
                paramSlots_[i]->setParameterInfo(param);
                paramSlots_[i]->setParamValue(param.currentValue);
                paramSlots_[i]->setShowEmptyText(false);
                paramSlots_[i]->setEnabled(true);
                paramSlots_[i]->setVisible(true);

                if (onValueChanged) {
                    paramSlots_[i]->onValueChanged = [onValueChanged, paramIndex](double value) {
                        onValueChanged(paramIndex, value);
                    };
                } else {
                    paramSlots_[i]->onValueChanged = nullptr;
                }
            } else {
                paramSlots_[i]->setParamName("-");
                paramSlots_[i]->setShowEmptyText(true);
                paramSlots_[i]->setEnabled(false);
                paramSlots_[i]->setVisible(true);
                paramSlots_[i]->onValueChanged = nullptr;
            }
        } else {
            paramSlots_[i]->setParamName("-");
            paramSlots_[i]->setShowEmptyText(true);
            paramSlots_[i]->setEnabled(false);
            paramSlots_[i]->setVisible(true);
            paramSlots_[i]->onValueChanged = nullptr;
        }
    }
}

void ParamGridComponent::updateParameterValues(const magda::DeviceInfo& device, int currentPage) {
    const int paramsPerPage = getParamsPerPage();
    const int pageOffset = currentPage * paramsPerPage;
    const bool useVisibilityFilter = !device.visibleParameters.empty();
    const int visibleCount = useVisibilityFilter ? static_cast<int>(device.visibleParameters.size())
                                                 : static_cast<int>(device.parameters.size());

    for (int i = 0; i < NUM_PARAMS; ++i) {
        const int slotIndex = pageOffset + i;

        if (slotIndex < visibleCount) {
            int paramIndex;
            if (useVisibilityFilter) {
                paramIndex = device.visibleParameters[static_cast<size_t>(slotIndex)];
            } else {
                paramIndex = slotIndex;
            }

            if (paramIndex >= 0 && paramIndex < static_cast<int>(device.parameters.size())) {
                const auto& param = device.parameters[static_cast<size_t>(paramIndex)];
                paramSlots_[i]->setParamValue(param.currentValue);
            }
        }
    }
}

void ParamGridComponent::updateParamModulation(
    const magda::ModArray* mods, const magda::MacroArray* macros, const magda::ModArray* rackMods,
    const magda::MacroArray* rackMacros, const magda::ModArray* trackMods,
    const magda::MacroArray* trackMacros, magda::DeviceId deviceId,
    const magda::ChainNodePath& devicePath, int selectedModIndex, int selectedMacroIndex) {
    for (int i = 0; i < NUM_PARAMS; ++i) {
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

void ParamGridComponent::updatePageControls(int currentPage, int totalPages) {
    currentPage_ = currentPage;
    totalPages_ = totalPages;
    pageLabel_->setText(juce::String(currentPage_ + 1) + "/" + juce::String(totalPages_),
                        juce::dontSendNotification);
    prevPageButton_->setEnabled(currentPage_ > 0);
    nextPageButton_->setEnabled(currentPage_ < totalPages_ - 1);
}

void ParamGridComponent::setGridVisible(bool visible) {
    for (int i = 0; i < NUM_PARAMS; ++i)
        paramSlots_[i]->setVisible(visible);
}

void ParamGridComponent::setPaginationVisible(bool visible) {
    prevPageButton_->setVisible(visible);
    nextPageButton_->setVisible(visible);
    pageLabel_->setVisible(visible);
}

void ParamGridComponent::setLearnMode(bool active) {
    learnMode_ = active;
    if (!active)
        clearHighlight();
}

void ParamGridComponent::highlightSlot(int slotIndex) {
    if (highlightedSlot_ >= 0 && highlightedSlot_ < NUM_PARAMS)
        paramSlots_[highlightedSlot_]->setSelected(false);
    highlightedSlot_ = slotIndex;
    if (slotIndex >= 0 && slotIndex < NUM_PARAMS)
        paramSlots_[slotIndex]->setSelected(true);
}

void ParamGridComponent::clearHighlight() {
    if (highlightedSlot_ >= 0 && highlightedSlot_ < NUM_PARAMS)
        paramSlots_[highlightedSlot_]->setSelected(false);
    highlightedSlot_ = -1;
}

void ParamGridComponent::setSlotFonts(int slotIndex, const juce::Font& labelFont,
                                      const juce::Font& valueFont) {
    jassert(slotIndex >= 0 && slotIndex < NUM_PARAMS);
    paramSlots_[slotIndex]->setFonts(labelFont, valueFont);
}

void ParamGridComponent::setAllSlotsSelected(bool selected) {
    for (int i = 0; i < NUM_PARAMS; ++i)
        paramSlots_[i]->setSelected(selected);
}

void ParamGridComponent::setSlotSelected(int slotIndex, bool selected) {
    jassert(slotIndex >= 0 && slotIndex < NUM_PARAMS);
    paramSlots_[slotIndex]->setSelected(selected);
}

void ParamGridComponent::layoutContent(const juce::Font& labelFont, const juce::Font& valueFont) {
    auto area = getLocalBounds();

    // Pagination row at top
    area.removeFromTop(2);
    auto paginationArea = area.removeFromTop(PAGINATION_HEIGHT);
    area.removeFromTop(2);

    placeNavArrow(*prevPageButton_, paginationArea, true);
    placeNavArrow(*nextPageButton_, paginationArea, false);
    pageLabel_->setBounds(paginationArea);

    // Small gap
    area.removeFromTop(2);

    // Slots grid — spread evenly across remaining area
    area = area.reduced(2, 0);
    constexpr int paramsPerRow = PARAMS_PER_ROW;
    constexpr int numRows = NUM_PARAMS / paramsPerRow;
    int cellWidth = area.getWidth() / paramsPerRow;
    int cellHeight = area.getHeight() / numRows;

    for (int i = 0; i < NUM_PARAMS; ++i) {
        int row = i / paramsPerRow;
        int col = i % paramsPerRow;
        int x = area.getX() + col * cellWidth + 2;
        int y = area.getY() + row * cellHeight + 2;

        paramSlots_[i]->setFonts(labelFont, valueFont);
        paramSlots_[i]->setBounds(x, y, cellWidth - 4, cellHeight - 4);
        paramSlots_[i]->setVisible(true);
    }

    setPaginationVisible(true);
}

void ParamGridComponent::resized() {
    // Layout is driven by layoutContent() which is called from DeviceSlotComponent
    // after setBounds() is set with the appropriate region.
}

}  // namespace magda::daw::ui
