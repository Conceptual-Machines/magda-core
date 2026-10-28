// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ClipLabelsSection.hpp"

#include <algorithm>

#include "../../../../../themes/ActiveTheme.hpp"
#include "../../../../../themes/FontManager.hpp"
#include "../ClipInspectorStyle.hpp"
#include "core/ClipPropertyCommands.hpp"
#include "core/UndoManager.hpp"
#include "project/ProjectManager.hpp"

namespace magda::daw::ui {

namespace {
constexpr int SECTION_H = CollapsibleSectionHeader::kHeight;
constexpr int ROW_H = clip_style::kFieldHeight;
constexpr int GAP = 2;
constexpr int CHIP_W = 14;
constexpr int RANGE_W = 92;
constexpr int DELETE_W = 22;

juce::String rangeText(const magda::LabelRegion& label) {
    if (label.isPoint())
        return juce::String(label.sourceStart, 3) + " s";
    return juce::String(label.sourceStart, 2) + juce::String(" - ") +
           juce::String(label.sourceEnd, 2) + " s";
}
}  // namespace

/// One label: colour chip (click for the palette), editable name, range, delete.
class ClipLabelsSection::Row : public juce::Component {
  public:
    Row(ClipLabelsSection& owner, int index, const magda::LabelRegion& label)
        : owner_(owner), index_(index), colour_(label.colour) {
        name_.setText(juce::String(label.name), juce::dontSendNotification);
        name_.setFont(FontManager::getInstance().getUIFont(11.0f));
        name_.setColour(juce::Label::textColourId, ActiveTheme::getTextColour());
        name_.setEditable(false, true, false);
        name_.onTextChange = [this]() {
            const auto text = name_.getText().trim();
            if (text.isEmpty())
                return;
            owner_.edit(
                index_, [text](magda::LabelRegion& l) { l.name = text.toStdString(); },
                "Rename Label");
        };
        addAndMakeVisible(name_);

        range_.setText(rangeText(label), juce::dontSendNotification);
        range_.setFont(FontManager::getInstance().getMonoFont(10.0f));
        range_.setColour(juce::Label::textColourId, ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
        range_.setJustificationType(juce::Justification::centredRight);
        range_.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(range_);

        delete_.setButtonText(juce::String::charToString(0x00D7));
        delete_.setTooltip("Delete label");
        clip_style::action(delete_);
        delete_.onClick = [this]() { owner_.remove(index_); };
        addAndMakeVisible(delete_);
    }

    ~Row() override {
        delete_.setLookAndFeel(nullptr);
    }

    bool isEditing() const {
        return name_.isBeingEdited();
    }

    void paint(juce::Graphics& g) override {
        g.setColour(colour_);
        g.fillRoundedRectangle(chipBounds().toFloat(), 2.0f);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(0, 1);
        area.removeFromLeft(CHIP_W + 6);
        delete_.setBounds(area.removeFromRight(DELETE_W));
        area.removeFromRight(4);
        range_.setBounds(area.removeFromRight(RANGE_W));
        name_.setBounds(area);
    }

    void mouseUp(const juce::MouseEvent& event) override {
        if (chipBounds().expanded(3).contains(event.getPosition()))
            showColourMenu();
    }

  private:
    ClipLabelsSection& owner_;
    int index_;
    juce::Colour colour_;
    juce::Label name_, range_;
    juce::TextButton delete_;

    juce::Rectangle<int> chipBounds() const {
        return getLocalBounds().removeFromLeft(CHIP_W).withSizeKeepingCentre(CHIP_W, CHIP_W);
    }

    void showColourMenu() {
        const auto palette =
            magda::ProjectManager::getInstance().getCurrentProjectInfo().defaults.colourPalette;
        juce::PopupMenu menu;
        for (size_t i = 0; i < palette.size(); ++i)
            menu.addColouredItem(static_cast<int>(i) + 1, palette[i].name,
                                 juce::Colour(palette[i].colour));
        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(this),
            [safeThis = juce::Component::SafePointer<Row>(this), palette](int r) {
                if (safeThis == nullptr || r <= 0 || static_cast<size_t>(r) > palette.size())
                    return;
                const auto colour = juce::Colour(palette[static_cast<size_t>(r - 1)].colour);
                safeThis->owner_.edit(
                    safeThis->index_, [colour](magda::LabelRegion& l) { l.colour = colour; },
                    "Recolour Label");
            });
    }
};

ClipLabelsSection::ClipLabelsSection() {
    header_.onCollapsedChanged = [this]() {
        update();
        if (onCollapsedChanged)
            onCollapsedChanged();
    };
    addAndMakeVisible(header_);

    clip_style::caption(emptyHint_, "Label tool: click or drag in the waveform");
    emptyHint_.setColour(juce::Label::textColourId, ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
    addChildComponent(emptyHint_);
}

ClipLabelsSection::~ClipLabelsSection() = default;

const std::vector<magda::LabelRegion>* ClipLabelsSection::labels() const {
    const auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
    const auto* event = clip && clip->isAudio() ? clip->primaryEvent() : nullptr;
    return event ? &event->labelRegions : nullptr;
}

void ClipLabelsSection::setSelectedClips(const std::unordered_set<magda::ClipId>& clipIds) {
    const auto id = clipIds.size() == 1 ? *clipIds.begin() : magda::INVALID_CLIP_ID;
    if (id != clipId_) {
        clipId_ = id;
        shown_.clear();
        rows_.clear();
    }
    update();
}

bool ClipLabelsSection::isEditing() const {
    return std::ranges::any_of(rows_, [](const auto& row) { return row->isEditing(); });
}

void ClipLabelsSection::update() {
    const auto* current = labels();
    const bool open = current != nullptr && !header_.isCollapsed();
    header_.setCaption(current && !current->empty() ? juce::String(current->size()) : "");
    emptyHint_.setVisible(open && current->empty());

    if (!open) {
        rows_.clear();
        shown_.clear();
        return;
    }
    if (*current == shown_ && rows_.size() == current->size())
        return;

    shown_ = *current;
    rows_.clear();
    for (size_t i = 0; i < shown_.size(); ++i) {
        auto row = std::make_unique<Row>(*this, static_cast<int>(i), shown_[i]);
        addAndMakeVisible(*row);
        rows_.push_back(std::move(row));
    }
    resized();
}

void ClipLabelsSection::edit(int index, const std::function<void(magda::LabelRegion&)>& change,
                             const juce::String& description) {
    const auto* current = labels();
    if (current == nullptr || index < 0 || index >= static_cast<int>(current->size()))
        return;
    auto edited = *current;
    change(edited[static_cast<size_t>(index)]);
    // Deferred: the call can come from a row that the resulting refresh deletes.
    juce::MessageManager::callAsync([clipId = clipId_, edited = std::move(edited), description]() {
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetClipLabelRegionsCommand>(clipId, edited, description));
    });
}

void ClipLabelsSection::remove(int index) {
    const auto* current = labels();
    if (current == nullptr || index < 0 || index >= static_cast<int>(current->size()))
        return;
    auto edited = *current;
    edited.erase(edited.begin() + index);
    juce::MessageManager::callAsync([clipId = clipId_, edited = std::move(edited)]() {
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetClipLabelRegionsCommand>(clipId, edited, "Delete Label"));
    });
}

int ClipLabelsSection::getPreferredHeight() const {
    if (!isVisible() || labels() == nullptr)
        return 0;
    if (header_.isCollapsed())
        return SECTION_H;
    if (rows_.empty())
        return SECTION_H + clip_style::kCaptionHeight + GAP;
    return SECTION_H + static_cast<int>(rows_.size()) * (ROW_H + GAP);
}

void ClipLabelsSection::resized() {
    auto area = getLocalBounds();
    header_.setBounds(area.removeFromTop(SECTION_H));
    if (header_.isCollapsed())
        return;
    emptyHint_.setBounds(area.removeFromTop(clip_style::kCaptionHeight));
    area = getLocalBounds().withTrimmedTop(SECTION_H);
    for (auto& row : rows_) {
        row->setBounds(area.removeFromTop(ROW_H));
        area.removeFromTop(GAP);
    }
}

}  // namespace magda::daw::ui
