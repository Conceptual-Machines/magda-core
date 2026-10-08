#pragma once

#include <functional>
#include <optional>

#include "core/ChainNodePath.hpp"
#include "core/LinkModeManager.hpp"
#include "ui/components/common/DraggableValueLabel.hpp"

namespace magda::daw::ui {

/**
 * @brief A rack's 0-127 chain selector (#1808).
 *
 * Links to a macro or modifier the way a device parameter does: dropped on, or clicked in
 * link mode.
 */
class ChainSelectorControl : public magda::DraggableValueLabel,
                             public juce::DragAndDropTarget,
                             public magda::LinkModeManagerListener,
                             private juce::Timer {
  public:
    explicit ChainSelectorControl(const magda::ChainNodePath& rackPath);
    ~ChainSelectorControl() override;

    void paintOverChildren(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

    void modLinkModeChanged(bool active, const magda::ModSelection& selection) override;
    void macroLinkModeChanged(bool active, const magda::MacroSelection& selection) override;

    /// The selector where macros and modifiers have moved it; nullopt once none are linked.
    std::function<void(std::optional<float>)> onModulatedValue;
    std::optional<float> getModulatedValue() const {
        return modulated_;
    }

  private:
    void timerCallback() override;
    /// The selector after every linked macro and modifier, or nullopt when none are linked.
    std::optional<float> modulatedValue() const;
    void linkMacro(const magda::ChainNodePath& owner, int macroIndex);
    void linkMod(const magda::ChainNodePath& owner, int modIndex);

    magda::ChainNodePath rackPath_;
    magda::MacroSelection linkMacro_;
    magda::ModSelection linkMod_;
    bool dragOver_ = false;
    std::optional<float> modulated_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChainSelectorControl)
};

}  // namespace magda::daw::ui
