#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "ProjectInfo.hpp"

namespace magda {

/**
 * @brief Listener interface for project lifecycle events
 */
class ProjectManagerListener {
  public:
    virtual ~ProjectManagerListener() = default;

    /**
     * @brief Called when a project is opened or created
     */
    virtual void projectOpened(const ProjectInfo& info) {
        juce::ignoreUnused(info);
    }

    /**
     * @brief Called when a project is saved
     */
    virtual void projectSaved(const ProjectInfo& info) {
        juce::ignoreUnused(info);
    }

    /**
     * @brief Called when a project is closed
     */
    virtual void projectClosed() {}

    /**
     * @brief Called when the project dirty state changes
     * @param isDirty True if there are unsaved changes
     */
    virtual void projectDirtyStateChanged(bool isDirty) {
        juce::ignoreUnused(isDirty);
    }
};

/**
 * @brief Singleton manager for project lifecycle and dirty state tracking
 *
 * Handles new/open/save/close operations and tracks unsaved changes.
 */
class ProjectManager {
  public:
    static ProjectManager& getInstance();

    // Prevent copying
    ProjectManager(const ProjectManager&) = delete;
    ProjectManager& operator=(const ProjectManager&) = delete;

    // ========================================================================
    // Project Lifecycle
    // ========================================================================

    /**
     * @brief Create a new empty project
     * @return true on success
     */
    bool newProject();

    /**
     * @brief Save project to current file
     * @return true on success, false if no current file or save failed
     */
    bool saveProject();

    /**
     * @brief Save project to a new file
     * @param file Target file path
     * @return true on success
     */
    bool saveProjectAs(const juce::File& file);

    /**
     * @brief Load project from file
     * @param file Source file path
     * @return true on success
     */
    bool loadProject(const juce::File& file);

    /**
     * @brief Close current project
     * @return true on success, false if user cancels due to unsaved changes
     */
    bool closeProject();

    // ========================================================================
    // Project State
    // ========================================================================

    /**
     * @brief Check if there are unsaved changes
     */
    bool hasUnsavedChanges() const {
        return isDirty_;
    }

    /**
     * @brief Get current project file path
     */
    juce::File getCurrentProjectFile() const {
        return currentFile_;
    }

    /**
     * @brief Get current project info
     */
    const ProjectInfo& getCurrentProjectInfo() const {
        return currentProject_;
    }

    /**
     * @brief Check if a project is currently open
     */
    bool hasOpenProject() const {
        return currentFile_.existsAsFile() || isDirty_;
    }

    /**
     * @brief Get the project name (filename without extension)
     */
    juce::String getProjectName() const;

    /**
     * @brief Set project tempo
     */
    void setTempo(double tempo);

    /**
     * @brief Set project time signature
     */
    void setTimeSignature(int numerator, int denominator);

    /**
     * @brief Set project loop settings
     */
    void setLoopSettings(bool enabled, double start, double end);

    /**
     * @brief Mark project as dirty (unsaved changes)
     * Called by managers when data changes
     */
    void markDirty();

    // ========================================================================
    // Listeners
    // ========================================================================

    void addListener(ProjectManagerListener* listener);
    void removeListener(ProjectManagerListener* listener);

    /**
     * @brief Get last error message from failed operation
     */
    const juce::String& getLastError() const {
        return lastError_;
    }

  private:
    ProjectManager();
    ~ProjectManager() = default;

    ProjectInfo currentProject_;
    juce::File currentFile_;
    bool isDirty_ = false;

    std::vector<ProjectManagerListener*> listeners_;
    juce::String lastError_;

    void clearDirty();
    void notifyProjectOpened();
    void notifyProjectSaved();
    void notifyProjectClosed();
    void notifyDirtyStateChanged();

    /**
     * @brief Show unsaved changes dialog
     * @return true if user wants to save, false if cancel
     */
    bool showUnsavedChangesDialog();
};

}  // namespace magda
