#pragma once

#include <functional>

#include "project_api.hpp"

namespace magda {

/// Forwards every ProjectApi call to ProjectManager::getInstance().
class ProjectApiLive : public ProjectApi {
  public:
    const ProjectInfo& getCurrentProjectInfo() const override;
    void setTempo(double bpm) override;
    void setTimeSignature(int numerator, int denominator) override;

    void setEngineTempoWriter(std::function<void(double)> writer);
    void setEngineTimeSignatureWriter(std::function<void(int, int)> writer);

  private:
    std::function<void(double)> engineTempoWriter_;
    std::function<void(int, int)> engineTimeSignatureWriter_;
};

}  // namespace magda
