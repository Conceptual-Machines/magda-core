// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "EditToolPalette.hpp"

#include "EditToolbar.hpp"

namespace magda {

namespace {

// Picks set the tool outright: EditToolState's hold-to-swap would revert a key
// held here once the palette has gone and the release lands elsewhere.
class EditToolPalette final : public juce::Component {
  public:
    EditToolPalette(EditToolState& state, const std::array<const char*, 5>& tooltips)
        : state_(state), buttons_(state, tooltips) {
        addAndMakeVisible(buttons_);
        buttons_.addMouseListener(this, true);
        setWantsKeyboardFocus(true);
        setSize(EditToolButtons::kWidth, EditToolButtons::kHeight);
    }

    void resized() override {
        buttons_.setBounds(getLocalBounds());
    }

    void parentHierarchyChanged() override {
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer(this)] {
            if (safe != nullptr && safe->isShowing())
                safe->grabKeyboardFocus();
        });
    }

    void mouseUp(const juce::MouseEvent&) override {
        dismiss();
    }

    bool keyPressed(const juce::KeyPress& key) override {
        const auto c = key.getTextCharacter();
        if (!key.getModifiers().isAnyModifierKeyDown() && c >= '1' && c <= '5') {
            state_.setTool(static_cast<EditTool>(c - '1'));
            dismiss();
            return true;
        }
        if (key == juce::KeyPress::escapeKey || key == juce::KeyPress('t')) {
            dismiss();
            return true;
        }
        return false;
    }

  private:
    void dismiss() {
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    }

    EditToolState& state_;
    EditToolButtons buttons_;
};

}  // namespace

void showEditToolPalette(EditToolState& state, const std::array<const char*, 5>& tooltips,
                         juce::Rectangle<int> screenArea) {
    juce::CallOutBox::launchAsynchronously(std::make_unique<EditToolPalette>(state, tooltips),
                                           screenArea, nullptr);
}

CurrentEditToolButton::CurrentEditToolButton(EditToolState& state,
                                             const std::array<const char*, 5>& tooltips)
    : SvgButton("EditTool", editToolIcon(state.getTool()).first,
                static_cast<size_t>(editToolIcon(state.getTool()).second)),
      state_(state),
      tooltips_(tooltips) {
    setTooltip("Tools (T)");
    onClick = [this]() { showEditToolPalette(state_, tooltips_, getScreenBounds()); };
    state_.addChangeListener(this);
}

CurrentEditToolButton::~CurrentEditToolButton() {
    state_.removeChangeListener(this);
}

void CurrentEditToolButton::changeListenerCallback(juce::ChangeBroadcaster*) {
    showTool();
}

void CurrentEditToolButton::showTool() {
    const auto [svg, svgSize] = editToolIcon(state_.getTool());
    updateSvgData(svg, static_cast<size_t>(svgSize));
}

}  // namespace magda
