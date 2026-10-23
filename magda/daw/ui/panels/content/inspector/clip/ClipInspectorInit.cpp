#include "../../../../state/TimelineController.hpp"
#include "../ClipInspector.hpp"

namespace magda::daw::ui {

ClipInspector::ClipInspector() {
    initHeader();
    magda::Config::getInstance().addListener(this);
    initClipSection();
    initMixSection();
    initWarpSection();
    initPitchSection();
    initFadesSection();
    initLabelsSection();
    initTimeBendSection();
    initSessionLaunchSection();
    initViewport();
    lookAndFeelChanged();
}

ClipInspector::~ClipInspector() {
    magda::Config::getInstance().removeListener(this);
    magda::ClipManager::getInstance().removeListener(this);
    magda::TrackManager::getInstance().removeListener(this);
}

double ClipInspector::projectBpm() const {
    return timelineController_ ? timelineController_->getState().tempo.bpm : 120.0;
}

}  // namespace magda::daw::ui
