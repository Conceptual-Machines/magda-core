#include "NativeProjectDocumentAdapter.hpp"

#include "../../core/AutomationManager.hpp"
#include "../../core/ClipManager.hpp"
#include "../../core/TrackManager.hpp"
#include "ProjectSerializer.hpp"

namespace magda {

ProjectDocument NativeProjectDocumentAdapter::captureCurrentProject(const ProjectInfo& info) {
    ProjectDocument document;
    document.info = info;
    document.tracks = TrackManager::getInstance().getTracks();
    document.clips = ClipManager::getInstance().getClips();
    document.automationLanes = AutomationManager::getInstance().getLanes();
    document.automationClips = AutomationManager::getInstance().getClips();
    return document;
}

StagedProjectData NativeProjectDocumentAdapter::toStagedProjectData(
    const ProjectDocument& document) {
    StagedProjectData staged;
    staged.info = document.info;
    staged.tracks = document.tracks;
    staged.clips = document.clips;
    staged.automationLanes = document.automationLanes;
    staged.automationClips = document.automationClips;
    return staged;
}

}  // namespace magda
