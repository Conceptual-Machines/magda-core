#include "RoutingSelector.hpp"

#include <algorithm>
#include <functional>
#include <utility>

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "HardwareInputLevels.hpp"
#include "LevelMeterBallistics.hpp"
#include "LevelMeterScale.hpp"

namespace magda {

namespace {

/// A menu row with a small level bar per input channel it reads.
class MeteredInputItem final : public juce::PopupMenu::CustomComponent, private juce::Timer {
  public:
    MeteredInputItem(juce::String text, std::vector<int> channels, bool ticked,
                     std::weak_ptr<const HardwareInputLevels> levels)
        : text_(std::move(text)), ticked_(ticked), levels_(std::move(levels)) {
        bars_.setChannels(std::move(channels));
        startTimerHz(30);
    }

    void getIdealSize(int& width, int& height) override {
        getLookAndFeel().getIdealPopupMenuItemSize(text_, false, -1, width, height);
        width += kMeterWidth + kMeterPadding;
    }

    void paint(juce::Graphics& g) override {
        getLookAndFeel().drawPopupMenuItem(g, getLocalBounds(), false, true, isItemHighlighted(),
                                           ticked_, false, text_, {}, nullptr, nullptr);
        bars_.paint(g, meterArea(), 3);
    }

  private:
    static constexpr int kMeterWidth = 28;
    static constexpr int kMeterPadding = 8;

    juce::Rectangle<int> meterArea() const {
        return getLocalBounds()
            .removeFromRight(kMeterWidth + kMeterPadding)
            .withTrimmedRight(kMeterPadding);
    }

    void timerCallback() override {
        if (bars_.follow(levels_.lock().get()))
            repaint(meterArea());
    }

    juce::String text_;
    bool ticked_ = false;
    std::weak_ptr<const HardwareInputLevels> levels_;
    InputLevelBars bars_;
};

}  // namespace

void InputLevelBars::setChannels(std::vector<int> channels) {
    if (channels == channels_)
        return;

    channels_ = std::move(channels);
    display_.assign(channels_.size(), 0.0f);
    lastUpdateMs_ = 0.0;
}

bool InputLevelBars::follow(const HardwareInputLevels* levels) {
    const auto elapsedMs = level_meter_ballistics::getElapsedMs(lastUpdateMs_);

    bool changed = false;
    for (std::size_t i = 0; i < channels_.size(); ++i) {
        const auto target = levels != nullptr ? levels->level(channels_[i]) : 0.0f;
        changed |= level_meter_ballistics::updateLevel(display_[i], target, elapsedMs);
    }
    return changed;
}

void InputLevelBars::paint(juce::Graphics& g, juce::Rectangle<int> area, int barHeight) const {
    constexpr int kGap = 1;
    const auto height = static_cast<int>(display_.size()) * (barHeight + kGap) - kGap;
    auto bars = area.withSizeKeepingCentre(area.getWidth(), height).toFloat();

    for (const auto level : display_) {
        const auto bar = bars.removeFromTop(static_cast<float>(barHeight));
        bars.removeFromTop(static_cast<float>(kGap));

        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRect(bar);

        const auto db = level_meter_scale::gainToDb(level);
        g.setColour(DarkTheme::getColour(db >= 0.0f     ? DarkTheme::LEVEL_METER_RED
                                         : db >= -12.0f ? DarkTheme::LEVEL_METER_YELLOW
                                                        : DarkTheme::LEVEL_METER_GREEN));
        g.fillRect(bar.withWidth(bar.getWidth() * level_meter_scale::dbToMeterPos(db)));
    }
}

RoutingSelector::RoutingSelector(Type type) : type_(type) {
    setRepaintsOnMouseActivity(true);
}

void RoutingSelector::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    auto mainArea = getMainButtonArea().toFloat();
    auto dropdownArea = getDropdownArea().toFloat();

    // Background: always use BUTTON_NORMAL, brighter on hover. Read-only
    // controls are dimmed and never react to hover.
    auto bgColour = DarkTheme::getColour(DarkTheme::BUTTON_NORMAL);
    if (readOnly_) {
        bgColour = bgColour.withAlpha(0.5f);
    } else if (isHovering_) {
        bgColour = bgColour.brighter(0.1f);
    }

