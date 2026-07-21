#include "custom_ui/FaustInstrumentTabbedUI.hpp"

#include <cmath>

#include "audio/plugins/FaustParamInfo.hpp"
#include "audio/plugins/FaustParamPool.hpp"
#include "audio/plugins/FaustParamSlot.hpp"
#include "audio/plugins/IFaustEditorModel.hpp"
#include "custom_ui/FaustUI.hpp"
#include "ui/components/chain/params/ParamWidgetSetup.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {
constexpr int kTabBarDepth = 20;
constexpr int kRowLabelHeight = 13;
constexpr int kCellPadding = 4;
}  // namespace

FaustInstrumentTabbedUI::FaustInstrumentTabbedUI() {
    header_ = std::make_unique<FaustUI>();
    addAndMakeVisible(*header_);

    tabs_ = std::make_unique<LayoutStableTabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
    tabs_->setTabBarDepth(kTabBarDepth);
    addAndMakeVisible(*tabs_);
}

FaustInstrumentTabbedUI::~FaustInstrumentTabbedUI() {
    releaseMomentaryButtons();
    // tabs_ owns the page Components by raw pointer (deleteWhenRemoved=false);
    // clear the tab bar before pages_ is destroyed so it doesn't dangle.
    if (tabs_)
        tabs_->clearTabs();
}

void FaustInstrumentTabbedUI::setPlugin(magda::daw::audio::IFaustEditorModel* model) {
    model_ = model;
    if (header_)
        header_->setPlugin(model);
    lastSignature_ = {};  // force a rebuild on the next refresh
    rebuildTabs();
}

void FaustInstrumentTabbedUI::setDevicePath(const magda::ChainNodePath& path) {
    devicePath_ = path;
    if (header_)
        header_->setDevicePath(path);
}

juce::String FaustInstrumentTabbedUI::poolSignature() const {
    if (model_ == nullptr)
        return {};
    juce::String sig;
    const auto& pool = model_->getPool();
    for (int i = 0; i < magda::daw::audio::FaustParamPool::kSize; ++i) {
        const auto& slot = pool.slot(i);
        if (!slot.active || slot.hidden)
            continue;
        sig << i << ':' << slot.group << ':' << slot.label << ':' << static_cast<int>(slot.kind)
            << '|';
    }
    return sig;
}

void FaustInstrumentTabbedUI::rebuildTabs() {
    releaseMomentaryButtons();
    if (tabs_)
        tabs_->clearTabs();
    pages_.clear();
    lastSignature_ = poolSignature();

    if (model_ == nullptr)
        return;

    const auto& pool = model_->getPool();
    const auto tabBg = DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f);

    // Distinct groups in first-appearance order; empty group → "Params".
    std::vector<juce::String> order;
    std::vector<GroupPage*> pageFor;  // parallel to `order`

    auto pageForGroup = [&](const juce::String& rawGroup) -> GroupPage* {
        const juce::String name = rawGroup.isEmpty() ? juce::String("Params") : rawGroup;
        for (size_t i = 0; i < order.size(); ++i)
            if (order[i] == name)
                return pageFor[i];
        auto page = std::make_unique<GroupPage>();
        auto* raw = page.get();
        pages_.push_back(std::move(page));
        order.push_back(name);
        pageFor.push_back(raw);
        return raw;
    };

    for (int i = 0; i < magda::daw::audio::FaustParamPool::kSize; ++i) {
        const auto& slot = pool.slot(i);
        if (!slot.active || slot.hidden)
            continue;

        auto* page = pageForGroup(slot.group);

        Row row;
        row.slotIndex = slot.index;

        row.label = std::make_unique<juce::Label>();
        row.label->setText(slot.label, juce::dontSendNotification);
        row.label->setFont(FontManager::getInstance().getUIFont(10.0f));
        row.label->setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
        row.label->setJustificationType(juce::Justification::centredLeft);
        page->addAndMakeVisible(*row.label);

        const int idx = slot.index;
        auto info = magda::daw::audio::paramInfoFromSlot(slot);
        if (info.momentary) {
            row.momentaryButton = std::make_unique<MomentaryParamButton>();
            configureMomentaryButton(*row.momentaryButton, [this, idx](double value) {
                if (onParameterChanged)
                    onParameterChanged(idx, static_cast<float>(value));
            });
            page->addAndMakeVisible(*row.momentaryButton);
        } else {
            row.slider = std::make_unique<LinkableTextSlider>();
            row.slider->setParameterInfo(info);
            row.slider->setParamIndex(slot.index);
            row.slider->setValue(slot.defaultValue, juce::dontSendNotification);
            row.slider->onValueChanged = [this, idx](double value) {
                if (onParameterChanged)
                    onParameterChanged(idx, static_cast<float>(value));
            };
            page->addAndMakeVisible(*row.slider);
        }

        page->rows.push_back(std::move(row));
    }

    if (pages_.empty()) {
        // No active params yet — show an empty "Params" tab so the strip exists.
        pageForGroup({});
    }

    for (size_t i = 0; i < order.size(); ++i)
        tabs_->addTab(order[i], tabBg, pageFor[i], false);

    resized();
}

