#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <functional>
#include <thread>
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
class ProjectManager : private juce::Timer {
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
     * @brief Load project from file (synchronous)
     * @param file Source file path
     * @param onBeforeCommit Optional callback invoked after staging but before committing data.
     *                       Use this to set tempo/time sig on the audio engine so that clip sync
     *                       uses the correct BPM. Receives the loaded ProjectInfo.
     * @return true on success
     */
    bool loadProject(const juce::File& file,
                     std::function<void(const ProjectInfo&)> onBeforeCommit = nullptr);

    /**
     * @brief Load project asynchronously (heavy I/O on background thread, commit on message thread)
     * @param file Source file path
     * @param onBeforeCommit Callback invoked on message thread before committing staged data.
     *                       Use this to set tempo/time sig on the audio engine so that clip sync
     *                       uses the correct BPM. Receives the loaded ProjectInfo.
     * @param onComplete Callback invoked on message thread after commit: (success, errorMessage)
     */
    void loadProjectAsync(const juce::File& file,
                          std::function<void(const ProjectInfo&)> onBeforeCommit,
                          std::function<void(bool, const juce::String&)> onComplete);

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
     * @brief Get mutable project info (for capturing live state before save)
     */
    ProjectInfo& getMutableProjectInfo() {
        return currentProject_;
    }

    /**
     * @brief Check if a project is currently open
     */
    bool hasOpenProject() const {
        return isProjectOpen_;
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
    void setLoopSettings(bool enabled, double startBeats, double endBeats);

    /**
     * @brief Check if the project has unsaved changes
     */
    bool isDirty() const {
        return isDirty_;
    }

    /**
     * @brief Show unsaved changes dialog and ask whether to proceed
     * @return true if the user chooses to proceed, false if cancelled
     */
    bool showUnsavedChangesDialog();

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
     * @brief Callback invoked before saving to capture live state (e.g., plugin native state)
     * Set by AudioBridge or TracktionEngineWrapper at initialization.
     */
    std::function<void()> onBeforeSave;

    /**
     * @brief Callback invoked after loading a project to restore UI state (zoom, view mode)
     */
    std::function<void(const ProjectInfo&)> onAfterLoad;

    /**
     * @brief Get last error message from failed operation
     */
    const juce::String& getLastError() const {
        return lastError_;
    }

    // ========================================================================
    // Auto-Save
    // ========================================================================

    /**
     * @brief Enable or disable auto-save
     * @param enabled Whether auto-save is active
     * @param intervalSeconds Interval between auto-save checks (default 60s)
     */
    void setAutoSaveEnabled(bool enabled, int intervalSeconds = 60);

    bool isAutoSaveEnabled() const {
        return autoSaveEnabled_;
    }

    /**
     * @brief Check if an autosave file exists for the given project file
     * @param projectFile The .mgd project file
     * @return The autosave file if it exists, or an invalid File
     */
    static juce::File getAutosaveFile(const juce::File& projectFile);

    /**
     * @brief Check for autosave recovery and prompt user
     * @param projectFile The .mgd project file being opened
     * @return true if the user chose to recover (caller should load the autosave),
     *         false if the user declined (caller should load the original)
     */
    static bool promptAutosaveRecovery(const juce::File& projectFile);

    // ========================================================================
    // Media Directories
    // ========================================================================

    /**
     * @brief Get the project media directory root
     */
    juce::File getMediaDirectory() const {
        return mediaDirectory_;
    }

    /**
     * @brief Get the recordings subdirectory
     */
    juce::File getRecordingsDirectory() const;

    /**
     * @brief Get the renders subdirectory
     */
    juce::File getRendersDirectory() const;

    /**
     * @brief Get the bounces subdirectory
     */
    juce::File getBouncesDirectory() const;

    /**
     * @brief Delete temp media directories older than 7 days.
     * Call once at app launch.
     */
    static void cleanupStaleTempDirectories();

  private:
    ProjectManager();
    ~ProjectManager();

    void joinBackgroundThread();
    void timerCallback() override;
    void performAutosave();
    void deleteAutosaveFile();

    ProjectInfo currentProject_;
    juce::File currentFile_;
    juce::File mediaDirectory_;
    bool isDirty_ = false;
    bool isProjectOpen_ = false;
    bool autoSaveEnabled_ = true;

    std::vector<ProjectManagerListener*> listeners_;
    juce::String lastError_;
    std::thread loadThread_;

    void clearDirty();
    void notifyProjectOpened();
    void notifyProjectSaved();
    void notifyProjectClosed();
    void notifyDirtyStateChanged();

    /**
     * @brief Create a temp media directory for unsaved projects
     */
    void createTempMediaDirectory();

    /**
     * @brief Ensure recordings/, renders/, bounces/ subdirectories exist
     */
    static void ensureMediaSubdirectories(const juce::File& mediaRoot);

    /**
     * @brief Migrate media files from old directory to new, updating clip paths
     */
    void migrateMediaFiles(const juce::File& oldDir, const juce::File& newDir);
};

}  // namespace magda