    // Draw the control as one rounded field.
    g.setColour(bgColour);
    g.fillRoundedRectangle(bounds, 2.0f);

    // Draw dropdown area with only a slight tonal shift so the control reads as
    // one smooth field instead of two hard boxes.
    g.setColour(bgColour.darker(0.04f));
    g.fillRect(dropdownArea);

    // Draw separator line between main and dropdown
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.35f));
    g.drawLine(dropdownArea.getX(), dropdownArea.getY() + 2, dropdownArea.getX(),
               dropdownArea.getBottom() - 2, 1.0f);

    // The selected input's level, at the right of the label.
    if (labelBars_.meters())
        labelBars_.paint(g, getLabelMeterArea(), 2);

    // Draw selected name as text in main area. Read-only controls mirror their
    // owner's selection (readOnlyDisplay_) in a dimmed colour.
    auto textBounds = mainArea.reduced(2.0f, 1.0f);
    if (labelBars_.meters())
        textBounds.removeFromRight(static_cast<float>(LABEL_METER_WIDTH + 2));
    g.setColour(
        DarkTheme::getColour(readOnly_ ? DarkTheme::TEXT_SECONDARY : DarkTheme::TEXT_PRIMARY)
            .withAlpha(readOnly_ ? 0.6f : 1.0f));
    g.setFont(FontManager::getInstance().getUIFont(9.0f));
    const juce::String displayText =
        readOnly_ ? (readOnlyDisplay_.isNotEmpty() ? readOnlyDisplay_ : juce::String("None"))
                  : getSelectedName();
    g.drawText(displayText, textBounds, juce::Justification::centredLeft, true);

    // Draw dropdown arrow
    auto arrowBounds = dropdownArea.reduced(2.0f);
    float arrowSize = std::min(arrowBounds.getWidth(), arrowBounds.getHeight()) * 0.28f;
    float arrowX = arrowBounds.getCentreX();
    float arrowY = arrowBounds.getCentreY();

    juce::Path arrow;
    arrow.addTriangle(arrowX - arrowSize, arrowY - arrowSize * 0.5f, arrowX + arrowSize,
                      arrowY - arrowSize * 0.5f, arrowX, arrowY + arrowSize * 0.5f);

    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).withAlpha(0.62f));
    g.fillPath(arrow);

    // Draw border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.8f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 2.0f, 1.0f);
}

void RoutingSelector::resized() {
    // Layout is calculated in getMainButtonArea() and getDropdownArea()
}

void RoutingSelector::mouseDown(const juce::MouseEvent& e) {
    juce::ignoreUnused(e);
    if (readOnly_)
        return;
    showPopupMenu();
}

void RoutingSelector::mouseEnter(const juce::MouseEvent&) {
    if (readOnly_)
        return;
    isHovering_ = true;
    repaint();
}

void RoutingSelector::mouseExit(const juce::MouseEvent&) {
    if (readOnly_)
        return;
    isHovering_ = false;
    repaint();
}

void RoutingSelector::setReadOnly(bool readOnly, const juce::String& displayText) {
    if (readOnly_ == readOnly && readOnlyDisplay_ == displayText)
        return;
    readOnly_ = readOnly;
    readOnlyDisplay_ = displayText;
    if (readOnly_)
        isHovering_ = false;
    updateMetering();
    repaint();
}

void RoutingSelector::setEnabled(bool shouldBeEnabled) {
    if (enabled_ != shouldBeEnabled) {
        enabled_ = shouldBeEnabled;
        updateMetering();
        repaint();
    }
}

void RoutingSelector::setSelectedId(int id) {
    if (selectedId_ != id) {
        selectedId_ = id;
        updateMetering();
        repaint();
    }
}

void RoutingSelector::meterInputsFrom(juce::AudioDeviceManager* devices) {
    inputDevices_ = devices;
    updateMetering();
}

