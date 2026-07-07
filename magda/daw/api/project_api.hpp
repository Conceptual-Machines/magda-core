#pragma once

#include "../project/ProjectInfo.hpp"

namespace magda {

/// Abstract view onto ProjectManager.
class ProjectApi {
  public:
    virtual ~ProjectApi() = default;

    virtual const ProjectInfo& getCurrentProjectInfo() const = 0;
    virtual void setTempo(double bpm) = 0;
};

}  // namespace magda
