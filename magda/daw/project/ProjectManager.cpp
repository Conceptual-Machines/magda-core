#include "ProjectManager.hpp"

#include <algorithm>

#include "../core/AutomationManager.hpp"
#include "../core/ClipManager.hpp"
#include "../core/TrackManager.hpp"
#include "ProjectSerializer.hpp"

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
    currentProject_ = std::move(newProject);
    currentFile_ = file;
    clearDirty();
    notifyProjectSaved();

    return true;
}

bool ProjectManager::loadProject(const juce::File& file) {
    // Check for unsaved changes in current project
    if (isDirty_ && !showUnsavedChangesDialog()) {
        return false;
    }

    // Check file exists
    if (!file.existsAsFile()) {
        lastError_ = "File does not exist: " + file.getFullPathName();
        return false;
    }

    // Load from file
    ProjectInfo loadedInfo;
    if (!ProjectSerializer::loadFromFile(file, loadedInfo)) {
        lastError_ = "Failed to load project: " + ProjectSerializer::getLastError();
        return false;
    }

    // Update state
    currentProject_ = loadedInfo;
    currentProject_.filePath = file.getFullPathName();
    currentFile_ = file;
    isProjectOpen_ = true;
    clearDirty();
    notifyProjectOpened();

    return true;
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

void ProjectManager::setLoopSettings(bool enabled, double start, double end) {
    if (currentProject_.loopEnabled != enabled || currentProject_.loopStart != start ||
        currentProject_.loopEnd != end) {
        currentProject_.loopEnabled = enabled;
        currentProject_.loopStart = start;
        currentProject_.loopEnd = end;
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
    // TODO: Implement UI dialog to ask user if they want to save changes
    // Until UI integration is complete, block operations that would discard
    // unsaved changes to prevent silent data loss.

    // When implemented, this dialog should offer:
    // - Save: Save changes and continue (return true)
    // - Don't Save: Discard changes and continue (return true)
    // - Cancel: Don't continue with the operation (return false)

    lastError_ = "Cannot proceed: project has unsaved changes. Please save or implement unsaved "
                 "changes dialog.";
    return false;  // Block operations until UI dialog is implemented
}

}  // namespace magda
