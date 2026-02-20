#include "RightPanel.hpp"

#include "state/PanelController.hpp"

namespace magda {

RightPanel::RightPanel() : TabbedPanel(daw::ui::PanelLocation::Left) {
    setName("Right Panel");
}

void RightPanel::setCollapsed(bool collapsed) {
    daw::ui::PanelController::getInstance().setCollapsed(daw::ui::PanelLocation::Left, collapsed);
}

}  // namespace magda
