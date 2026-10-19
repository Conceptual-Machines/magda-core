#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "ui/components/common/SvgButton.hpp"

namespace magda {

/// The track area's home for a faceplate a device slot lends out (ExpandedFaceplate): its
/// device's name and a collapse button over it.
class ExpandedFaceplateHost : public juce::Component, private juce::ChangeListener {
  public:
    ExpandedFaceplateHost();
    ~ExpandedFaceplateHost() override;

    /// Whether anything is out, so the track area should give way.
    bool isShowing() const;

    /// Called when a faceplate comes or goes.
    std::function<void()> onShowingChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

  private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void adoptFaceplate();

    SvgButton collapseButton_;
    juce::Rectangle<int> titleArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExpandedFaceplateHost)
};

}  // namespace magda
