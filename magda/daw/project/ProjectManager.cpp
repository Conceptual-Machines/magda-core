#include "ProjectManager.hpp"

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>

#include "../core/AutomationManager.hpp"
#include "../core/ClipManager.hpp"
#include "../core/TrackManager.hpp"
#include "serialization/ProjectSerializer.hpp"

namespace magda {

ProjectManager& ProjectManager::getInstance() {
    static ProjectManager instance;
    return instance;
}

ProjectManager::ProjectManager() {
    // Initialize with default project info
    currentProject_.name = "Untitled";
    currentProject_.version = "1.0.0";
}

ProjectManager::~ProjectManager() {
    joinBackgroundThread();
}

void ProjectManager::joinBackgroundThread() {
    if (loadThread_.joinable())
        loadThread_.join();
}

// ============================================================================
// Project Lifecycle
// ============================================================================

bool ProjectManager::newProject() {
    // Check for unsaved changes
    if (isDirty_ && !showUnsavedChangesDialog()) {
        return false;
    }

    // Clear all project content from singleton managers
    TrackManager::getInstance().clearAllTracks();
    ClipManager::getInstance().clearAllClips();
    AutomationManager::getInstance().clearAll();

    // Reset project state
    currentProject_ = ProjectInfo();
    currentProject_.name = "Untitled";
    currentProject_.version = "1.0.0";
    currentFile_ = juce::File();
    isProjectOpen_ = true;

    clearDirty();
    notifyProjectOpened();

    return true;
}

bool ProjectManager::saveProject() {
    if (currentFile_.getFullPathName().isEmpty() ||
        !currentFile_.getParentDirectory().isDirectory()) {
        lastError_ = "No file path set. Use Save As.";
        return false;
    }

    return saveProjectAs(currentFile_);
}

bool ProjectManager::saveProjectAs(const juce::File& file) {
    // Prepare updated project info without mutating currentProject_ yet
    ProjectInfo newProject = currentProject_;
    newProject.filePath = file.getFullPathName();
    newProject.name = file.getFileNameWithoutExtension();
    newProject.touch();

    // Save to file
    if (!ProjectSerializer::saveToFile(file, newProject)) {
        lastError_ = "Failed to save project: " + ProjectSerializer::getLastError();
        return false;
    }

    // Commit updated state only after successful save
    const bool wasOpen = isProjectOpen_;
    currentProject_ = std::move(newProject);
    currentFile_ = file;
    isProjectOpen_ = true;
    clearDirty();

    if (!wasOpen) {
        notifyProjectOpened();
    } else {
        notifyProjectSaved();
    }

    return true;
}

bool ProjectManager::loadProject(const juce::File& file,
                                 std::function<void(const ProjectInfo&)> onBeforeCommit) {
    // Check for unsaved changes in current project
    if (isDirty_ && !showUnsavedChangesDialog()) {
        return false;
    }

    // Check file exists
    if (!file.existsAsFile()) {
        lastError_ = "File does not exist: " + file.getFullPathName();
        return false;
    }

    // Stage first (file I/O + parse + validate)
    StagedProjectData staged;
    if (!ProjectSerializer::loadAndStage(file, staged)) {
        lastError_ = "Failed to load project: " + ProjectSerializer::getLastError();
        return false;
    }

    // Set tempo/time sig/loop on the audio engine BEFORE committing tracks & clips,
    // so that audio engine clip sync uses the correct BPM.
    if (onBeforeCommit)
        onBeforeCommit(staged.info);

    // Commit staged data to singleton managers
    ProjectSerializer::commitStaged(staged);

    // Update state
    currentProject_ = staged.info;
    currentProject_.filePath = file.getFullPathName();
    currentFile_ = file;
    isProjectOpen_ = true;
    clearDirty();
    notifyProjectOpened();

    return true;
}

void ProjectManager::loadProjectAsync(const juce::File& file,
                                      std::function<void(const ProjectInfo&)> onBeforeCommit,
                                      std::function<void(bool, const juce::String&)> onComplete) {
    // Pre-flight checks on the message thread
    if (isDirty_ && !showUnsavedChangesDialog()) {
        if (onComplete)
            onComplete(false, "Cancelled by user");
        return;
    }

    if (!file.existsAsFile()) {
        if (onComplete)
            onComplete(false, "File does not exist: " + file.getFullPathName());
        return;
    }

    // Capture file path for the background thread
    auto fileCopy = file;

    // Join any previous background load before starting a new one
    joinBackgroundThread();

    // Launch background thread for I/O + parse + staging
    loadThread_ = std::thread([fileCopy, onBeforeCommit, onComplete, this]() {
        auto staged = std::make_shared<StagedProjectData>();
        bool ok = ProjectSerializer::loadAndStage(fileCopy, *staged);
        juce::String error =
            ok ? juce::String() : ("Failed to load project: " + ProjectSerializer::getLastError());

        // Bounce back to the message thread for commit + notification
        juce::MessageManager::callAsync(
            [this, staged, ok, error, fileCopy, onBeforeCommit, onComplete]() {
                if (ok) {
                    // Set tempo/time sig/loop BEFORE committing tracks & clips,
                    // so that audio engine clip sync uses the correct BPM.
                    if (onBeforeCommit)
                        onBeforeCommit(staged->info);

                    ProjectSerializer::commitStaged(*staged);
                    currentProject_ = staged->info;
                    currentProject_.filePath = fileCopy.getFullPathName();
                    currentFile_ = fileCopy;
                    isProjectOpen_ = true;
                    clearDirty();
                    notifyProjectOpened();
                }

                if (onComplete)
                    onComplete(ok, error);
            });
    });
}

bool ProjectManager::closeProject() {
    // Check for unsaved changes
    if (isDirty_ && !showUnsavedChangesDialog()) {
        return false;
    }

    // Clear all project content from singleton managers
    TrackManager::getInstance().clearAllTracks();
    ClipManager::getInstance().clearAllClips();
    AutomationManager::getInstance().clearAll();

    // Reset state
    currentProject_ = ProjectInfo();
    currentFile_ = juce::File();
    isProjectOpen_ = false;
    clearDirty();
    notifyProjectClosed();

    return true;
}

// ============================================================================
// Project State
// ============================================================================

juce::String ProjectManager::getProjectName() const {
    if (currentFile_.existsAsFile()) {
        return currentFile_.getFileNameWithoutExtension();
    }
    return currentProject_.name;
}

void ProjectManager::setTempo(double tempo) {
    if (currentProject_.tempo != tempo) {
        currentProject_.tempo = tempo;
        markDirty();
    }
}

void ProjectManager::setTimeSignature(int numerator, int denominator) {
    if (currentProject_.timeSignatureNumerator != numerator ||
        currentProject_.timeSignatureDenominator != denominator) {
        currentProject_.timeSignatureNumerator = numerator;
        currentProject_.timeSignatureDenominator = denominator;
        markDirty();
    }
}

void ProjectManager::setLoopSettings(bool enabled, double startBeats, double endBeats) {
    if (currentProject_.loopEnabled != enabled || currentProject_.loopStartBeats != startBeats ||
        currentProject_.loopEndBeats != endBeats) {
        currentProject_.loopEnabled = enabled;
        currentProject_.loopStartBeats = startBeats;
        currentProject_.loopEndBeats = endBeats;
        markDirty();
    }
}

void ProjectManager::markDirty() {
    if (!isDirty_) {
        isDirty_ = true;
        notifyDirtyStateChanged();
    }
}

void ProjectManager::clearDirty() {
    if (isDirty_) {
        isDirty_ = false;
        notifyDirtyStateChanged();
    }
}

// ============================================================================
// Listeners
// ============================================================================

void ProjectManager::addListener(ProjectManagerListener* listener) {
    if (listener != nullptr) {
        // Avoid adding the same listener multiple times
        auto it = std::find(listeners_.begin(), listeners_.end(), listener);
        if (it == listeners_.end()) {
            listeners_.push_back(listener);
        }
    }
}

void ProjectManager::removeListener(ProjectManagerListener* listener) {
    listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), listener), listeners_.end());
}