void FaustInstrumentTabbedUI::updateFromParameters(
    const std::vector<magda::ParameterInfo>& params) {
    // Re-tab only when the pool layout actually changed (avoids destroying
    // sliders — and any in-flight link drag — on every value refresh).
    if (poolSignature() != lastSignature_)
        rebuildTabs();

    for (auto& page : pages_) {
        for (auto& row : page->rows) {
            for (const auto& info : params) {
                if (info.paramIndex != row.slotIndex)
                    continue;
                if (row.slider) {
                    row.slider->setParameterInfo(info);
                    row.slider->setValue(info.currentValue, juce::dontSendNotification);
                }
                break;
            }
        }
    }
}

std::vector<LinkableTextSlider*> FaustInstrumentTabbedUI::getLinkableSliders() {
    std::vector<LinkableTextSlider*> sliders;
    for (auto& page : pages_)
        for (auto& row : page->rows)
            if (row.slider)
                sliders.push_back(row.slider.get());
    return sliders;
}

void FaustInstrumentTabbedUI::releaseMomentaryButtons() {
    for (auto& page : pages_)
        for (auto& row : page->rows)
            if (row.momentaryButton)
                row.momentaryButton->release();
}

int FaustInstrumentTabbedUI::getCurrentTabIndex() const {
    return tabs_ ? tabs_->getCurrentTabIndex() : 0;
}

void FaustInstrumentTabbedUI::setCurrentTabIndex(int index) {
    if (tabs_ && index >= 0 && index < tabs_->getNumTabs())
        tabs_->setCurrentTabIndex(index, false);
}

void FaustInstrumentTabbedUI::paint(juce::Graphics& g) {
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    g.fillRect(getLocalBounds().reduced(1));
}

void FaustInstrumentTabbedUI::resized() {
    auto area = getLocalBounds().reduced(1);
    if (header_)
        header_->setBounds(area.removeFromTop(FaustUI::kHeaderHeight));
    if (tabs_)
        tabs_->setBoundsStable(area);
}

void FaustInstrumentTabbedUI::lookAndFeelChanged() {
    // Tab backgrounds, row labels, and momentary buttons capture concrete
    // palette colours in rebuildTabs; re-apply them in place so a live theme
    // switch restyles the open device UI without resetting slider values.
    const auto tabBg = DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f);
    if (tabs_)
        for (int i = 0; i < tabs_->getNumTabs(); ++i)
            tabs_->setTabBackgroundColour(i, tabBg);

    for (auto& page : pages_) {
        for (auto& row : page->rows) {
            if (row.label)
                row.label->setColour(juce::Label::textColourId,
                                     DarkTheme::getSecondaryTextColour());
            if (row.momentaryButton) {
                row.momentaryButton->setColour(juce::TextButton::buttonColourId,
                                               DarkTheme::getColour(DarkTheme::SURFACE));
                row.momentaryButton->setColour(juce::TextButton::textColourOffId,
                                               DarkTheme::getTextColour());
            }
        }
    }
    repaint();
}

void FaustInstrumentTabbedUI::GroupPage::resized() {
    const int n = static_cast<int>(rows.size());
    if (n == 0)
        return;

    auto area = getLocalBounds().reduced(kCellPadding);
    const int cols = area.getWidth() >= 360 ? 2 : 1;
    const int numRows = (n + cols - 1) / cols;
    const int cellW = area.getWidth() / cols;
    const int cellH = std::max(28, area.getHeight() / std::max(1, numRows));

    for (int i = 0; i < n; ++i) {
        const int col = i % cols;
        const int rowIdx = i / cols;
        juce::Rectangle<int> cell(area.getX() + col * cellW, area.getY() + rowIdx * cellH, cellW,
                                  cellH);
        cell = cell.reduced(kCellPadding, 2);
        auto& row = rows[static_cast<size_t>(i)];
        row.label->setBounds(cell.removeFromTop(kRowLabelHeight));
        if (row.slider)
            row.slider->setBounds(cell);
        else if (row.momentaryButton)
            row.momentaryButton->setBounds(cell.reduced(2));
    }
}

}  // namespace magda::daw::ui
