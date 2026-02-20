#pragma once

#include <functional>

#include "TabbedPanel.hpp"

namespace magda {

/**
 * @brief Left sidebar panel with tabbed content
 *
 * Default tabs: Plugin Browser, Sample Browser, Preset Browser
 */
class LeftPanel : public daw::ui::TabbedPanel {
  public:
    LeftPanel();
    ~LeftPanel() override = default;

    // Legacy API for compatibility
    void setCollapsed(bool collapsed);

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LeftPanel)
};

}  // namespace magda
