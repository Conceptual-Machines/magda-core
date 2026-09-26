#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include "../project/ProjectInfo.hpp"

namespace magda {

class TempoMap;

enum class ProjectOpenAutosavePolicy { Fail, Recover, Ignore };
enum class ProjectFileOperationStatus {
    Succeeded,
    Cancelled,
    Conflict,
    NotFound,
    InvalidFormat,
    Failed
};

struct ProjectOpenOptions {
    bool discardUnsavedChanges = false;
    ProjectOpenAutosavePolicy autosavePolicy = ProjectOpenAutosavePolicy::Fail;
    bool allowMissingMedia = false;
    bool allowUnavailableDevices = false;
    std::vector<juce::String> availableDeviceCatalogIds;
    std::shared_ptr<std::atomic_bool> cancelled;
};

struct ProjectSaveAsOptions {
    bool overwrite = false;
    bool overwriteApproved = false;
    bool copyMedia = true;
    std::shared_ptr<std::atomic_bool> cancelled;
};

struct ProjectFileOperationResult {
    ProjectFileOperationStatus status = ProjectFileOperationStatus::Failed;
    bool recoveredAutosave = false;
    int missingMediaCount = 0;
    int unavailableDeviceCount = 0;
    ProjectInfo project;
    bool projectOpen = false;
    bool projectDirty = false;
    bool hasSaveTarget = false;
};

using ProjectFileOperationCallback = std::function<void(ProjectFileOperationResult)>;

/// Abstract view onto ProjectManager.
class ProjectApi {
  public:
    virtual ~ProjectApi() = default;

    virtual const ProjectInfo& getCurrentProjectInfo() const = 0;
    virtual bool hasOpenProject() const = 0;
    virtual bool isDirty() const = 0;
    /** Whether Save can write without asking the user to choose a path. */
    virtual bool hasSaveTarget() const = 0;
    /** Save to the existing target. Never opens a file chooser. */
    virtual bool saveProject() = 0;
    /** Never opens a dialog; dirty projects require explicit discard. */
    virtual bool newProject(bool discardUnsavedChanges) = 0;
    /** Never opens a dialog; dirty projects require explicit discard. */
    virtual bool closeProject(bool discardUnsavedChanges) = 0;
    /** Resolve and load only an already-approved path; never opens a dialog. */
    virtual void openProjectAsync(const juce::File& source, ProjectOpenOptions options,
                                  ProjectFileOperationCallback onComplete) = 0;
    /** Save only to an already-approved path; never opens a dialog. */
    virtual void saveProjectAsAsync(const juce::File& destination, ProjectSaveAsOptions options,
                                    ProjectFileOperationCallback onComplete) = 0;
    virtual void setTempo(double bpm) = 0;
    virtual void setTimeSignature(int numerator, int denominator) = 0;
    virtual void setLoopRange(double startBeats, double endBeats) = 0;

    /// The project's tempo map; null until an engine is wired.
    virtual const TempoMap* tempoMap() const = 0;
};

}  // namespace magda
