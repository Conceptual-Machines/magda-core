#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace magda::daw::ui {

/** @brief A main-window area that context commands such as Ctrl+A act on. */
enum class FocusRegion { None, Content, Bottom, Left, Right };

/**
 * @brief Tracks which region holds keyboard focus and outlines it (#3157).
 *
 * A click inside a region moves focus into it when the clicked component did not
 * take focus itself. The outline is an overlay on the host, so it takes no layout
 * space, and it hides while focus is outside the host window.
 */
class PanelFocusTracker : private juce::FocusChangeListener,
                          private juce::MouseListener,
                          private juce::ComponentListener {
  public:
    explicit PanelFocusTracker(juce::Component& host);
    ~PanelFocusTracker() override;

    /** @brief Register a direct child of the host as a region; it is made focusable. */
    void addRegion(juce::Component& component, FocusRegion region);

    /** @brief The region containing @p component, or None. */
    FocusRegion regionOf(const juce::Component* component) const;

    /** @brief The region containing the currently focused component. */
    FocusRegion getActiveRegion() const;

    /** @brief Re-place the outline on the focused region. */
    void update();

    /** @brief The outline overlay, exposed for tests. */
    const juce::Component& getOutline() const;

  private:
    class Outline;

    struct Entry {
        juce::Component::SafePointer<juce::Component> component;
        FocusRegion region;
    };

    juce::Component& host_;
    std::unique_ptr<Outline> outline_;
    std::vector<Entry> regions_;
    juce::Component::SafePointer<juce::Component> lastFocusedRegion_;

    juce::Component* regionComponentOf(const juce::Component* component) const;

    void globalFocusChanged(juce::Component* focusedComponent) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void componentMovedOrResized(juce::Component&, bool, bool) override;
    void componentVisibilityChanged(juce::Component&) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PanelFocusTracker)
};

}  // namespace magda::daw::ui
