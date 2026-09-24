#pragma once

#include "../project/ProjectInfo.hpp"

namespace magda {

class TempoMap;

/// Abstract view onto ProjectManager.
class ProjectApi {
  public:
    virtual ~ProjectApi() = default;

    virtual const ProjectInfo& getCurrentProjectInfo() const = 0;
    virtual bool isDirty() const = 0;
    /** Whether Save can write without asking the user to choose a path. */
    virtual bool hasSaveTarget() const = 0;
    /** Save to the existing target. Never opens a file chooser. */
    virtual bool saveProject() = 0;
    virtual void setTempo(double bpm) = 0;
    virtual void setTimeSignature(int numerator, int denominator) = 0;

    /// The project's tempo map; null until an engine is wired.
    virtual const TempoMap* tempoMap() const = 0;
};

}  // namespace magda
