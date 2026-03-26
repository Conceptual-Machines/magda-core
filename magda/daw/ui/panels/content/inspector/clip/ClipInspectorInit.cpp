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
    // Multi-clip count label (shown when multiple clips selected)
    clipCountLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
    clipCountLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    addChildComponent(clipCountLabel_);

    initClipPropertiesSection();
    initSessionLaunchSection();
    initPitchSection();
    initGrooveSection();
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
