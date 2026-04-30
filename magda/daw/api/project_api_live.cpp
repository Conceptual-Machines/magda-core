#include "project_api_live.hpp"

#include "../project/ProjectManager.hpp"

namespace magda {

const ProjectInfo& ProjectApiLive::getCurrentProjectInfo() const {
    return ProjectManager::getInstance().getCurrentProjectInfo();
}

}  // namespace magda
