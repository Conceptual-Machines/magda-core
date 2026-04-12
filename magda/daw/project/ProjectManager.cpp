#include "ProjectManager.hpp"

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>

#include "../core/AutomationManager.hpp"
#include "../core/ClipManager.hpp"
#include "../core/TrackManager.hpp"
#include "serialization/ProjectSerializer.hpp"
#include "version.hpp"

namespace magda {

// Subdirectory names used by the media directory
static const char* const kRecordingsDir = "recordings";
static const char* const kRendersDir = "renders";
static const char* const kBouncesDir = "bounces";
static const char* const kTempRootDir = "MAGDA";
static const char* const kTempPrefix = "UnsavedProject_";
static constexpr int kStaleTempDays = 7;
static const char* const kAutosaveExtension = ".autosave";
static constexpr int kDefaultAutoSaveIntervalMs = 60000;

ProjectManager& ProjectManager::getInstance() {
    static ProjectManager instance;
    return instance;
}

ProjectManager::ProjectManager() {
    // Initialize with default project info
    currentProject_.name = "Untitled";
    currentProject_.version = MAGDA_VERSION;

    // Create temp media directory so recordings/renders have a home even before
    // the user explicitly creates or saves a project.
    createTempMediaDirectory();
    ensureMediaSubdirectories(mediaDirectory_);

    // Start auto-save timer
    startTimer(kDefaultAutoSaveIntervalMs);
}

ProjectManager::~ProjectManager() {
    stopTimer();
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
    currentProject_.version = MAGDA_VERSION;
    currentFile_ = juce::File();
    isProjectOpen_ = true;

    // Create temp media directory for unsaved project
    createTempMediaDirectory();
    ensureMediaSubdirectories(mediaDirectory_);

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
    // Capture live plugin state before serializing
    if (onBeforeSave)
        onBeforeSave();

    // Ensure the .mgd file lives inside a wrapper folder named after the project.
    // If the user picked /path/to/MyProject.mgd, wrap it as /path/to/MyProject/MyProject.mgd.
    // If it's already inside a matching folder, use it as-is.
    auto actualFile = file;
    auto projectName = file.getFileNameWithoutExtension();
    auto parentDir = file.getParentDirectory();

    if (parentDir.getFileName() != projectName) {
        auto wrapperDir = parentDir.getChildFile(projectName);
        if (!wrapperDir.createDirectory()) {
            lastError_ = "Failed to create project directory: " + wrapperDir.getFullPathName();
            return false;
        }
        actualFile = wrapperDir.getChildFile(file.getFileName());
    }

    // Prepare updated project info without mutating currentProject_ yet
    ProjectInfo newProject = currentProject_;
    newProject.filePath = actualFile.getFullPathName();
    newProject.name = projectName;
    newProject.touch();

    // Save to file
    if (!ProjectSerializer::saveToFile(actualFile, newProject)) {
        DBG("Failed to save project: " + ProjectSerializer::getLastError());
        lastError_ =
            "The project could not be saved. Please check disk space and file permissions.";
        return false;
    }

    // Commit updated state only after successful save
    const bool wasOpen = isProjectOpen_;
    currentProject_ = std::move(newProject);
    currentFile_ = actualFile;
    isProjectOpen_ = true;

    // Set up permanent media directory beside the project file
    auto oldMediaDir = mediaDirectory_;
    juce::String mediaDirName = actualFile.getFileNameWithoutExtension() + "_Media";
    mediaDirectory_ = actualFile.getParentDirectory().getChildFile(mediaDirName);
    ensureMediaSubdirectories(mediaDirectory_);

    // Migrate files from temp directory if needed
    if (oldMediaDir != juce::File() && oldMediaDir != mediaDirectory_ &&
        oldMediaDir.isDirectory()) {
        migrateMediaFiles(oldMediaDir, mediaDirectory_);
    }

    clearDirty();
    deleteAutosaveFile();

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

    // Check for autosave recovery
    auto fileToLoad = file;
    auto autosaveFile = getAutosaveFile(file);
    if (autosaveFile.existsAsFile()) {
        if (promptAutosaveRecovery(file)) {
            fileToLoad = autosaveFile;
        } else {
            autosaveFile.deleteFile();
        }
    }

    // Stage first (file I/O + parse + validate)
    StagedProjectData staged;
    if (!ProjectSerializer::loadAndStage(fileToLoad, staged)) {
        DBG("Failed to load project: " + ProjectSerializer::getLastError());
        lastError_ = "The project file could not be opened. It may be corrupted or from an "
                     "incompatible version.";
        return false;
    }

    // Set tempo/time sig/loop on the audio engine BEFORE committing tracks & clips,
    // so that audio engine clip sync uses the correct BPM.
    if (onBeforeCommit)
        onBeforeCommit(staged.info);

    // Commit staged data to singleton managers
    ProjectSerializer::commitStaged(staged);

    // Update state — always use the original file as the canonical project file
    currentProject_ = staged.info;
    currentProject_.filePath = file.getFullPathName();
    currentFile_ = file;
    isProjectOpen_ = true;

    // Set media directory beside project file
    juce::String mediaDirName = file.getFileNameWithoutExtension() + "_Media";
    mediaDirectory_ = file.getParentDirectory().getChildFile(mediaDirName);
    ensureMediaSubdirectories(mediaDirectory_);

    // If we recovered from autosave, mark dirty so the user can save properly
    if (fileToLoad != file) {
        isDirty_ = true;
        notifyDirtyStateChanged();
        autosaveFile.deleteFile();
    } else {
        clearDirty();
    }

    deleteAutosaveFile();
    notifyProjectOpened();

    if (onAfterLoad)
        onAfterLoad(currentProject_);

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

    // Check for autosave recovery (modal dialog on message thread)
    auto fileToLoad = file;
    auto autosaveFile = getAutosaveFile(file);
    bool recoveredFromAutosave = false;
    if (autosaveFile.existsAsFile()) {
        if (promptAutosaveRecovery(file)) {
            fileToLoad = autosaveFile;
            recoveredFromAutosave = true;
        } else {
            autosaveFile.deleteFile();
        }
    }

    // Capture file path for the background thread
    auto fileCopy = fileToLoad;

    // Join any previous background load before starting a new one
    joinBackgroundThread();

    auto originalFile = file;

    // Launch background thread for I/O + parse + staging
    loadThread_ = std::thread([fileCopy, originalFile, recoveredFromAutosave, onBeforeCommit,
                               onComplete, this]() {
        auto staged = std::make_shared<StagedProjectData>();
        bool ok = ProjectSerializer::loadAndStage(fileCopy, *staged);
        juce::String error;
        if (!ok) {
            DBG("Failed to load project: " + ProjectSerializer::getLastError());
            error = "The project file could not be opened. It may be corrupted or from an "
                    "incompatible version.";
        }

        // Bounce back to the message thread for commit + notification
        juce::MessageManager::callAsync([this, staged, ok, error, originalFile,
                                         recoveredFromAutosave, onBeforeCommit, onComplete]() {
            if (ok) {
                // Set tempo/time sig/loop BEFORE committing tracks & clips,
                // so that audio engine clip sync uses the correct BPM.
                if (onBeforeCommit)
                    onBeforeCommit(staged->info);

                ProjectSerializer::commitStaged(*staged);
                currentProject_ = staged->info;
                currentProject_.filePath = originalFile.getFullPathName();
                currentFile_ = originalFile;
                isProjectOpen_ = true;

                // Set media directory beside project file
                juce::String mediaDirName = originalFile.getFileNameWithoutExtension() + "_Media";
                mediaDirectory_ = originalFile.getParentDirectory().getChildFile(mediaDirName);
                ensureMediaSubdirectories(mediaDirectory_);

                if (recoveredFromAutosave) {
                    isDirty_ = true;
                    notifyDirtyStateChanged();
                    deleteAutosaveFile();
                } else {
                    clearDirty();
                }

                notifyProjectOpened();

                if (onAfterLoad)
                    onAfterLoad(currentProject_);
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

    deleteAutosaveFile();

    // Clear all project content from singleton managers
    TrackManager::getInstance().clearAllTracks();
    ClipManager::getInstance().clearAllClips();
    AutomationManager::getInstance().clearAll();

    // Reset state
    currentProject_ = ProjectInfo();
    currentFile_ = juce::File();
    mediaDirectory_ = juce::File();
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

// ============================================================================
// Media Directories
// ============================================================================

juce::File ProjectManager::getRecordingsDirectory() const {
    if (mediaDirectory_ == juce::File())
        return {};
    return mediaDirectory_.getChildFile(kRecordingsDir);
}

juce::File ProjectManager::getRendersDirectory() const {
    if (mediaDirectory_ == juce::File())
        return {};
    return mediaDirectory_.getChildFile(kRendersDir);
}

juce::File ProjectManager::getBouncesDirectory() const {
    if (mediaDirectory_ == juce::File())
        return {};
    return mediaDirectory_.getChildFile(kBouncesDir);
}

void ProjectManager::createTempMediaDirectory() {
    auto tempRoot =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(kTempRootDir);
    juce::String timestamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
    mediaDirectory_ = tempRoot.getChildFile(kTempPrefix + timestamp);
    mediaDirectory_.createDirectory();
}

void ProjectManager::ensureMediaSubdirectories(const juce::File& mediaRoot) {
    if (mediaRoot == juce::File())
        return;
    mediaRoot.getChildFile(kRecordingsDir).createDirectory();
    mediaRoot.getChildFile(kRendersDir).createDirectory();
    mediaRoot.getChildFile(kBouncesDir).createDirectory();
}

void ProjectManager::migrateMediaFiles(const juce::File& oldDir, const juce::File& newDir) {
    if (!oldDir.isDirectory() || oldDir == newDir)
        return;

    auto oldPath = oldDir.getFullPathName();
    auto newPath = newDir.getFullPathName();

    // Move files from each subdirectory
    const char* subdirs[] = {kRecordingsDir, kRendersDir, kBouncesDir};
    for (auto* subdir : subdirs) {
        auto srcDir = oldDir.getChildFile(subdir);
        auto dstDir = newDir.getChildFile(subdir);
        dstDir.createDirectory();

        if (srcDir.isDirectory()) {
            for (const auto& entry : juce::RangedDirectoryIterator(srcDir, false)) {
                auto srcFile = entry.getFile();
                auto dstFile = dstDir.getChildFile(srcFile.getFileName());
                srcFile.moveFileTo(dstFile);
            }
        }
    }

    // Update clip audio paths that reference the old media directory
    auto& clipManager = ClipManager::getInstance();
    auto updateClipPaths = [&](const std::vector<ClipInfo>& clips) {
        for (const auto& clipInfo : clips) {
            if (clipInfo.audioFilePath.isNotEmpty() && clipInfo.audioFilePath.startsWith(oldPath)) {
                auto* clip = clipManager.getClip(clipInfo.id);
                if (clip) {
                    clip->audioFilePath = clip->audioFilePath.replace(oldPath, newPath, false);
                }
            }
        }
    };

    updateClipPaths(clipManager.getArrangementClips());
    updateClipPaths(clipManager.getSessionClips());

    // Remove old temp directory if it's empty or under the temp root
    auto tempRoot =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(kTempRootDir);
    if (oldDir.isAChildOf(tempRoot)) {
        oldDir.deleteRecursively();
    }
}

void ProjectManager::cleanupStaleTempDirectories() {
    auto tempRoot =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(kTempRootDir);
    if (!tempRoot.isDirectory())
        return;

    auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::days(kStaleTempDays);

    for (const auto& entry :
         juce::RangedDirectoryIterator(tempRoot, false, "*", juce::File::findDirectories)) {
        auto dir = entry.getFile();
        if (dir.getFileName().startsWith(kTempPrefix)) {
            if (dir.getLastModificationTime() < cutoff) {
                dir.deleteRecursively();
            }
        }
    }
}

// ============================================================================
// Auto-Save
// ============================================================================

void ProjectManager::setAutoSaveEnabled(bool enabled, int intervalSeconds) {
    autoSaveEnabled_ = enabled;
    if (enabled) {
        startTimer(intervalSeconds * 1000);
    } else {
        stopTimer();
    }
}

void ProjectManager::timerCallback() {
    if (autoSaveEnabled_ && isDirty_ && isProjectOpen_) {
        performAutosave();
    }
}

void ProjectManager::performAutosave() {
    // Only autosave if we have a saved project file
    if (currentFile_.getFullPathName().isEmpty())
        return;

    // Capture live plugin state before serializing
    if (onBeforeSave)
        onBeforeSave();

    auto autosaveFile = currentFile_.getParentDirectory().getChildFile(currentFile_.getFileName() +
                                                                       kAutosaveExtension);

    ProjectInfo autosaveInfo = currentProject_;
    autosaveInfo.touch();

    ProjectSerializer::saveToFile(autosaveFile, autosaveInfo);
}

void ProjectManager::deleteAutosaveFile() {
    if (currentFile_.getFullPathName().isEmpty())
        return;

    auto autosaveFile = getAutosaveFile(currentFile_);
    if (autosaveFile.existsAsFile())
        autosaveFile.deleteFile();
}

juce::File ProjectManager::getAutosaveFile(const juce::File& projectFile) {
    auto f = projectFile.getParentDirectory().getChildFile(projectFile.getFileName() +
                                                           kAutosaveExtension);
    return f.existsAsFile() ? f : juce::File();
}

bool ProjectManager::promptAutosaveRecovery(const juce::File& projectFile) {
    auto autosaveFile = projectFile.getParentDirectory().getChildFile(projectFile.getFileName() +
                                                                      kAutosaveExtension);

    if (!autosaveFile.existsAsFile())
        return false;

    auto autosaveTime = autosaveFile.getLastModificationTime();
    auto projectTime = projectFile.getLastModificationTime();

    // Only offer recovery if the autosave is newer
    if (autosaveTime <= projectTime)
        return false;

    int result = juce::AlertWindow::showYesNoCancelBox(
        juce::AlertWindow::QuestionIcon, "Recover Autosaved Changes",
        "An autosave file was found that is newer than the project file.\n\n"
        "Project saved: " +
            projectTime.toString(true, true) +
            "\n"
            "Autosave saved: " +
            autosaveTime.toString(true, true) +
            "\n\n"
            "Would you like to recover the autosaved version?",
        "Recover", "Discard", "Cancel");

    return result == 1;
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
