#include "project_api_live.hpp"

#include "../project/ProjectManager.hpp"

namespace magda {

const ProjectInfo& ProjectApiLive::getCurrentProjectInfo() const {
    return ProjectManager::getInstance().getCurrentProjectInfo();
}

void ProjectApiLive::setTempo(double bpm) {
    if (engineTempoWriter_)
        engineTempoWriter_(bpm);
    ProjectManager::getInstance().setTempo(bpm);
}

void ProjectApiLive::setTimeSignature(int numerator, int denominator) {
    if (engineTimeSignatureWriter_)
        engineTimeSignatureWriter_(numerator, denominator);
    ProjectManager::getInstance().setTimeSignature(numerator, denominator);
}

const TempoMap* ProjectApiLive::tempoMap() const {
    return engineTempoMap_ ? engineTempoMap_() : nullptr;
}

void ProjectApiLive::setEngineTempoWriter(std::function<void(double)> writer) {
    engineTempoWriter_ = std::move(writer);
}

void ProjectApiLive::setEngineTimeSignatureWriter(std::function<void(int, int)> writer) {
    engineTimeSignatureWriter_ = std::move(writer);
}

void ProjectApiLive::setEngineTempoMap(std::function<const TempoMap*()> getter) {
    engineTempoMap_ = std::move(getter);
}

}  // namespace magda
