#include "MagdaAlertWindow.hpp"

#include <algorithm>

#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda {

namespace {

constexpr int kMinWidth = 440;
constexpr int kPad = 20;
constexpr int kChip = 36;
constexpr int kChipGap = 14;
constexpr int kButtonHeight = 34;  // A 30px body and a 2px margin for the focus ring.
constexpr int kFooterHeight = kButtonHeight + 20;
const juce::Identifier kRoleProperty{"magdaAlertRole"};

juce::Font titleFont() {
    return FontManager::getInstance().getUIFontMedium(15.0f);
}
juce::Font messageFont() {
    return FontManager::getInstance().getUIFont(13.5f);
}
juce::Font buttonFont() {
    return FontManager::getInstance().getUIFont(13.5f);
}
juce::Font hintFont() {
    return FontManager::getInstance().getMonoFont(10.5f);
}

bool isAccessibleMessageLabel(juce::Component* c) {
    bool clicks = false, childClicks = false;
    c->getInterceptsMouseClicks(clicks, childClicks);
    return dynamic_cast<juce::Label*>(c) != nullptr && !clicks;
}

bool isAlertButton(juce::Component* c) {
    return dynamic_cast<juce::TextButton*>(c) != nullptr && c->getExplicitFocusOrder() == 1;
}

MagdaAlertWindow::ButtonRole roleForLabel(const juce::String& label) {
    const auto text =
        label.trim().toLowerCase().replace(juce::CharPointer_UTF8("\xe2\x80\x99"), "'");
    if (text.startsWith("don't save") || text.startsWith("dont save") || text.startsWith("discard"))
        return MagdaAlertWindow::ButtonRole::Discard;
    for (const char* verb : {"delete", "remove", "wipe", "overwrite", "erase", "reset"})
        if (text.startsWith(verb))
            return MagdaAlertWindow::ButtonRole::Destructive;
    return MagdaAlertWindow::ButtonRole::Secondary;
}

juce::KeyPress commandKey(int key) {
    return juce::KeyPress(key, juce::ModifierKeys::commandModifier, 0);
}

int preferredButtonWidth(juce::Button& button) {
    int width = juce::GlyphArrangement::getStringWidthInt(buttonFont(), button.getButtonText());
    if (const auto hint = MagdaAlertWindow::keyHintFor(button); hint.isNotEmpty())
        width += 7 + juce::GlyphArrangement::getStringWidthInt(hintFont(), hint);
    return width + 28 + 4;
}

class AlertControlsLookAndFeel : public juce::LookAndFeel_V4 {
  public:
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                              bool over, bool down) override {
        const auto role = MagdaAlertWindow::roleOf(button);
        const auto body = button.getLocalBounds().toFloat().reduced(2.0f);
        auto fill = ActiveTheme::getColour(ActiveTheme::DEVICE_HEAD);
        auto border = ActiveTheme::getColour(ActiveTheme::DEVICE_LINE);
        if (role == MagdaAlertWindow::ButtonRole::Primary ||
            role == MagdaAlertWindow::ButtonRole::Destructive) {
            const auto tone = ActiveTheme::getColour(role == MagdaAlertWindow::ButtonRole::Primary
                                                         ? ActiveTheme::DEVICE_BLUE
                                                         : ActiveTheme::DEVICE_RED);
            fill = ActiveTheme::getColour(ActiveTheme::DEVICE_BG).interpolatedWith(tone, 0.22f);
            border = tone.withAlpha(0.45f);
        }
        if (down)
            fill = fill.darker(0.15f);
        else if (over)
            fill = fill.brighter(0.08f);
        g.setColour(fill);
        g.fillRoundedRectangle(body, 6.0f);
        g.setColour(border);
        g.drawRoundedRectangle(body.reduced(0.5f), 6.0f, 1.0f);
        if (role == MagdaAlertWindow::ButtonRole::Primary) {
            g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE).withAlpha(0.7f));
            g.drawRoundedRectangle(button.getLocalBounds().toFloat().reduced(0.75f), 7.5f, 1.5f);
        }
    }

    void fillTextEditorBackground(juce::Graphics& g, int width, int height,
                                  juce::TextEditor& editor) override {
        g.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle(juce::Rectangle<int>(width, height).toFloat(), 6.0f);
    }

    void drawTextEditorOutline(juce::Graphics& g, int width, int height,
                               juce::TextEditor& editor) override {
        const bool focused = editor.hasKeyboardFocus(true) && !editor.isReadOnly();
        g.setColour(editor.findColour(focused ? juce::TextEditor::focusedOutlineColourId
                                              : juce::TextEditor::outlineColourId));
        g.drawRoundedRectangle(juce::Rectangle<int>(width, height).toFloat().reduced(0.5f), 6.0f,
                               focused ? 1.5f : 1.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) override {
        const auto role = MagdaAlertWindow::roleOf(button);
        const auto hint = MagdaAlertWindow::keyHintFor(button);
        const auto& label = button.getButtonText();
        const int labelWidth = juce::GlyphArrangement::getStringWidthInt(buttonFont(), label);
        const int hintWidth =
            hint.isEmpty() ? 0 : 7 + juce::GlyphArrangement::getStringWidthInt(hintFont(), hint);
        auto area = button.getLocalBounds();
        area = area.withSizeKeepingCentre(labelWidth + hintWidth, area.getHeight());

        g.setFont(buttonFont());
        g.setColour(role == MagdaAlertWindow::ButtonRole::Discard
                        ? ActiveTheme::getColour(ActiveTheme::DEVICE_RED)
                        : ActiveTheme::getColour(ActiveTheme::DEVICE_TITLE));
        g.drawText(label, area.removeFromLeft(labelWidth), juce::Justification::centred, false);
        if (hint.isNotEmpty()) {
            g.setFont(hintFont());
            g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM).withAlpha(0.8f));
            g.drawText(hint, area.withTrimmedLeft(7), juce::Justification::centredLeft, false);
        }
    }
};

