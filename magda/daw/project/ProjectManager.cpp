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
    // Note: AutomationManager doesn't have clearAllLanes() yet
    // TODO: Add clearAllLanes() to AutomationManager and call it here

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
    if (!currentFile_.existsAsFile()) {
        lastError_ = "No file path set. Use Save As.";
        return false;
    }

    return saveProjectAs(currentFile_);
}

bool ProjectManager::saveProjectAs(const juce::File& file) {
    // Update project info
    currentProject_.filePath = file.getFullPathName();
    currentProject_.name = file.getFileNameWithoutExtension();
    currentProject_.touch();

    // Save to file
    if (!ProjectSerializer::saveToFile(file, currentProject_)) {
        lastError_ = "Failed to save project: " + ProjectSerializer::getLastError();
        return false;
    }

    // Update state
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
    // Note: AutomationManager doesn't have clearAllLanes() yet
    // TODO: Add clearAllLanes() to AutomationManager and call it here

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
        listeners_.push_back(listener);
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
    // TODO: Show dialog asking user if they want to save changes
    // For now, just return true (don't block)
    // This should be implemented when we have UI integration

    // Options should be:
    // - Save: Save changes and continue
    // - Don't Save: Discard changes and continue
    // - Cancel: Don't continue with the operation

    return true;  // For now, allow all operations
}

}  // namespace magda
