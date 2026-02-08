#include <cmath>

#include "../../../../state/TimelineController.hpp"
#include "../../../../themes/DarkTheme.hpp"
#include "../../../../themes/FontManager.hpp"
#include "../../../../themes/InspectorComboBoxLookAndFeel.hpp"
#include "../../../../themes/SmallButtonLookAndFeel.hpp"
#include "../../../../utils/TimelineUtils.hpp"
#include "../ClipInspector.hpp"
#include "BinaryData.h"
#include "core/ClipOperations.hpp"

namespace magda::daw::ui {

ClipInspector::ClipInspector() {
    initClipPropertiesSection();
    initSessionLaunchSection();
    initPitchSection();
    initMixSection();
    initPlaybackSection();
    initFadesSection();
    initChannelsSection();
    initViewport();
}

ClipInspector::~ClipInspector() {
    magda::ClipManager::getInstance().removeListener(this);
}

}  // namespace magda::daw::ui
