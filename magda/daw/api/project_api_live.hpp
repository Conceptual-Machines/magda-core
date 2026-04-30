#pragma once

#include "project_api.hpp"

namespace magda {

/// Forwards every ProjectApi call to ProjectManager::getInstance().
class ProjectApiLive : public ProjectApi {
  public:
    const ProjectInfo& getCurrentProjectInfo() const override;
};

}  // namespace magda
