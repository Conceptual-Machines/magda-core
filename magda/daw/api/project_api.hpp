#pragma once

#include "../project/ProjectInfo.hpp"

namespace magda {

/// Abstract view onto ProjectManager — read-only project info today.
class ProjectApi {
  public:
    virtual ~ProjectApi() = default;

    virtual const ProjectInfo& getCurrentProjectInfo() const = 0;
};

}  // namespace magda
