#pragma once

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/ChainNodePath.hpp"

namespace magda {

/**
 * @brief The one device faceplate lent to the arrangement's track area, if any.
 *
 * A slot lends its faceplate and has it given back on collapse. Lending another gives the current
 * one back first, so only one is ever out. A slot rebuilt while its faceplate is out lends the new
 * one again (isPendingFor); if none does by the next message-loop pass, the expansion ends.
 * Message thread only; broadcasts synchronously on every change.
 */
class ExpandedFaceplate : public juce::ChangeBroadcaster {
  public:
    static ExpandedFaceplate& getInstance();

    void expand(const ChainNodePath& device, juce::Component& faceplate, const juce::String& title,
                std::function<void()> giveBack);
    void collapse();
    /// The faceplate's owner is going with it: drop the loan without giving it back.
    void release(const juce::Component& faceplate);

    /// A faceplate is out, or one is about to be lent again after a rebuild.
    bool isActive() const {
        return faceplate_ != nullptr || pending_;
    }
    juce::Component* faceplate() const {
        return faceplate_.getComponent();
    }
    const juce::String& title() const {
        return title_;
    }
    bool holds(const juce::Component& faceplate) const {
        return faceplate_.getComponent() == &faceplate;
    }
    /// Whether a slot built for @p device should lend its faceplate again.
    bool isPendingFor(const ChainNodePath& device) const {
        return pending_ && device_ == device;
    }

  private:
    void clear();

    ChainNodePath device_;
    juce::Component::SafePointer<juce::Component> faceplate_;
    juce::String title_;
    std::function<void()> giveBack_;
    bool pending_ = false;
    /// Bumped by every change, so a stale pending check leaves a newer loan alone.
    int generation_ = 0;
};

}  // namespace magda
