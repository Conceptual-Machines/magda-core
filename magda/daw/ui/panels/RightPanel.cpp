#include "RightPanel.hpp"

#include "state/PanelController.hpp"

namespace magda {

RightPanel::RightPanel() : TabbedPanel(daw::ui::PanelLocation::Right) {
    setName("Right Panel");
}

void RightPanel::setCollapsed(bool collapsed) {
    daw::ui::PanelController::getInstance().setCollapsed(daw::ui::PanelLocation::Right, collapsed);
}

}  // namespace magda