class Backdrop : public juce::Component {
  public:
    Backdrop() {
        setInterceptsMouseClicks(true, false);
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colours::black.withAlpha(0.55f));
    }
};

}  // namespace

MagdaAlertWindow::MagdaAlertWindow(const juce::String& title, const juce::String& message,
                                   juce::MessageBoxIconType iconType,
                                   juce::Component* associatedComponent)
    : juce::AlertWindow(title, message, iconType, associatedComponent),
      iconType_(iconType),
      message_(message),
      controlsLookAndFeel_(std::make_unique<AlertControlsLookAndFeel>()) {
    setOpaque(false);
    // AlertWindow draws text-field labels in this colour.
    setColour(textColourId, ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
    if (associatedComponent != nullptr)
        backdropHost_ = associatedComponent->getTopLevelComponent();
    else
        backdropHost_ = juce::TopLevelWindow::getActiveTopLevelWindow();
    layoutAlert();
}

MagdaAlertWindow::~MagdaAlertWindow() {
    showBackdrop(false);
    for (auto* child : getChildren())
        if (&child->getLookAndFeel() == controlsLookAndFeel_.get())
            child->setLookAndFeel(nullptr);
}

MagdaAlertWindow::ButtonRole MagdaAlertWindow::roleOf(const juce::Button& button) {
    return static_cast<ButtonRole>(static_cast<int>(button.getProperties()[kRoleProperty]));
}

juce::String MagdaAlertWindow::keyHintFor(const juce::Button& button) {
    // Cancel keeps "esc" when Return moved onto it; Escape always closes the alert anyway.
    if (button.isRegisteredForShortcut(juce::KeyPress(juce::KeyPress::escapeKey)))
        return "esc";
    if (button.isRegisteredForShortcut(juce::KeyPress(juce::KeyPress::returnKey)))
        return juce::String(juce::CharPointer_UTF8("\xe2\x86\xb5"));
#if JUCE_MAC
    const juce::String command(juce::CharPointer_UTF8("\xe2\x8c\x98"));
    const juce::String backspace(juce::CharPointer_UTF8("\xe2\x8c\xab"));
#else
    const juce::String command("Ctrl+");
    const juce::String backspace("Bksp");
#endif
    if (button.isRegisteredForShortcut(commandKey('d')))
        return command + "D";
    if (button.isRegisteredForShortcut(commandKey(juce::KeyPress::backspaceKey)))
        return command + backspace;
    return {};
}

void MagdaAlertWindow::assignRoles(const juce::Array<juce::Button*>& buttons) {
    const juce::KeyPress returnKey(juce::KeyPress::returnKey);
    const juce::KeyPress escapeKey(juce::KeyPress::escapeKey);
    juce::Button* cancel = nullptr;
    juce::Button* withReturn = nullptr;
    for (auto* button : buttons) {
        if (button->isRegisteredForShortcut(escapeKey) && cancel == nullptr)
            cancel = button;
        if (button->isRegisteredForShortcut(returnKey) && withReturn == nullptr)
            withReturn = button;
    }

    for (auto* button : buttons) {
        const auto role = roleForLabel(button->getButtonText());
        button->getProperties().set(kRoleProperty, static_cast<int>(role));
        if (role == ButtonRole::Discard) {
            button->addShortcut(commandKey('d'));
        } else if (role == ButtonRole::Destructive) {
            // A stray Return must not destroy: it moves to the safe button.
            if (button == withReturn && cancel != nullptr && cancel != button) {
                button->clearShortcuts();
                cancel->addShortcut(returnKey);
                withReturn = cancel;
            }
            button->addShortcut(commandKey(juce::KeyPress::backspaceKey));
        }
    }

    if (withReturn == nullptr) {
        for (auto* button : buttons)
            if (button != cancel && roleOf(*button) == ButtonRole::Secondary) {
                withReturn = button;
                break;
            }
        if (withReturn != nullptr)
            withReturn->addShortcut(returnKey);
        else
            withReturn = cancel;  // Styled as primary; Return must not cancel a running task.
    }
    if (withReturn != nullptr && roleOf(*withReturn) == ButtonRole::Secondary)
        withReturn->getProperties().set(kRoleProperty, static_cast<int>(ButtonRole::Primary));
}

MagdaAlertWindow::FooterOrder MagdaAlertWindow::footerOrder(
    const juce::Array<juce::Button*>& buttons) {
    FooterOrder order;
    for (auto* b : buttons) {
        if (roleOf(*b) == ButtonRole::Discard)
            order.left.add(b);
        else if (roleOf(*b) == ButtonRole::Secondary)
            order.right.add(b);
    }
    for (auto role : {ButtonRole::Primary, ButtonRole::Destructive})
        for (auto* b : buttons)
            if (roleOf(*b) == role)
                order.right.add(b);
    return order;
}

juce::Array<juce::Button*> MagdaAlertWindow::alertButtons() const {
    juce::Array<juce::Button*> buttons;
    for (auto* child : getChildren())
        if (isAlertButton(child))
            buttons.insert(0,
                           static_cast<juce::Button*>(child));  // Added at the back of the z-order.
    return buttons;
}

void MagdaAlertWindow::assignButtonRoles() {
    const auto buttons = alertButtons();
    assignRoles(buttons);
    for (auto* button : buttons)
        if (&button->getLookAndFeel() != controlsLookAndFeel_.get())
            button->setLookAndFeel(controlsLookAndFeel_.get());
}

MagdaAlertWindow::Tone MagdaAlertWindow::tone() const {
    for (auto* child : getChildren())
        if (isAlertButton(child) &&
            roleOf(*static_cast<juce::Button*>(child)) == ButtonRole::Destructive)
            return Tone::Danger;
    switch (iconType_) {
        case juce::MessageBoxIconType::QuestionIcon:
            return Tone::Question;
        case juce::MessageBoxIconType::InfoIcon:
            return Tone::Info;
        case juce::MessageBoxIconType::WarningIcon:
            return Tone::Warning;
        case juce::MessageBoxIconType::NoIcon:
            break;
    }
    return Tone::None;
}

void MagdaAlertWindow::layoutAlert() {
    if (laying_)
        return;
    const juce::ScopedValueSetter<bool> guard(laying_, true);
    assignButtonRoles();

    juce::Array<juce::Component*> content;
    juce::Array<juce::Button*> buttons;
    for (auto* child : getChildren()) {
        if (isAlertButton(child))
            buttons.insert(0, static_cast<juce::Button*>(child));
        else if (!isAccessibleMessageLabel(child))
            content.add(child);
    }

    const bool hasChip = tone() != Tone::None;
    const int textX = hasChip ? kPad + kChip + kChipGap : kPad;
    int width = kMinWidth;
    for (auto* c : content)
        if (dynamic_cast<juce::TextEditor*>(c) == nullptr &&
            dynamic_cast<juce::ComboBox*>(c) == nullptr)
            width = std::max(width, textX + c->getWidth() + kPad);
    int buttonsWidth = 2 * 16;
    for (auto* b : buttons)
        buttonsWidth += preferredButtonWidth(*b) + 6;
    width = std::max(width, buttonsWidth + 24);
    const int textWidth = width - textX - kPad;

    juce::AttributedString title;
    title.append(getName(), titleFont(), ActiveTheme::getColour(ActiveTheme::DEVICE_TITLE));
    titleLayout_.createLayout(title, static_cast<float>(textWidth));
    juce::AttributedString message;
    message.setLineSpacing(3.0f);
    message.append(message_.trim(), messageFont(), ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
    messageLayout_.createLayout(message, static_cast<float>(textWidth));

    int y = kPad;
    const int titleHeight = static_cast<int>(std::ceil(titleLayout_.getHeight()));
    // A title alone centres on the chip; with a message it lines up with the chip's top.
    const bool hasMessage = message_.trim().isNotEmpty();
    const int titleOffset = !hasChip ? 0 : hasMessage ? 1 : std::max(0, (kChip - titleHeight) / 2);
    titleArea_ = {textX, y + titleOffset, textWidth, titleHeight};
    y = titleArea_.getBottom();
    messageArea_ = {};
    if (hasMessage) {
        y += 5;
        messageArea_ = {textX, y, textWidth,
                        static_cast<int>(std::ceil(messageLayout_.getHeight()))};
        y = messageArea_.getBottom();
    }
    for (auto* c : content) {
        y += 12;
        if (dynamic_cast<juce::ComboBox*>(c) != nullptr ||
            (dynamic_cast<juce::TextEditor*>(c) != nullptr &&
             !static_cast<juce::TextEditor*>(c)->isReadOnly())) {
            y += 14;  // AlertWindow paints the field's label above it.
            c->setBounds(textX, y, textWidth, 28);
            if (auto* editor = dynamic_cast<juce::TextEditor*>(c)) {
                editor->setColour(juce::TextEditor::backgroundColourId,
                                  ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD));
                editor->setColour(juce::TextEditor::outlineColourId,
                                  ActiveTheme::getColour(ActiveTheme::DEVICE_LINE));
                editor->setColour(juce::TextEditor::focusedOutlineColourId,
                                  ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE).withAlpha(0.7f));
                editor->setColour(juce::TextEditor::textColourId,
                                  ActiveTheme::getColour(ActiveTheme::DEVICE_TITLE));
                editor->setIndents(8, 6);
                if (&editor->getLookAndFeel() != controlsLookAndFeel_.get())
                    editor->setLookAndFeel(controlsLookAndFeel_.get());
            }
        } else if (dynamic_cast<juce::ProgressBar*>(c) != nullptr) {
            c->setBounds(textX, y, textWidth, 20);
        } else {
            c->setBounds(textX, y, std::min(textWidth, c->getWidth()), c->getHeight());
        }
        y = c->getBottom();
    }
    chipArea_ = hasChip ? juce::Rectangle<int>(kPad, kPad, kChip, kChip) : juce::Rectangle<int>();
    const int bodyBottom = std::max(y, chipArea_.getBottom()) + kPad;
    footerArea_ = buttons.isEmpty() ? juce::Rectangle<int>()
                                    : juce::Rectangle<int>(0, bodyBottom, width, kFooterHeight);
    const int height = bodyBottom + footerArea_.getHeight();

    for (auto* child : getChildren())
        if (isAccessibleMessageLabel(child))
            child->setBounds(messageArea_.isEmpty() ? titleArea_ : messageArea_);

    // Discard sits far left; the rest right-aligned, primary then destructive last.
    auto row = footerArea_.reduced(14, (kFooterHeight - kButtonHeight) / 2);
    const auto order = footerOrder(buttons);
    for (auto* b : order.left)
        b->setBounds(row.removeFromLeft(preferredButtonWidth(*b)));
    const auto& right = order.right;
    for (int i = right.size(); --i >= 0;) {
        right[i]->setBounds(row.removeFromRight(preferredButtonWidth(*right[i])));
        row.removeFromRight(4);
    }

    if (getWidth() != width || getHeight() != height) {
        if (isVisible())
            setBounds(getBounds().withSizeKeepingCentre(width, height));
        else
            setSize(width, height);
    }
    repaint();
}

