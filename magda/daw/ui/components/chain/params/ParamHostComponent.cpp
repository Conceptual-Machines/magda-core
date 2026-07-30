#include "params/ParamHostComponent.hpp"

#include <algorithm>

#include "ui/components/chain/layout/DeviceSlotHeaderLayout.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

class ParamPageTabBar final : public juce::TabbedButtonBar {
  public:
    ParamPageTabBar() : juce::TabbedButtonBar(juce::TabbedButtonBar::TabsAtTop) {
        setMinimumTabScaleFactor(0.5);
    }

    void setPages(const DeviceParamLayout& layout, const magda::DeviceInfo& device, int totalPages,
                  int currentPage) {
        suppressCallback_ = true;
        clearTabs();
        const auto tabColour = DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f);
        for (int page = 0; page < totalPages; ++page)
            addTab(layout.pageName(device, page), tabColour, -1);
        setCurrentTabIndex(currentPage, false);
        suppressCallback_ = false;
    }

    std::function<void(int)> onPageSelected;

    void currentTabChanged(int newCurrentTabIndex, const juce::String&) override {
        if (!suppressCallback_ && onPageSelected)
            onPageSelected(newCurrentTabIndex);
    }

  private:
    bool suppressCallback_ = false;
};

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

void applyMeter(MeterCellComponent& meterCell, const magda::MeterInfo& meter,
                const std::function<float(int meterIndex)>& meterSource) {
    meterCell.setMeterInfo(meter);
    if (meterSource) {
        const int index = meter.meterIndex;
        meterCell.setSource([meterSource, index]() { return meterSource(index); });
    } else {
        // No supplier means the device family does not report values back;
        // the cell then draws its floor rather than a stale reading.
        meterCell.setSource(nullptr);
    }
    meterCell.setVisible(true);
}

}  // namespace

ParamHostComponent::ParamHostComponent(std::unique_ptr<DeviceParamLayout> layout)
    : layout_(std::move(layout)) {
    jassert(layout_ != nullptr);
    cellCount_ = layout_->cellCount();
    cellsPerRow_ = layout_->cellsPerRow();
    jassert(cellCount_ >= 0 && cellCount_ <= kMaxCells);
    jassert((cellCount_ == 0 && cellsPerRow_ == 0) || cellsPerRow_ > 0);

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

    pageTabBar_ = std::make_unique<ParamPageTabBar>();
    pageTabBar_->onPageSelected = [this](int pageIndex) {
        if (onPageSelected)
            onPageSelected(pageIndex);
    };
    addAndMakeVisible(*pageTabBar_);

    for (int i = 0; i < cellCount_; ++i) {
        paramSlots_[i] = std::make_unique<ParamSlotComponent>(i);
        addAndMakeVisible(*paramSlots_[i]);

        meterCells_[i] = std::make_unique<MeterCellComponent>();
        meterCells_[i]->setVisible(false);
        addChildComponent(*meterCells_[i]);
    }
}

ParamHostComponent::~ParamHostComponent() = default;

void ParamHostComponent::setMeterSource(std::function<float(int meterIndex)> source) {
    meterSource_ = std::move(source);
}

void ParamHostComponent::updateParameterSlots(
    const magda::DeviceInfo& device, int currentPage,
    std::function<void(int paramIndex, double value)> onValueChanged) {
    cellSpans_.assign(static_cast<size_t>(std::max(0, cellCount_)), 1);
    cellIsMeter_.assign(static_cast<size_t>(std::max(0, cellCount_)), false);
    for (int i = 0; i < cellCount_; ++i) {
        const auto cell = layout_->cellFor(device, i, currentPage);
        cellSpans_[static_cast<size_t>(i)] = std::max(1, cell.span);

        // A cell is a param slot or a meter, never both, so the other one is
        // always put away first.
        const bool isMeter = cell.mode == ParamCell::Mode::Meter;
        cellIsMeter_[static_cast<size_t>(i)] = isMeter;
        if (!isMeter) {
            meterCells_[i]->setSource(nullptr);
            meterCells_[i]->setVisible(false);
        }

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
            case ParamCell::Mode::Meter: {
                applyHidden(*paramSlots_[i]);
                if (cell.meterArrayIndex < 0 ||
                    cell.meterArrayIndex >= static_cast<int>(device.meters.size())) {
                    meterCells_[i]->setVisible(false);
                    break;
                }
                applyMeter(*meterCells_[i],
                           device.meters[static_cast<size_t>(cell.meterArrayIndex)], meterSource_);
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

void ParamHostComponent::updatePageControls(const magda::DeviceInfo& device, int currentPage,
                                            int totalPages) {
    currentPage_ = currentPage;
    totalPages_ = totalPages;
    if (layout_->wantsPageTabs())
        pageTabBar_->setPages(*layout_, device, totalPages_, currentPage_);
    pageLabel_->setText(juce::String(currentPage_ + 1) + "/" + juce::String(totalPages_),
                        juce::dontSendNotification);
    prevPageButton_->setEnabled(currentPage_ > 0);
    nextPageButton_->setEnabled(currentPage_ < totalPages_ - 1);
    setPaginationVisible(true);
}

void ParamHostComponent::setGridVisible(bool visible) {
    for (int i = 0; i < cellCount_; ++i) {
        const bool isMeter =
            i < static_cast<int>(cellIsMeter_.size()) && cellIsMeter_[static_cast<size_t>(i)];
        paramSlots_[i]->setVisible(visible && !isMeter);
        meterCells_[i]->setVisible(visible && isMeter);
    }
}

void ParamHostComponent::setPaginationVisible(bool visible) {
    const bool effective = visible && layout_->wantsPagination() && totalPages_ > 1;
    const bool tabs = effective && layout_->wantsPageTabs();
    prevPageButton_->setVisible(effective && !tabs);
    nextPageButton_->setVisible(effective && !tabs);
    pageLabel_->setVisible(effective && !tabs);
    pageTabBar_->setVisible(tabs);
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

    if (cellCount_ <= 0 || cellsPerRow_ <= 0) {
        setPaginationVisible(false);
        return;
    }

    area.removeFromTop(2);
    juce::Rectangle<int> paginationArea;
    if (layout_->wantsPagination()) {
        paginationArea = area.removeFromTop(PAGINATION_HEIGHT);
        area.removeFromTop(4);
    }

    if (layout_->wantsPagination()) {
        if (layout_->wantsPageTabs()) {
            pageTabBar_->setBounds(paginationArea);
        } else {
            placeNavArrow(*prevPageButton_, paginationArea, true);
            placeNavArrow(*nextPageButton_, paginationArea, false);
            pageLabel_->setBounds(paginationArea);
        }
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

        // A spanning cell keeps the gutters of a single one, so a wide control
        // lines up with its narrow neighbours instead of gaining extra padding
        // per cell it swallowed.
        const int span = i < static_cast<int>(cellSpans_.size())
                             ? std::max(1, cellSpans_[static_cast<size_t>(i)])
                             : 1;
        const int width = span * cellWidth - 4;

        paramSlots_[i]->setFonts(labelFont, valueFont);
        paramSlots_[i]->setBounds(x, y, width, cellHeight - 4);
        // The meter cell shares its cell's geometry so a readout lines up with
        // the controls around it whichever one the layout picked.
        meterCells_[i]->setFonts(labelFont, valueFont);
        meterCells_[i]->setBounds(x, y, width, cellHeight - 4);
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
