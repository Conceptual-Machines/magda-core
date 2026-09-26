#include "project_api_live.hpp"

#include <algorithm>
#include <set>

#include "../project/ProjectManager.hpp"
#include "../project/serialization/ProjectSerializer.hpp"

namespace magda {

const ProjectInfo& ProjectApiLive::getCurrentProjectInfo() const {
    return ProjectManager::getInstance().getCurrentProjectInfo();
}

bool ProjectApiLive::hasOpenProject() const {
    return ProjectManager::getInstance().hasOpenProject();
}

bool ProjectApiLive::isDirty() const {
    return ProjectManager::getInstance().isDirty();
}

bool ProjectApiLive::hasSaveTarget() const {
    const auto& projects = ProjectManager::getInstance();
    return projects.hasOpenProject() &&
           projects.getCurrentProjectFile().getFullPathName().isNotEmpty();
}

bool ProjectApiLive::saveProject() {
    return ProjectManager::getInstance().saveProject();
}

bool ProjectApiLive::newProject(bool discardUnsavedChanges) {
    return ProjectManager::getInstance().newProject(
        discardUnsavedChanges ? ProjectManager::UnsavedChangesPolicy::Discard
                              : ProjectManager::UnsavedChangesPolicy::Refuse);
}

bool ProjectApiLive::closeProject(bool discardUnsavedChanges) {
    return ProjectManager::getInstance().closeProject(
        discardUnsavedChanges ? ProjectManager::UnsavedChangesPolicy::Discard
                              : ProjectManager::UnsavedChangesPolicy::Refuse);
}

namespace {

ProjectFileOperationStatus mapStatus(ProjectManager::ControlledLoadStatus status) {
    switch (status) {
        case ProjectManager::ControlledLoadStatus::Succeeded:
            return ProjectFileOperationStatus::Succeeded;
        case ProjectManager::ControlledLoadStatus::Cancelled:
            return ProjectFileOperationStatus::Cancelled;
        case ProjectManager::ControlledLoadStatus::Conflict:
            return ProjectFileOperationStatus::Conflict;
        case ProjectManager::ControlledLoadStatus::NotFound:
            return ProjectFileOperationStatus::NotFound;
        case ProjectManager::ControlledLoadStatus::InvalidFormat:
            return ProjectFileOperationStatus::InvalidFormat;
        case ProjectManager::ControlledLoadStatus::Failed:
            return ProjectFileOperationStatus::Failed;
    }
    return ProjectFileOperationStatus::Failed;
}

void inspectElements(const std::vector<ChainElement>& elements,
                     const std::vector<juce::String>& available, int& unavailable) {
    for (const auto& element : elements) {
        if (isRack(element)) {
            for (const auto& chain : getRack(element).chains)
                inspectElements(chain.elements, available, unavailable);
            continue;
        }

        const auto& device = getDevice(element);
        if (device.pluginId.isNotEmpty() && !std::ranges::contains(available, device.pluginId))
            ++unavailable;
        if (device.pads)
            for (const auto& chain : device.pads->chains)
                inspectElements(chain.elements, available, unavailable);
    }
}

ProjectManager::LoadInspection inspectLoad(
    const StagedProjectData& staged, const std::vector<juce::String>& availableDeviceCatalogIds) {
    std::set<juce::String> missing;
    const auto inspectPath = [&missing](const juce::String& path) {
        if (path.isEmpty())
            return;
        const auto file = ProjectManager::localFileForStoredMediaPath(path);
        if (file == juce::File{} || !file.existsAsFile())
            missing.insert(path);
    };
    for (const auto& source : staged.sources)
        inspectPath(source.filePath);
    for (const auto& source : staged.legacySources)
        inspectPath(source.filePath);
    for (const auto& clip : staged.clips)
        if (clip.isAudio())
            for (const auto& take : clip.audio().takes)
                inspectPath(take.filePath);

    int unavailable = 0;
    const auto inspectTrack = [&](const TrackInfo& track) {
        inspectElements(track.chain.fxChainElements, availableDeviceCatalogIds, unavailable);
        for (const auto& device : track.chain.postFxChainElements)
            if (device.device.pluginId.isNotEmpty() &&
                !std::ranges::contains(availableDeviceCatalogIds, device.device.pluginId))
                ++unavailable;
    };
    for (const auto& track : staged.tracks)
        inspectTrack(track);
    if (staged.masterTrack)
        inspectTrack(*staged.masterTrack);

    return {static_cast<int>(missing.size()), unavailable};
}

}  // namespace

void ProjectApiLive::openProjectAsync(const juce::File& source, ProjectOpenOptions options,
                                      ProjectFileOperationCallback onComplete) {
    ProjectManager::ControlledLoadOptions loadOptions;
    loadOptions.unsavedChanges = options.discardUnsavedChanges
                                     ? ProjectManager::UnsavedChangesPolicy::Discard
                                     : ProjectManager::UnsavedChangesPolicy::Refuse;
    switch (options.autosavePolicy) {
        case ProjectOpenAutosavePolicy::Fail:
            loadOptions.autosaveRecovery = ProjectManager::AutosaveRecoveryPolicy::Fail;
            break;
        case ProjectOpenAutosavePolicy::Recover:
            loadOptions.autosaveRecovery = ProjectManager::AutosaveRecoveryPolicy::Recover;
            break;
        case ProjectOpenAutosavePolicy::Ignore:
            loadOptions.autosaveRecovery = ProjectManager::AutosaveRecoveryPolicy::Ignore;
            break;
    }
    loadOptions.allowMissingMedia = options.allowMissingMedia;
    loadOptions.allowUnavailableDevices = options.allowUnavailableDevices;
    loadOptions.shouldCancel = [cancelled = options.cancelled] {
        return cancelled && cancelled->load(std::memory_order_acquire);
    };
    loadOptions.inspect = [available = std::move(options.availableDeviceCatalogIds)](
                              const StagedProjectData& staged) {
        return inspectLoad(staged, available);
    };

    ProjectManager::getInstance().loadProjectAsyncControlled(
        source, std::move(loadOptions),
        [this](const ProjectInfo& info) {
            if (engineTempoWriter_)
                engineTempoWriter_(info.tempo);
            if (engineTimeSignatureWriter_)
                engineTimeSignatureWriter_(info.timeSignatureNumerator,
                                           info.timeSignatureDenominator);
            if (engineLoopRangeWriter_)
                engineLoopRangeWriter_(info.loopStartBeats, info.loopEndBeats);
        },
        [onComplete = std::move(onComplete)](ProjectManager::ControlledLoadResult result) {
            if (!onComplete)
                return;
            ProjectFileOperationResult operation;
            operation.status = mapStatus(result.status);
            operation.recoveredAutosave = result.recoveredAutosave;
            operation.missingMediaCount = result.inspection.missingMediaCount;
            operation.unavailableDeviceCount = result.inspection.unavailableDeviceCount;
            if (result.status == ProjectManager::ControlledLoadStatus::Succeeded) {
                const auto& projects = ProjectManager::getInstance();
                operation.project = projects.getCurrentProjectInfo();
                operation.projectOpen = projects.hasOpenProject();
                operation.projectDirty = projects.isDirty();
                operation.hasSaveTarget =
                    projects.getCurrentProjectFile().getFullPathName().isNotEmpty();
            }
            onComplete(std::move(operation));
        });
}

void ProjectApiLive::saveProjectAsAsync(const juce::File& destination, ProjectSaveAsOptions options,
                                        ProjectFileOperationCallback onComplete) {
    auto save = [destination, options = std::move(options),
                 onComplete = std::move(onComplete)]() mutable {
        ProjectFileOperationResult result;
        const auto target = ProjectManager::saveTargetFor(destination);
        if (options.cancelled && options.cancelled->load(std::memory_order_acquire)) {
            result.status = ProjectFileOperationStatus::Cancelled;
        } else if (!destination.hasFileExtension(".mgd")) {
            result.status = ProjectFileOperationStatus::InvalidFormat;
        } else if (!options.overwrite && target.existsAsFile()) {
            result.status = ProjectFileOperationStatus::Conflict;
        } else if (target.existsAsFile() && !options.overwriteApproved) {
            result.status = ProjectFileOperationStatus::Conflict;
        } else {
            const auto transfer = options.copyMedia ? ProjectManager::MediaTransfer::Copy
                                                    : ProjectManager::MediaTransfer::Move;
            result.status = ProjectManager::getInstance().saveProjectAs(destination, transfer)
                                ? ProjectFileOperationStatus::Succeeded
                                : ProjectFileOperationStatus::Failed;
        }
        if (result.status == ProjectFileOperationStatus::Succeeded) {
            const auto& projects = ProjectManager::getInstance();
            result.project = projects.getCurrentProjectInfo();
            result.projectOpen = projects.hasOpenProject();
            result.projectDirty = projects.isDirty();
            result.hasSaveTarget = projects.getCurrentProjectFile().getFullPathName().isNotEmpty();
        }
        if (onComplete)
            onComplete(result);
    };

    if (juce::MessageManager::getInstanceWithoutCreating() == nullptr) {
        save();
        return;
    }
    auto sharedSave = std::make_shared<std::function<void()>>(std::move(save));
    if (!juce::MessageManager::callAsync([sharedSave] { (*sharedSave)(); }))
        (*sharedSave)();
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

void ProjectApiLive::setLoopRange(double startBeats, double endBeats) {
    if (engineLoopRangeWriter_)
        engineLoopRangeWriter_(startBeats, endBeats);
    auto& projects = ProjectManager::getInstance();
    projects.setLoopSettings(projects.getCurrentProjectInfo().loopEnabled, startBeats, endBeats);
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

void ProjectApiLive::setEngineLoopRangeWriter(std::function<void(double, double)> writer) {
    engineLoopRangeWriter_ = std::move(writer);
}

void ProjectApiLive::setEngineTempoMap(std::function<const TempoMap*()> getter) {
    engineTempoMap_ = std::move(getter);
}

}  // namespace magda
