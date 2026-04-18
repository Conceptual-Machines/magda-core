#include "QwertyKeyboardPopup.hpp"

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magda {

namespace {
constexpr int kPaddingX = 10;
constexpr int kPaddingY = 8;
constexpr int kHeaderHeight = 22;
constexpr int kFooterHeight = 18;
constexpr int kBlackKeyHeightRatio = 60;  // percentage of white key height

juce::String formatNoteName(int semitone, int octave) {
    static const std::array<const char*, 12> names{
        {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}};
    return juce::String(names[static_cast<size_t>(semitone)]) + juce::String(octave);
}

juce::String sharpAccidental(int semitone) {
    static const std::array<const char*, 12> names{
        {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}};
    return names[static_cast<size_t>(semitone)];
}
}  // namespace

QwertyKeyboardPopup::QwertyKeyboardPopup(QwertyMidiKeyboard& keyboard) : keyboard_(keyboard) {
    setSize(440, 180);

    // Opening a CallOutBox normally steals keyboard focus, which would stop
    // the QwertyMidiKeyboard from receiving key events routed through the
    // main window. Decline focus on the popup and its children, and route
    // any stray key events directly to the keyboard.
    setWantsKeyboardFocus(false);
    addKeyListener(&keyboard_);

    octaveDownButton_.setButtonText("Z");
    octaveDownButton_.setTooltip("Shift base octave down");
    octaveDownButton_.setWantsKeyboardFocus(false);
    octaveDownButton_.onClick = [this]() { shiftOctave(-1); };
    addAndMakeVisible(octaveDownButton_);

    octaveUpButton_.setButtonText("X");
    octaveUpButton_.setTooltip("Shift base octave up");
    octaveUpButton_.setWantsKeyboardFocus(false);
    octaveUpButton_.onClick = [this]() { shiftOctave(1); };
    addAndMakeVisible(octaveUpButton_);

    octaveLabel_.setFont(FontManager::getInstance().getUIFontMedium(12.0f));
    octaveLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    octaveLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(octaveLabel_);

    hintLabel_.setText("Modifier combos and space always pass through", juce::dontSendNotification);
    hintLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    hintLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    hintLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(hintLabel_);

    startTimerHz(30);
}

QwertyKeyboardPopup::~QwertyKeyboardPopup() {
    stopTimer();
    removeKeyListener(&keyboard_);
}

void QwertyKeyboardPopup::shiftOctave(int delta) {
    keyboard_.setBaseOctave(keyboard_.getBaseOctave() + delta);
    repaint();
}

void QwertyKeyboardPopup::timerCallback() {
    auto held = keyboard_.getHeldNotes();
    auto octave = keyboard_.getBaseOctave();
    if (octave != lastBaseOctave_ || held != lastHeldNotes_) {
        lastBaseOctave_ = octave;
        lastHeldNotes_ = held;
        octaveLabel_.setText("Base octave: " + juce::String(octave), juce::dontSendNotification);
        repaint();
    }
}

void QwertyKeyboardPopup::resized() {
    auto bounds = getLocalBounds().reduced(kPaddingX, kPaddingY);

    auto header = bounds.removeFromTop(kHeaderHeight);
    // Octave buttons on the right of the header row, label fills the rest.
    const int btnW = 28;
    const int btnGap = 4;
    auto btnUp = header.removeFromRight(btnW);
    header.removeFromRight(btnGap);
    auto btnDown = header.removeFromRight(btnW);
    header.removeFromRight(10);
    octaveUpButton_.setBounds(btnUp);
    octaveDownButton_.setBounds(btnDown);
    octaveLabel_.setBounds(header);

    bounds.removeFromTop(6);

    auto footer = bounds.removeFromBottom(kFooterHeight);
    hintLabel_.setBounds(footer);

    keyboardArea_ = bounds;
}

juce::Rectangle<float> QwertyKeyboardPopup::whiteKeyBounds(size_t index) const {
    const float total = static_cast<float>(kWhiteKeys_.size());
    const float w = keyboardArea_.getWidth() / total;
    return juce::Rectangle<float>(keyboardArea_.getX() + static_cast<float>(index) * w,
                                  static_cast<float>(keyboardArea_.getY()), w,
                                  static_cast<float>(keyboardArea_.getHeight()));
}

juce::Rectangle<float> QwertyKeyboardPopup::blackKeyBounds(const BlackKey& bk) const {
    auto anchor = whiteKeyBounds(static_cast<size_t>(bk.anchorWhiteIndex));
    const float w = anchor.getWidth() * 0.62f;
    const float h = anchor.getHeight() * (kBlackKeyHeightRatio / 100.0f);
    return juce::Rectangle<float>(anchor.getRight() - w * 0.5f, anchor.getY(), w, h);
}

void QwertyKeyboardPopup::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));

    // Outer border
    g.setColour(DarkTheme::getBorderColour());
    g.drawRect(getLocalBounds(), 1);

    if (keyboardArea_.isEmpty())
        return;

    auto heldNotes = keyboard_.getHeldNotes();
    const int baseOctave = keyboard_.getBaseOctave();

    auto& fm = FontManager::getInstance();
    auto letterFont = fm.getUIFontBold(13.0f);
    auto noteFont = fm.getUIFont(9.0f);

    const auto whiteFill = juce::Colours::white.withAlpha(0.92f);
    const auto whiteHeld = DarkTheme::getAccentColour().brighter(0.2f);
    const auto blackFill = juce::Colour(0xFF1A1A1A);
    const auto blackHeld = DarkTheme::getAccentColour().darker(0.4f);
    const auto borderCol = DarkTheme::getBorderColour();

    auto noteFromCell = [baseOctave](int semitone, int octaveOffset) {
        return (baseOctave + octaveOffset) * 12 + semitone;
    };

    // White keys first
    for (size_t i = 0; i < kWhiteKeys_.size(); ++i) {
        const auto& key = kWhiteKeys_[i];
        auto rect = whiteKeyBounds(i);
        int note = noteFromCell(key.semitone, key.octaveOffset);
        bool held = heldNotes.find(note) != heldNotes.end();

        g.setColour(held ? whiteHeld : whiteFill);
        g.fillRect(rect.reduced(0.5f));
        g.setColour(borderCol);
        g.drawRect(rect, 1.0f);

        // Note label near the bottom of the key.
        g.setColour(juce::Colour(0xFF1F1F1F));
        g.setFont(noteFont);
        auto noteLabelRect = rect.removeFromBottom(14.0f).reduced(2.0f, 2.0f);
        g.drawText(formatNoteName(key.semitone, baseOctave + key.octaveOffset),
                   noteLabelRect.toNearestInt(), juce::Justification::centred);

        // QWERTY letter near the middle-top of the key, scaled up for visibility.
        g.setFont(letterFont);
        g.setColour(juce::Colour(0xFF111111));
        auto letterRect = rect;
        g.drawText(juce::String::charToString(key.letter), letterRect.toNearestInt(),
                   juce::Justification::centred);
    }

    // Black keys on top
    for (const auto& bk : kBlackKeys_) {
        auto rect = blackKeyBounds(bk);
        int note = noteFromCell(bk.semitone, bk.octaveOffset);
        bool held = heldNotes.find(note) != heldNotes.end();

        g.setColour(held ? blackHeld : blackFill);
        g.fillRect(rect);
        g.setColour(borderCol);
        g.drawRect(rect, 1.0f);

        // Note label
        g.setFont(noteFont);
        g.setColour(juce::Colours::white.withAlpha(0.75f));
        auto noteLabelRect = rect.removeFromBottom(12.0f).reduced(2.0f, 1.0f);
        g.drawText(sharpAccidental(bk.semitone), noteLabelRect.toNearestInt(),
                   juce::Justification::centred);

        // QWERTY letter
        g.setFont(letterFont);
        g.setColour(juce::Colours::white);
        g.drawText(juce::String::charToString(bk.letter), rect.toNearestInt(),
                   juce::Justification::centred);
    }
}

juce::String QwertyKeyboardPopup::noteName(int semitone, int octave) {
    return formatNoteName(semitone, octave);
}

}  // namespace magda
