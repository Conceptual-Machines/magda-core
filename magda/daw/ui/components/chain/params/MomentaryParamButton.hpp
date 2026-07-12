#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <utility>

namespace magda::daw::ui {

class MomentaryParamButton : public juce::TextButton {
  public:
    MomentaryParamButton() : juce::TextButton("Momentary parameter") {
        onStateChange = [this]() {
            if (!mouseGestureActive_)
                setPressed(isDown());
        };
    }

    void setValueChangedCallback(std::function<void(double)> callback) {
        onValueChanged_ = std::move(callback);
    }

    void release() {
        mouseGestureActive_ = false;
        setPressed(false);
        setState(juce::Button::buttonNormal);
    }

    void mouseDown(const juce::MouseEvent& event) override {
        mouseGestureActive_ = event.mods.isLeftButtonDown();
        juce::TextButton::mouseDown(event);
        if (mouseGestureActive_)
            setPressed(true);
    }

    void mouseUp(const juce::MouseEvent& event) override {
        juce::TextButton::mouseUp(event);
        if (mouseGestureActive_)
            setPressed(false);
        mouseGestureActive_ = false;
    }

  private:
    void setPressed(bool pressed) {
        if (pressed_ == pressed)
            return;

        pressed_ = pressed;
        if (onValueChanged_)
            onValueChanged_(pressed ? 1.0 : 0.0);
    }

    std::function<void(double)> onValueChanged_;
    bool pressed_ = false;
    bool mouseGestureActive_ = false;
};

}  // namespace magda::daw::ui
