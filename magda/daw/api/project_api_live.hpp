#pragma once

#include <functional>

#include "project_api.hpp"

namespace magda {

/// Forwards every ProjectApi call to ProjectManager::getInstance().
class ProjectApiLive : public ProjectApi {
  public:
    const ProjectInfo& getCurrentProjectInfo() const override;
    bool hasOpenProject() const override;
    bool isDirty() const override;
    bool hasSaveTarget() const override;
    bool saveProject() override;
    bool newProject(bool discardUnsavedChanges) override;
    bool closeProject(bool discardUnsavedChanges) override;
    void setTempo(double bpm) override;
    void setTimeSignature(int numerator, int denominator) override;
    void setLoopRange(double startBeats, double endBeats) override;
    const TempoMap* tempoMap() const override;

    void setEngineTempoWriter(std::function<void(double)> writer);
    void setEngineTimeSignatureWriter(std::function<void(int, int)> writer);
    void setEngineLoopRangeWriter(std::function<void(double, double)> writer);
    void setEngineTempoMap(std::function<const TempoMap*()> getter);

  private:
    std::function<void(double)> engineTempoWriter_;
    std::function<void(int, int)> engineTimeSignatureWriter_;
    std::function<void(double, double)> engineLoopRangeWriter_;
    std::function<const TempoMap*()> engineTempoMap_;
};

}  // namespace magda
