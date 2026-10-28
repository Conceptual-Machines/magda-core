// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MediaDbFilterWidgets.hpp"

#include "../../themes/ActiveTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

constexpr float kRadius = 5.0f;

juce::Font pillFont() {
    return FontManager::getInstance().getUIFont(12.0f);
}

void drawCaret(juce::Graphics& g, juce::Rectangle<float> area) {
    juce::Path p;
    const auto c = area.getCentre();
    p.addTriangle(c.x - 3.5f, c.y - 1.5f, c.x + 3.5f, c.y - 1.5f, c.x, c.y + 2.5f);
    g.fillPath(p);
}

/** @brief Field chrome shared by the pill, the bpm field and the tag box. */
void drawField(juce::Graphics& g, juce::Rectangle<float> r, bool active, bool hover) {
    const auto accent = ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY);
    g.setColour(
        active ? accent.withAlpha(0.12f)
               : ActiveTheme::getColour(hover ? ActiveTheme::SURFACE_HOVER : ActiveTheme::SURFACE));
    g.fillRoundedRectangle(r, kRadius);
    g.setColour(active ? accent.withAlpha(0.55f) : ActiveTheme::getBorderColour());
    g.drawRoundedRectangle(r.reduced(0.5f), kRadius, 1.0f);
}

void styleBareEditor(juce::TextEditor& e) {
    e.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    e.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    e.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    e.setColour(juce::TextEditor::textColourId, ActiveTheme::getTextColour());
    e.setColour(juce::TextEditor::highlightColourId,
                ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY).withAlpha(0.45f));
}

}  // namespace

// ---------------------------------------------------------------------------
// FilterPill
// ---------------------------------------------------------------------------

FilterPill::FilterPill(juce::String label, juce::StringArray options)
    : label_(std::move(label)), options_(std::move(options)) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void FilterPill::setSelectedIndex(int index, juce::NotificationType notification) {
    index = juce::jlimit(0, juce::jmax(0, options_.size() - 1), index);
    if (index == selected_)
        return;
    selected_ = index;
    repaint();
    if (notification != juce::dontSendNotification && onChange)
        onChange();
}

int FilterPill::getIdealWidth() const {
    const auto font = pillFont();
    float widest = 0.0f;
    for (const auto& o : options_)
        widest = juce::jmax(widest, juce::GlyphArrangement::getStringWidth(font, o));
    return juce::roundToInt(juce::GlyphArrangement::getStringWidth(font, label_) + widest) + 36;
}

void FilterPill::paint(juce::Graphics& g) {
    const auto r = getLocalBounds().toFloat();
    drawField(g, r, selected_ > 0, isMouseOver());

    auto text = getLocalBounds().reduced(10, 0);
    const auto caret = text.removeFromRight(10).toFloat();
    const auto font = pillFont();
    g.setFont(font);

    const auto labelWidth =
        juce::roundToInt(juce::GlyphArrangement::getStringWidth(font, label_ + " "));
    g.setColour(ActiveTheme::getSecondaryTextColour());
    g.drawText(label_, text.removeFromLeft(labelWidth), juce::Justification::centredLeft, false);
    g.setColour(selected_ > 0 ? ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY)
                              : ActiveTheme::getTextColour());
    g.drawText(options_[selected_], text, juce::Justification::centredLeft, true);

    g.setColour(ActiveTheme::getSecondaryTextColour());
    drawCaret(g, caret);
}

void FilterPill::mouseEnter(const juce::MouseEvent&) {
    repaint();
}

void FilterPill::mouseExit(const juce::MouseEvent&) {
    repaint();
}

void FilterPill::mouseUp(const juce::MouseEvent& e) {
    if (!isEnabled() || !getLocalBounds().contains(e.getPosition()))
        return;
    juce::PopupMenu menu;
    for (int i = 0; i < options_.size(); ++i)
        menu.addItem(i + 1, options_[i], true, i == selected_);
    const juce::Component::SafePointer<FilterPill> self(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [self](int result) {
        if (self != nullptr && result > 0)
            self->setSelectedIndex(result - 1, juce::sendNotification);
    });
}

// ---------------------------------------------------------------------------
// BpmRangeField
// ---------------------------------------------------------------------------

BpmRangeField::BpmRangeField() {
    for (auto* e : {&min_, &max_}) {
        e->setInputRestrictions(5, "0123456789.");
        e->setFont(FontManager::getInstance().getMonoFont(12.0f));
        e->setJustification(juce::Justification::centred);
        e->setIndents(0, 0);
        e->onReturnKey = [this]() {
            repaint();
            if (onChange)
                onChange();
        };
        e->onFocusLost = e->onReturnKey;
        addAndMakeVisible(*e);
    }
    applyThemeColours();
}

std::optional<double> BpmRangeField::getMin() const {
    if (min_.isEmpty())
        return std::nullopt;
    return min_.getText().getDoubleValue();
}

std::optional<double> BpmRangeField::getMax() const {
    if (max_.isEmpty())
        return std::nullopt;
    return max_.getText().getDoubleValue();
}