void RoutingSelector::updateMetering() {
    const auto hasInputs =
        inputDevices_ != nullptr && std::ranges::any_of(options_, [](const RoutingOption& option) {
            return !option.inputChannels.empty();
        });
    if (!hasInputs)
        inputLevels_.reset();
    else if (inputLevels_ == nullptr)
        inputLevels_ = HardwareInputLevels::acquire(*inputDevices_);

    const auto selected = std::ranges::find(options_, selectedId_, &RoutingOption::id);
    const auto metered =
        inputLevels_ != nullptr && enabled_ && !readOnly_ && selected != options_.end();
    labelBars_.setChannels(metered ? selected->inputChannels : std::vector<int>{});

    if (!labelBars_.meters())
        stopTimer();
    else if (!isTimerRunning())
        startTimerHz(30);
}

void RoutingSelector::timerCallback() {
    if (isShowing() && labelBars_.follow(inputLevels_.get()))
        repaint(getLabelMeterArea());
}

juce::Rectangle<int> RoutingSelector::getLabelMeterArea() const {
    return getMainButtonArea()
        .reduced(0, 3)
        .removeFromRight(LABEL_METER_WIDTH + 3)
        .withTrimmedRight(3);
}

juce::String RoutingSelector::getSelectedName() const {
    const auto opt = std::ranges::find(options_, selectedId_, &RoutingOption::id);
    return opt != options_.end() ? opt->name : juce::String("None");
}

void RoutingSelector::setOptions(const std::vector<RoutingOption>& options) {
    options_ = options;
    // Auto-select first non-separator option if nothing selected
    if (selectedId_ < 0) {
        const auto opt = std::ranges::find_if(options_, std::not_fn(&RoutingOption::isSeparator));
        if (opt != options_.end())
            selectedId_ = opt->id;
    }
    updateMetering();
}

void RoutingSelector::addOption(RoutingOption option) {
    options_.push_back(std::move(option));
    updateMetering();
}

void RoutingSelector::clearOptions() {
    options_.clear();
    selectedId_ = -1;
    updateMetering();
}

int RoutingSelector::getFirstChannelOptionId() const {
    constexpr auto isChannelOption = [](const auto& o) { return !o.isSeparator && o.id >= 10; };
    const auto opt = std::ranges::find_if(options_, isChannelOption);
    return opt != options_.end() ? opt->id : -1;
}

juce::Rectangle<int> RoutingSelector::getMainButtonArea() const {
    auto bounds = getLocalBounds();
    return bounds.withTrimmedRight(DROPDOWN_ARROW_WIDTH);
}

juce::Rectangle<int> RoutingSelector::getDropdownArea() const {
    auto bounds = getLocalBounds();
    return bounds.removeFromRight(DROPDOWN_ARROW_WIDTH);
}

void RoutingSelector::showPopupMenu() {
    juce::PopupMenu menu;

    const auto metered = inputLevels_ != nullptr;

    // Add routing options
    if (options_.empty()) {
        menu.addItem(-1, "(No options available)", false);
    } else {
        for (const auto& opt : options_) {
            if (opt.isSeparator) {
                menu.addSeparator();
            } else if (metered && !opt.inputChannels.empty()) {
                juce::PopupMenu::Item item(opt.name);
                item.itemID = opt.id;
                item.isTicked = opt.id == selectedId_;
                item.customComponent =
                    new MeteredInputItem(opt.name, opt.inputChannels, item.isTicked,
                                         std::weak_ptr<const HardwareInputLevels>(inputLevels_));
                menu.addItem(std::move(item));
            } else {
                menu.addItem(opt.id, opt.name, true, opt.id == selectedId_);
            }
        }
    }

    juce::Component::SafePointer<RoutingSelector> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMinimumWidth(100),
                       [safeThis](int result) {
                           if (safeThis == nullptr || result == 0) {
                               return;  // Dismissed or component destroyed
                           }
                           if (result > 0) {
                               // Selection changed
                               safeThis->setSelectedId(result);
                               if (safeThis->onSelectionChanged) {
                                   safeThis->onSelectionChanged(result);
                               }
                           }
                       });
}

}  // namespace magda
