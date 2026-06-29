#include "project_api_live.hpp"

#include "../project/ProjectManager.hpp"

namespace magda {

const ProjectInfo& ProjectApiLive::getCurrentProjectInfo() const {
    return ProjectManager::getInstance().getCurrentProjectInfo();
}

void ProjectApiLive::setTempo(double bpm) {
    ProjectManager::getInstance().setTempo(bpm);
}

}  // namespace magda
