#include "LeftPanel.hpp"

#include "state/PanelController.hpp"

namespace magda {

LeftPanel::LeftPanel() : TabbedPanel(daw::ui::PanelLocation::Left) {
    setName("Left Panel");
}

void LeftPanel::setCollapsed(bool collapsed) {
    daw::ui::PanelController::getInstance().setCollapsed(daw::ui::PanelLocation::Left, collapsed);
}

}  // namespace magda
