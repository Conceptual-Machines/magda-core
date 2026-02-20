#include "LeftPanel.hpp"

#include "state/PanelController.hpp"

namespace magda {

LeftPanel::LeftPanel() : TabbedPanel(daw::ui::PanelLocation::Right) {
    setName("Left Panel");
}

void LeftPanel::setCollapsed(bool collapsed) {
    daw::ui::PanelController::getInstance().setCollapsed(daw::ui::PanelLocation::Right, collapsed);
}

}  // namespace magda
