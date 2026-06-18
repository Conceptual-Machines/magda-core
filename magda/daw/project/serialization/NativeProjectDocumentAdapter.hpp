#pragma once

#include "ProjectDocument.hpp"
#include "ProjectSerializer.hpp"

namespace magda {

class NativeProjectDocumentAdapter {
  public:
    static ProjectDocument captureCurrentProject(const ProjectInfo& info);
    static StagedProjectData toStagedProjectData(const ProjectDocument& document);
};

}  // namespace magda
