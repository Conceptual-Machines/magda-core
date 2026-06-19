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

    // MAGDA keeps the master as a singleton outside getTracks(); append it so
    // interchange formats carry its role + FX. It's already TrackType::Master,
    // so the DAWproject adapter emits role="master" and toStagedProjectData
    // peels it back onto MASTER_TRACK_ID on re-import.
    if (auto* master = TrackManager::getInstance().getTrack(MASTER_TRACK_ID))
        document.tracks.push_back(*master);

    document.clips = ClipManager::getInstance().getClips();
    document.automationLanes = AutomationManager::getInstance().getLanes();
    document.automationClips = AutomationManager::getInstance().getClips();
    return document;
}

StagedProjectData NativeProjectDocumentAdapter::toStagedProjectData(
    const ProjectDocument& document) {
    StagedProjectData staged;
    staged.info = document.info;
    staged.clips = document.clips;
    staged.automationLanes = document.automationLanes;
    staged.automationClips = document.automationClips;

    // A master-role track merges into MAGDA's singleton master rather than
    // becoming a new track; commitStaged applies staged.masterTrack onto the
    // existing MASTER_TRACK_ID. Everything else stages as a regular track.
    for (const auto& track : document.tracks) {
        if (track.type == TrackType::Master && !staged.masterTrack)
            staged.masterTrack = std::make_unique<TrackInfo>(track);
        else
            staged.tracks.push_back(track);
    }
    return staged;
}

}  // namespace magda
