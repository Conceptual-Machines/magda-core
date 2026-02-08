#include "InputTypeSelector.hpp"

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magda {

InputTypeSelector::InputTypeSelector() {
    setRepaintsOnMouseActivity(true);
}

void InputTypeSelector::paint(juce::Graphics& g) {
    auto audioArea = getAudioSegmentArea().toFloat();
    auto midiArea = getMidiSegmentArea().toFloat();

    bool isAudio = (currentType_ == InputType::Audio);

    // Active segment colours
    auto audioActiveColour =
        juce::Colour(0xFF446644);  // Green tint (matches RoutingSelector AudioIn)
    auto midiActiveColour = juce::Colour(0xFF446666);  // Cyan tint (matches RoutingSelector MidiIn)
    auto inactiveColour = DarkTheme::getColour(DarkTheme::BUTTON_NORMAL);

    // Audio segment
    auto audioBg = isAudio ? audioActiveColour : inactiveColour;
    if (isHovering_ && !isAudio && getAudioSegmentArea().contains(getMouseXYRelative()))
        audioBg = audioBg.brighter(0.1f);
    else if (isAudio && isHovering_)
        audioBg = audioBg.brighter(0.05f);
    g.setColour(audioBg);
    g.fillRect(audioArea);

    // MIDI segment
    auto midiBg = !isAudio ? midiActiveColour : inactiveColour;
    if (isHovering_ && isAudio && getMidiSegmentArea().contains(getMouseXYRelative()))
        midiBg = midiBg.brighter(0.1f);
    else if (!isAudio && isHovering_)
        midiBg = midiBg.brighter(0.05f);
    g.setColour(midiBg);
    g.fillRect(midiArea);

    // Separator line between segments
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawLine(midiArea.getX(), midiArea.getY() + 2, midiArea.getX(), midiArea.getBottom() - 2,
               1.0f);

    // Text labels
    auto font = FontManager::getInstance().getUIFont(9.0f);
    g.setFont(font);

    auto textColour = DarkTheme::getColour(DarkTheme::TEXT_PRIMARY);
    auto dimTextColour = DarkTheme::getColour(DarkTheme::TEXT_SECONDARY);

    g.setColour(isAudio ? textColour : dimTextColour);
    g.drawText("A", audioArea, juce::Justification::centred, false);

    g.setColour(!isAudio ? textColour : dimTextColour);
    g.drawText("M", midiArea, juce::Justification::centred, false);

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds().toFloat(), 1.0f);
}

void InputTypeSelector::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isLeftButtonDown()) {
        if (getAudioSegmentArea().contains(e.getPosition())) {
            setInputType(InputType::Audio);
        } else if (getMidiSegmentArea().contains(e.getPosition())) {
            setInputType(InputType::MIDI);
        }
    }
}

void InputTypeSelector::mouseEnter(const juce::MouseEvent&) {
    isHovering_ = true;
    repaint();
}

void InputTypeSelector::mouseExit(const juce::MouseEvent&) {
    isHovering_ = false;
    repaint();
}

void InputTypeSelector::setInputType(InputType type) {
    if (currentType_ != type) {
        currentType_ = type;
        repaint();
        if (onInputTypeChanged) {
            onInputTypeChanged(currentType_);
        }
    }
}

juce::Rectangle<int> InputTypeSelector::getAudioSegmentArea() const {
    auto bounds = getLocalBounds();
    return bounds.removeFromLeft(bounds.getWidth() / 2);
}

juce::Rectangle<int> InputTypeSelector::getMidiSegmentArea() const {
    auto bounds = getLocalBounds();
    bounds.removeFromLeft(bounds.getWidth() / 2);
    return bounds;
}

}  // namespace magda
