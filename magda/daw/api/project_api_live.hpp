#pragma once

#include <functional>

#include "project_api.hpp"

namespace magda {

/// Forwards every ProjectApi call to ProjectManager::getInstance().
class ProjectApiLive : public ProjectApi {
  public:
    const ProjectInfo& getCurrentProjectInfo() const override;
    void setTempo(double bpm) override;

    void setEngineTempoWriter(std::function<void(double)> writer);

  private:
    std::function<void(double)> engineTempoWriter_;
};

}  // namespace magda