void MagdaAlertWindow::resized() {
    juce::AlertWindow::resized();
    layoutAlert();
}

void MagdaAlertWindow::childBoundsChanged(juce::Component* child) {
    juce::AlertWindow::childBoundsChanged(child);
    layoutAlert();
}

void MagdaAlertWindow::visibilityChanged() {
    juce::AlertWindow::visibilityChanged();
    showBackdrop(isVisible());
}

void MagdaAlertWindow::showBackdrop(bool shown) {
    if (!shown) {
        backdrop_.reset();
        return;
    }
    if (backdrop_ != nullptr || backdropHost_ == nullptr || backdropHost_ == this)
        return;
    backdrop_ = std::make_unique<Backdrop>();
    backdropHost_->addAndMakeVisible(*backdrop_);
    backdrop_->setBounds(backdropHost_->getLocalBounds());
}

void MagdaAlertWindow::paintShell(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    constexpr float kRadius = 10.0f;
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_BG));
    g.fillRoundedRectangle(bounds, kRadius);
    if (!footerArea_.isEmpty()) {
        juce::Path footer;
        footer.addRoundedRectangle(footerArea_.toFloat().getX(), footerArea_.toFloat().getY(),
                                   footerArea_.toFloat().getWidth(),
                                   footerArea_.toFloat().getHeight(), kRadius, kRadius, false,
                                   false, true, true);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_PANEL));
        g.fillPath(footer);
    }
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE));
    g.drawRoundedRectangle(bounds.reduced(0.5f), kRadius, 1.0f);

    if (const auto t = tone(); t != Tone::None) {
        const auto colour = ActiveTheme::getColour(t == Tone::Warning  ? ActiveTheme::DEVICE_AMBER
                                                   : t == Tone::Danger ? ActiveTheme::DEVICE_RED
                                                                       : ActiveTheme::DEVICE_BLUE);
        const auto chip = chipArea_.toFloat();
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_BG).interpolatedWith(colour, 0.16f));
        g.fillRoundedRectangle(chip, 8.0f);
        g.setColour(colour.withAlpha(0.35f));
        g.drawRoundedRectangle(chip.reduced(0.5f), 8.0f, 1.0f);
        const juce::String glyph = t == Tone::Question ? juce::String("?")
                                   : t == Tone::Info   ? juce::String("i")
                                   : t == Tone::Warning
                                       ? juce::String("!")
                                       : juce::String(juce::CharPointer_UTF8("\xc3\x97"));
        g.setColour(colour.brighter(0.3f));
        g.setFont(FontManager::getInstance().getUIFontMedium(t == Tone::Danger ? 20.0f : 17.0f));
        g.drawText(glyph, chipArea_, juce::Justification::centred, false);
    }

    titleLayout_.draw(g, titleArea_.toFloat());
    if (!messageArea_.isEmpty())
        messageLayout_.draw(g, messageArea_.toFloat());
}

juce::AlertWindow* createMagdaAlertWindow(const juce::String& title, const juce::String& message,
                                          const juce::String& button1, const juce::String& button2,
                                          const juce::String& button3,
                                          juce::MessageBoxIconType iconType, int numButtons,
                                          juce::Component* associatedComponent) {
    auto* alert = new MagdaAlertWindow(title, message, iconType, associatedComponent);
    const juce::KeyPress returnKey(juce::KeyPress::returnKey);
    const juce::KeyPress escapeKey(juce::KeyPress::escapeKey);
    if (numButtons == 1) {
        alert->addButton(button1, 0, returnKey);
    } else if (numButtons == 2) {
        alert->addButton(button1, 1, returnKey);
        alert->addButton(button2, 0, escapeKey);
    } else if (numButtons == 3) {
        alert->addButton(button1, 1, returnKey);
        alert->addButton(button2, 2);
        alert->addButton(button3, 0, escapeKey);
    }
    return alert;
}

}  // namespace magda