void ProjectManager::notifyProjectOpened() {
    for (auto* listener : listeners_) {
        listener->projectOpened(currentProject_);
    }
}

void ProjectManager::notifyProjectSaved() {
    for (auto* listener : listeners_) {
        listener->projectSaved(currentProject_);
    }
}

void ProjectManager::notifyProjectClosed() {
    for (auto* listener : listeners_) {
        listener->projectClosed();
    }
}

void ProjectManager::notifyDirtyStateChanged() {
    for (auto* listener : listeners_) {
        listener->projectDirtyStateChanged(isDirty_);
    }
}

bool ProjectManager::showUnsavedChangesDialog() {
    // Modal dialog: returns 1 for "Save", 2 for "Don't Save", 0 for "Cancel"
    int result = juce::AlertWindow::showYesNoCancelBox(
        juce::AlertWindow::QuestionIcon, "Unsaved Changes",
        "You have unsaved changes. Do you want to save before continuing?", "Save", "Don't Save",
        "Cancel");

    if (result == 0) {
        // Cancel — abort the operation
        return false;
    }

    if (result == 1) {
        // Save — attempt to save, abort if save fails
        if (!currentFile_.getFullPathName().isEmpty()) {
            if (!saveProject()) {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                       "Save Failed",
                                                       "Could not save project: " + lastError_);
                return false;
            }
        } else {
            // No file path — can't save without a file chooser (synchronous context).
            // Treat as cancel so the user can use Save As first.
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon, "Save Required",
                "Please use File > Save As to save your project first.");
            return false;
        }
    }

    // result == 2: Don't Save — proceed without saving
    return true;
}

}  // namespace magda