void BpmRangeField::applyThemeColours() {
    for (auto* e : {&min_, &max_}) {
        styleBareEditor(*e);
        e->setTextToShowWhenEmpty(e == &min_ ? "min" : "max",
                                  ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
    }
}

void BpmRangeField::paint(juce::Graphics& g) {
    drawField(g, getLocalBounds().toFloat(), !min_.isEmpty() || !max_.isEmpty(), false);
    g.setFont(pillFont());
    g.setColour(ActiveTheme::getSecondaryTextColour());
    g.drawText("bpm", getLocalBounds().reduced(10, 0), juce::Justification::centredLeft, false);
    g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
    g.drawText(juce::String::fromUTF8("\xe2\x80\x93"),
               juce::Rectangle<int>(min_.getRight(), 0, max_.getX() - min_.getRight(), getHeight()),
               juce::Justification::centred, false);
}

void BpmRangeField::resized() {
    auto r = getLocalBounds().reduced(4, 1);
    r.removeFromLeft(34);
    const int w = (r.getWidth() - 10) / 2;
    min_.setBounds(r.removeFromLeft(w));
    max_.setBounds(r.removeFromRight(w));
}

// ---------------------------------------------------------------------------
// TagChipInput
// ---------------------------------------------------------------------------

class TagChipInput::Input : public juce::TextEditor {
  public:
    explicit Input(TagChipInput& owner) : owner_(owner) {}

    bool keyPressed(const juce::KeyPress& key) override {
        return owner_.handleKey(key) || juce::TextEditor::keyPressed(key);
    }

  private:
    TagChipInput& owner_;
};

/** @brief The suggestion list, floated over the top-level window so it isn't clipped. */
class TagChipInput::Suggestions : public juce::Component {
  public:
    static constexpr int kRowHeight = 22;

    std::function<void(const juce::String&)> onPick;

    void setItems(juce::StringArray items) {
        items_ = std::move(items);
        highlighted_ = items_.isEmpty() ? -1 : 0;
        repaint();
    }
    const juce::StringArray& items() const {
        return items_;
    }
    juce::String highlightedItem() const {
        return items_[highlighted_];
    }
    void move(int delta) {
        if (items_.isEmpty())
            return;
        highlighted_ = (highlighted_ + delta + items_.size()) % items_.size();
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto r = getLocalBounds().toFloat();
        g.setColour(ActiveTheme::getColour(ActiveTheme::SURFACE));
        g.fillRoundedRectangle(r, kRadius);
        g.setColour(ActiveTheme::getBorderColour());
        g.drawRoundedRectangle(r.reduced(0.5f), kRadius, 1.0f);
        g.setFont(pillFont());
        for (int i = 0; i < items_.size(); ++i) {
            const auto row = juce::Rectangle<int>(0, 3 + i * kRowHeight, getWidth(), kRowHeight);
            if (i == highlighted_) {
                g.setColour(ActiveTheme::getColour(ActiveTheme::SURFACE_HOVER));
                g.fillRect(row.reduced(3, 0));
            }
            g.setColour(ActiveTheme::getTextColour());
            g.drawText(items_[i], row.reduced(10, 0), juce::Justification::centredLeft, true);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override {
        const int row = (e.y - 3) / kRowHeight;
        if (row != highlighted_ && juce::isPositiveAndBelow(row, items_.size())) {
            highlighted_ = row;
            repaint();
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        const int row = (e.y - 3) / kRowHeight;
        if (juce::isPositiveAndBelow(row, items_.size()) && onPick)
            onPick(items_[row]);
    }

  private:
    juce::StringArray items_;
    int highlighted_ = -1;
};

TagChipInput::TagChipInput()
    : input_(std::make_unique<Input>(*this)), suggestions_(std::make_unique<Suggestions>()) {
    input_->setFont(pillFont());
    input_->setIndents(4, 0);
    input_->setJustification(juce::Justification::centredLeft);
    input_->addListener(this);
    addAndMakeVisible(*input_);

    suggestions_->setWantsKeyboardFocus(false);
    suggestions_->setMouseClickGrabsKeyboardFocus(false);
    suggestions_->onPick = [this](const juce::String& tag) {
        input_->clear();
        addTag(tag);
        hideSuggestions();
    };
    applyThemeColours();
}

TagChipInput::~TagChipInput() {
    hideSuggestions();
    input_->removeListener(this);
}

void TagChipInput::setTags(const juce::StringArray& tags, juce::NotificationType notification) {
    if (tags == tags_)
        return;
    tags_ = tags;
    resized();
    repaint();
    if (notification != juce::dontSendNotification && onChange)
        onChange();
}

void TagChipInput::applyThemeColours() {
    styleBareEditor(*input_);
    input_->setTextToShowWhenEmpty(juce::String::fromUTF8("add tag\xe2\x80\xa6"),
                                   ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
}

void TagChipInput::paint(juce::Graphics& g) {
    drawField(g, getLocalBounds().toFloat(), false, false);

    const auto accent = ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY);
    g.setFont(pillFont());
    for (int i = 0; i < tags_.size() && i < chipBounds_.size(); ++i) {
        const auto chip = chipBounds_[i].toFloat();
        g.setColour(accent.withAlpha(0.18f));
        g.fillRoundedRectangle(chip, 3.0f);
        g.setColour(accent);
        g.drawText(tags_[i], chipBounds_[i].withTrimmedLeft(7).withTrimmedRight(16),
                   juce::Justification::centredLeft, true);

        const auto x = closeBounds_[i].toFloat().withSizeKeepingCentre(6.0f, 6.0f);
        g.setColour(accent.withAlpha(0.8f));
        g.drawLine(x.getX(), x.getY(), x.getRight(), x.getBottom(), 1.2f);
        g.drawLine(x.getRight(), x.getY(), x.getX(), x.getBottom(), 1.2f);
    }
}

void TagChipInput::resized() {
    chipBounds_.clearQuick();
    closeBounds_.clearQuick();
    auto r = getLocalBounds().reduced(5, 4);
    const auto font = pillFont();
    for (const auto& tag : tags_) {
        const int w = juce::roundToInt(juce::GlyphArrangement::getStringWidth(font, tag)) + 26;
        if (w > r.getWidth() - 60)
            break;
        const auto chip = r.removeFromLeft(w);
        chipBounds_.add(chip);
        closeBounds_.add(chip.withTrimmedLeft(chip.getWidth() - 16));
        r.removeFromLeft(4);
    }
    input_->setBounds(r);
}

void TagChipInput::mouseUp(const juce::MouseEvent& e) {
    for (int i = 0; i < closeBounds_.size(); ++i) {
        if (closeBounds_[i].contains(e.getPosition())) {
            removeTag(i);
            return;
        }
    }
    input_->grabKeyboardFocus();
}

void TagChipInput::addTag(const juce::String& tag) {
    const auto cleaned = tag.trim().toLowerCase();
    if (cleaned.isEmpty() || tags_.contains(cleaned))
        return;
    auto next = tags_;
    next.add(cleaned);
    setTags(next, juce::sendNotification);
}

void TagChipInput::removeTag(int index) {
    auto next = tags_;
    next.remove(index);
    setTags(next, juce::sendNotification);
}

void TagChipInput::commitText() {
    const auto text = input_->getText();
    input_->clear();
    hideSuggestions();
    addTag(text);
}

bool TagChipInput::handleKey(const juce::KeyPress& key) {
    const bool open = suggestions_->isShowing() && !suggestions_->items().isEmpty();
    if (open && (key == juce::KeyPress::downKey || key == juce::KeyPress::upKey)) {
        suggestions_->move(key == juce::KeyPress::downKey ? 1 : -1);
        return true;
    }
    if (open && key == juce::KeyPress::tabKey) {
        input_->setText(suggestions_->highlightedItem(), juce::dontSendNotification);
        commitText();
        return true;
    }
    if (key == juce::KeyPress::escapeKey && open) {
        hideSuggestions();
        return true;
    }
    if (key.getTextCharacter() == ',' || key.getTextCharacter() == ' ') {
        commitText();
        return true;
    }
    if (key == juce::KeyPress::backspaceKey && input_->isEmpty() && !tags_.isEmpty()) {
        removeTag(tags_.size() - 1);
        return true;
    }
    return false;
}

void TagChipInput::textEditorTextChanged(juce::TextEditor&) {
    refreshSuggestions();
}

void TagChipInput::textEditorReturnKeyPressed(juce::TextEditor&) {
    const bool open = suggestions_->isShowing() && !suggestions_->items().isEmpty();
    if (open && suggestions_->highlightedItem().startsWithIgnoreCase(input_->getText().trim()))
        input_->setText(suggestions_->highlightedItem(), juce::dontSendNotification);
    commitText();
}

void TagChipInput::textEditorFocusLost(juce::TextEditor&) {
    hideSuggestions();
}

void TagChipInput::refreshSuggestions() {
    const auto prefix = input_->getText().trim().toLowerCase();
    juce::StringArray items;
    if (prefix.isNotEmpty() && suggest) {
        for (const auto& s : suggest(prefix))
            if (!tags_.contains(s))
                items.add(s);
    }
    if (items.isEmpty()) {
        hideSuggestions();
        return;
    }

    auto* top = getTopLevelComponent();
    if (top == nullptr)
        return;
    suggestions_->setItems(items);
    if (suggestions_->getParentComponent() != top)
        top->addChildComponent(*suggestions_);
    const auto anchor = top->getLocalArea(this, input_->getBounds());
    suggestions_->setBounds(anchor.getX(), anchor.getBottom() + 6,
                            juce::jmax(160, anchor.getWidth() / 2),
                            items.size() * Suggestions::kRowHeight + 6);
    suggestions_->setVisible(true);
    suggestions_->toFront(false);
}

void TagChipInput::hideSuggestions() {
    if (auto* parent = suggestions_->getParentComponent())
        parent->removeChildComponent(suggestions_.get());
    suggestions_->setVisible(false);
}

}  // namespace magda::daw::ui
