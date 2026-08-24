#include "ProjectManager.hpp"

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <functional>
#include <map>
#include <unordered_set>

#include "../audio/AudioThumbnailManager.hpp"
#include "../core/AutomationManager.hpp"
#include "../core/ClipManager.hpp"
#include "../core/Config.hpp"
#include "../core/TempoUtils.hpp"
#include "../core/TrackManager.hpp"
#include "../core/UndoManager.hpp"
#include "../engine/AudioEngine.hpp"
#include "serialization/ProjectSerializer.hpp"
#include "version.hpp"

namespace magda {

// The media directory has three roots, each with a distinct meaning to the
// user: what they recorded, what MAGDA computed from the timeline, and what
// they brought in from outside. kMediaSubdirs is the single source of truth —
// creation and migration both read it, and every accessor below derives from
// it (#2170).
static const char* const kRecordingsDir = "recordings";
static const char* const kRendersDir = "renders";
static const char* const kImportedDir = "imported";
static const char* const kMediaSubdirs[] = {kRecordingsDir, kRendersDir, kImportedDir};

// Roots retired by #2170, and the surviving root each one folds into.
struct LegacyMediaSubdir {
    const char* from;
    const char* to;
};
static const LegacyMediaSubdir kLegacyMediaSubdirs[] = {
    {"bounces", kRendersDir},
    {"external-edits", kImportedDir},
    {"stems", kRendersDir},
};
static const char* const kTempRootDir = "MAGDA";
static const char* const kTempPrefix = "UnsavedProject_";
static constexpr int kStaleTempDays = 7;
static const char* const kAutosaveExtension = ".autosave";
static constexpr int kDefaultAutoSaveIntervalMs = 60000;
static const char* const kProjectChangedWhileLoading =
    "The current project changed while the new project was loading. "
    "Open the file again to avoid losing those changes.";

namespace {

juce::File getWritableTempRoot() {
    auto envTmp = juce::SystemStats::getEnvironmentVariable("TMPDIR", {});
    if (envTmp.isNotEmpty()) {
        auto envRoot = juce::File(envTmp);
        if (envRoot.createDirectory())
            return envRoot;
    }

    auto systemRoot = juce::File::getSpecialLocation(juce::File::tempDirectory);
    if (systemRoot.createDirectory())
        return systemRoot;

    auto privateTmp = juce::File("/private/tmp");
    if (privateTmp.createDirectory())
        return privateTmp;

    return systemRoot;
}

void resetTransportForProjectBoundary() {
    auto* audioEngine = TrackManager::getInstance().getAudioEngine();
    if (!audioEngine)
        return;

    audioEngine->stop();
    audioEngine->deactivateAllSessionClips();
    audioEngine->setLooping(false);
    audioEngine->locate(0.0);
}

// What to do with a file whose destination name is already taken. Never
// overwrite either way: clobbering would destroy a file some other clip still
// points at.
enum class OnCollision {
    // Land beside it under a unique name. For a move that writes its new paths
    // out in the same operation, so nothing may be left behind.
    Uniquify,
    // Leave the file where it is. For the legacy fold, whose new paths only
    // reach the .mgd when the user next saves — a file that stays put keeps the
    // path already in the .mgd valid, and keeps every file that did move
    // resolvable by name alone.
    Skip,
};

// Move every file under srcDir into dstDir, nested structure intact, recording
// where each one landed so the references to it can follow.
void moveMediaTree(const juce::File& srcDir, const juce::File& dstDir,
                   std::map<juce::String, juce::String>& moves, OnCollision onCollision) {
    if (!srcDir.isDirectory() || srcDir == dstDir)
        return;

    dstDir.createDirectory();

    // Snapshot the listing before moving anything — iterating a directory while
    // emptying it is not something every platform defines.
    for (const auto& srcFile : srcDir.findChildFiles(juce::File::findFiles, true)) {
        auto dstFile = dstDir.getChildFile(srcFile.getRelativePathFrom(srcDir));
        dstFile.getParentDirectory().createDirectory();
        if (dstFile.exists()) {
            if (onCollision == OnCollision::Skip)
                continue;
            dstFile = dstFile.getNonexistentSibling();
        }
        if (srcFile.moveFileTo(dstFile))
            moves[srcFile.getFullPathName()] = dstFile.getFullPathName();
    }

    // Only the empty skeleton is left once every file has moved out. A skipped
    // collision keeps the directory alive, and the next load tries it again.
    if (srcDir.findChildFiles(juce::File::findFiles, true).isEmpty())
        srcDir.deleteRecursively();
}

// Re-point everything that can name a media file — pooled clip sources, take
// paths, and sampler/drum-pad samples — through `resolve`, which returns the
// new path for a file that moved or an empty string for one that did not.
// Returns true if anything was re-pointed.
bool relinkMediaPaths(const std::function<juce::String(const juce::String&)>& resolve) {
    auto& clipManager = ClipManager::getInstance();
    auto& pool = SourcePool::getInstance();
    auto& thumbs = AudioThumbnailManager::getInstance();

    std::vector<ClipId> updatedClipIds;
    std::unordered_set<SourceId> relinkedSources;

    for (const auto& clipInfo : clipManager.getClips()) {
        if (!clipInfo.isAudio())
            continue;

        bool touched = false;

        // Sources are pooled per file, so a moved file is relinked once however
        // many clips reference it; the clip loop only decides which clips need
        // re-notifying.
        for (const auto& event : clipInfo.audio().events) {
            const auto oldPath = event.sourceFilePath();
            const auto newPath = resolve(oldPath);
            if (newPath.isEmpty())
                continue;

            touched = true;
            if (event.sourceId == INVALID_SOURCE_ID ||
                !relinkedSources.insert(event.sourceId).second)
                continue;

            const auto owner = pool.relink(event.sourceId, newPath);
            if (owner != INVALID_SOURCE_ID && owner != event.sourceId) {
                // The destination was already pooled under another source: move
                // the events across rather than leaving two entries claiming
                // one file.
                clipManager.repointEventsToSource(event.sourceId, owner);
            }
            thumbs.invalidateFile(oldPath);
            thumbs.invalidateFile(newPath);
        }

        if (auto* clip = clipManager.getClip(clipInfo.id)) {
            for (auto& take : clip->audio().takes) {
                const auto newPath = resolve(take.filePath);
                if (newPath.isEmpty())
                    continue;
                thumbs.invalidateFile(take.filePath);
                thumbs.invalidateFile(newPath);
                take.filePath = newPath;
                touched = true;
            }
        }

        if (touched)
            updatedClipIds.push_back(clipInfo.id);
    }

    if (!updatedClipIds.empty())
        clipManager.forceNotifyMultipleClipPropertiesChanged(updatedClipIds);

    // Collected samples live in imported/, so a media tree that moves takes the
    // samplers and drum pads pointing into it along too.
    bool relinkedSamplers = false;
    if (auto* audioEngine = TrackManager::getInstance().getAudioEngine()) {
        for (auto& reference : audioEngine->getSamplerMediaReferences()) {
            const auto newPath = resolve(reference.source.getFullPathName());
            if (newPath.isNotEmpty()) {
                reference.replace(juce::File(newPath));
                relinkedSamplers = true;
            }
        }
    }

    return !updatedClipIds.empty() || relinkedSamplers;
}

}  // namespace

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

    resetTransportForProjectBoundary();

    // Clear all project content from singleton managers. Source ids are
    // project-scoped like clip ids, so the pool empties here rather than in
    // clearAllClips, which the project LOAD path also calls after staging.
    TrackManager::getInstance().clearAllTracks();
    ClipManager::getInstance().clearAllClips();
    SourcePool::getInstance().clear();
    AutomationManager::getInstance().clearAll();

    // Reset project state
    currentProject_ = ProjectInfo();
    currentProject_.name = "Untitled";
    currentProject_.version = MAGDA_VERSION;
    // Seed per-project settings from the global new-project defaults.
    {
        auto& config = Config::getInstance();
        currentProject_.timelineLengthBars = config.getDefaultTimelineLengthBars();
        currentProject_.sampleRate = config.getRenderSampleRate();
        currentProject_.renderBitDepth = config.getRenderBitDepth();
        currentProject_.bounceBitDepth = config.getBounceBitDepth();
    }
    currentFile_ = juce::File();
    isProjectOpen_ = true;

    // Create temp media directory for unsaved project
    createTempMediaDirectory();
    ensureMediaSubdirectories(mediaDirectory_);

    // The old project's commands reference ids that no longer exist.
    UndoManager::getInstance().clearHistory();
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

    // Set up the target media directory before serializing so any clips that
    // point at the unsaved project's temp media folder are rewritten to the
    // durable project media folder in the saved .mgd.
    auto oldMediaDir = mediaDirectory_;
    juce::String mediaDirName = actualFile.getFileNameWithoutExtension() + "_Media";
    auto targetMediaDir = actualFile.getParentDirectory().getChildFile(mediaDirName);
    ensureMediaSubdirectories(targetMediaDir);

    if (oldMediaDir != juce::File() && oldMediaDir != targetMediaDir && oldMediaDir.isDirectory()) {
        migrateMediaFiles(oldMediaDir, targetMediaDir);
    }

    // Capture live plugin state before serializing. Runs after the migration
    // above, not before it: the migration re-points samplers and drum pads at
    // the files it just moved, and a state captured before that would freeze
    // the pre-move paths into the .mgd, leaving their samples missing when the
    // saved project is reopened.
    if (onBeforeSave)
        onBeforeSave();

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
    mediaDirectory_ = targetMediaDir;

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

    resetTransportForProjectBoundary();

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

    // The previous project's undo stack cannot be applied to this one — its
    // commands reference track/clip ids that are gone.
    UndoManager::getInstance().clearHistory();
    clearDirty();

    // Projects saved before #2170 still have the retired media roots on disk.
    // Runs after clearDirty() so the dirty flag it raises survives.
    foldLegacyMediaDirectories(mediaDirectory_);

    // If we recovered from autosave, mark dirty so the user can save properly
    if (fileToLoad != file) {
        markDirty();
        autosaveFile.deleteFile();
    }

    deleteAutosaveFile();
    notifyProjectOpened();

    if (onAfterLoad)
        onAfterLoad(currentProject_);

    return true;
}

bool ProjectManager::exportDawProject(const juce::File& file) {
    if (onBeforeSave)
        onBeforeSave();

    ProjectInfo exportInfo = currentProject_;
    if (exportInfo.name.isEmpty())
        exportInfo.name = file.getFileNameWithoutExtension();
    exportInfo.touch();

    if (!ProjectSerializer::exportToDawProject(file, exportInfo)) {
        DBG("Failed to export DAWproject: " + ProjectSerializer::getLastError());
        lastError_ = ProjectSerializer::getLastError();
        return false;
    }

    return true;
}

void ProjectManager::importDawProjectAsync(
    const juce::File& file, std::function<void(const ProjectInfo&)> onBeforeCommit,
    std::function<void(bool, const juce::String&)> onComplete) {
    // Pre-flight checks on the message thread.
    if (isDirty_ && !showUnsavedChangesDialog()) {
        if (onComplete)
            onComplete(false, {});  // empty error = user cancelled
        return;
    }

    if (!file.existsAsFile()) {
        if (onComplete)
            onComplete(false, "File does not exist: " + file.getFullPathName());
        return;
    }

    // Set up the project media directory first so embedded audio extracts into
    // it (the "imported" subdir) and persists with the project, rather than into
    // a throwaway temp folder. Done on the message thread before staging so the
    // background thread has a valid extraction target.
    createTempMediaDirectory();
    ensureMediaSubdirectories(mediaDirectory_);
    const auto importedDir = getImportedDirectory();

    // Join any previous background load before starting a new one.
    joinBackgroundThread();

    const auto startingRevision = mutationRevision_;
    auto fileCopy = file;
    loadThread_ =
        std::thread([fileCopy, importedDir, startingRevision, onBeforeCommit, onComplete, this]() {
            auto staged = std::make_shared<StagedProjectData>();
            const bool ok =
                ProjectSerializer::loadDawProjectAndStage(fileCopy, *staged, importedDir);
            juce::String error;
            if (!ok) {
                DBG("Failed to import DAWproject: " + ProjectSerializer::getLastError());
                error = ProjectSerializer::getLastError();
            }

            // Bounce back to the message thread for commit + notification.
            juce::MessageManager::callAsync(
                [this, staged, ok, error, startingRevision, onBeforeCommit, onComplete]() {
                    if (ok) {
                        if (mutationRevision_ != startingRevision) {
                            if (onComplete)
                                onComplete(false, kProjectChangedWhileLoading);
                            return;
                        }

                        resetTransportForProjectBoundary();

                        if (onBeforeCommit)
                            onBeforeCommit(staged->info);

                        ProjectSerializer::commitStaged(*staged);

                        currentProject_ = staged->info;
                        currentProject_.filePath = {};
                        currentFile_ = juce::File();
                        isProjectOpen_ = true;

                        // An import has never been saved as a .mgd, so it starts dirty
                        // — but the previous project's undo stack still has to go.
                        UndoManager::getInstance().clearHistory();
                        clearDirty();
                        markDirty();
                        notifyProjectOpened();

                        if (onAfterLoad)
                            onAfterLoad(currentProject_);
                    }

                    if (onComplete)
                        onComplete(ok, error);
                });
        });
}

void ProjectManager::loadProjectAsync(const juce::File& file,
                                      std::function<void(const ProjectInfo&)> onBeforeCommit,
                                      std::function<void(bool, const juce::String&)> onComplete) {
    // Pre-flight checks on the message thread
    if (isDirty_ && !showUnsavedChangesDialog()) {
        if (onComplete)
            onComplete(false, {});  // empty error = user cancelled, not a failure
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

    const auto startingRevision = mutationRevision_;
    auto originalFile = file;

    // Launch background thread for I/O + parse + staging
    loadThread_ = std::thread([fileCopy, originalFile, recoveredFromAutosave, onBeforeCommit,
                               onComplete, startingRevision, this]() {
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
                                         recoveredFromAutosave, startingRevision, onBeforeCommit,
                                         onComplete]() {
            if (ok) {
                if (mutationRevision_ != startingRevision) {
                    if (onComplete)
                        onComplete(false, kProjectChangedWhileLoading);
                    return;
                }

                resetTransportForProjectBoundary();

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

                // The previous project's undo stack cannot be applied to this
                // one — its commands reference ids that are gone.
                UndoManager::getInstance().clearHistory();
                clearDirty();

                // Projects saved before #2170 still have the retired media
                // roots on disk. Runs after clearDirty() so the dirty flag it
                // raises survives.
                foldLegacyMediaDirectories(mediaDirectory_);

                if (recoveredFromAutosave) {
                    markDirty();
                    deleteAutosaveFile();
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

    resetTransportForProjectBoundary();

    // Clear all project content from singleton managers. Source ids are
    // project-scoped like clip ids, so the pool empties here rather than in
    // clearAllClips, which the project LOAD path also calls after staging.
    TrackManager::getInstance().clearAllTracks();
    ClipManager::getInstance().clearAllClips();
    SourcePool::getInstance().clear();
    AutomationManager::getInstance().clearAll();

    // Reset state
    currentProject_ = ProjectInfo();
    currentFile_ = juce::File();
    mediaDirectory_ = juce::File();
    isProjectOpen_ = false;
    UndoManager::getInstance().clearHistory();
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
    const double clampedTempo = clampBpm(tempo);
    if (currentProject_.tempo != clampedTempo) {
        currentProject_.tempo = clampedTempo;
        markDirty();
        for (auto* listener : listeners_)
            listener->projectPropertiesChanged();
    }
}

void ProjectManager::setTimeSignature(int numerator, int denominator) {
    const int clampedNumerator = clampTimeSignatureValue(numerator);
    const int clampedDenominator = clampTimeSignatureValue(denominator);
    if (currentProject_.timeSignatureNumerator != clampedNumerator ||
        currentProject_.timeSignatureDenominator != clampedDenominator) {
        currentProject_.timeSignatureNumerator = clampedNumerator;
        currentProject_.timeSignatureDenominator = clampedDenominator;
        markDirty();
        for (auto* listener : listeners_)
            listener->projectPropertiesChanged();
    }
}

void ProjectManager::setLoopSettings(bool enabled, double startBeats, double endBeats) {
    if (currentProject_.loopEnabled != enabled || currentProject_.loopStartBeats != startBeats ||
        currentProject_.loopEndBeats != endBeats) {
        currentProject_.loopEnabled = enabled;
        currentProject_.loopStartBeats = startBeats;
        currentProject_.loopEndBeats = endBeats;
        markDirty();
        for (auto* listener : listeners_)
            listener->projectPropertiesChanged();
    }
}

void ProjectManager::markDirty() {
    ++mutationRevision_;
    if (undoableMutationDepth_ == 0)
        externalDirty_ = true;
    refreshDirtyState();
}

void ProjectManager::clearDirty() {
    externalDirty_ = false;
    UndoManager::getInstance().markCurrentStateSaved();
    refreshDirtyState();
}

void ProjectManager::beginUndoableMutation() {
    ++undoableMutationDepth_;
}

void ProjectManager::endUndoableMutation() {
    jassert(undoableMutationDepth_ > 0);
    if (undoableMutationDepth_ > 0)
        --undoableMutationDepth_;

    // A command just changed project state. Most commands never call
    // markDirty() themselves, so the revision has to move here or an in-flight
    // background load would not notice edits made while it was parsing.
    ++mutationRevision_;
}

void ProjectManager::setUndoHistoryDirty(bool dirty) {
    // Deliberately no revision bump. This runs from the dirty-state
    // bookkeeping, which clearDirty() drives on every save, so counting it as a
    // mutation would make saving during a background load abandon that load
    // and report that the project changed underneath it.
    undoHistoryDirty_ = dirty;
    refreshDirtyState();
}

ProjectManager::UndoableMutationScope::UndoableMutationScope() {
    ProjectManager::getInstance().beginUndoableMutation();
}

ProjectManager::UndoableMutationScope::~UndoableMutationScope() {
    ProjectManager::getInstance().endUndoableMutation();
}

void ProjectManager::refreshDirtyState() {
    const bool shouldBeDirty = externalDirty_ || undoHistoryDirty_;
    if (isDirty_ != shouldBeDirty) {
        isDirty_ = shouldBeDirty;
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

juce::File ProjectManager::getImportedDirectory() const {
    if (mediaDirectory_ == juce::File())
        return {};
    return mediaDirectory_.getChildFile(kImportedDir);
}

void ProjectManager::createTempMediaDirectory() {
    auto tempRoot = getWritableTempRoot().getChildFile(kTempRootDir);
    tempRoot.createDirectory();

    if (!tempRoot.isDirectory()) {
        tempRoot = juce::File("/tmp").getChildFile(kTempRootDir);
        tempRoot.createDirectory();
    }

    juce::String timestamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
    mediaDirectory_ = tempRoot.getNonexistentChildFile(kTempPrefix + timestamp, {});
    mediaDirectory_.createDirectory();

    if (!mediaDirectory_.isDirectory()) {
        auto fallbackRoot = juce::File("/tmp").getChildFile(kTempRootDir);
        fallbackRoot.createDirectory();
        mediaDirectory_ = fallbackRoot.getNonexistentChildFile(kTempPrefix + timestamp, {});
        mediaDirectory_.createDirectory();
    }
}

void ProjectManager::ensureMediaSubdirectories(const juce::File& mediaRoot) {
    if (mediaRoot == juce::File())
        return;
    for (auto* subdir : kMediaSubdirs) {
        mediaRoot.getChildFile(subdir).createDirectory();
    }
}

void ProjectManager::migrateMediaFiles(const juce::File& oldDir, const juce::File& newDir) {
    if (!oldDir.isDirectory() || oldDir == newDir)
        return;

    // Uniquify rather than skip: the .mgd is written from the relinked model at
    // the end of this same save, so a file that lands under a new name is
    // recorded correctly, and nothing may be stranded in the directory being
    // left behind.
    std::map<juce::String, juce::String> moves;
    for (auto* subdir : kMediaSubdirs)
        moveMediaTree(oldDir.getChildFile(subdir), newDir.getChildFile(subdir), moves,
                      OnCollision::Uniquify);

    // A Save-As from a project opened before #2170 can still be carrying the
    // retired roots, so fold them on the way across rather than stranding them.
    for (const auto& legacy : kLegacyMediaSubdirs)
        moveMediaTree(oldDir.getChildFile(legacy.from), newDir.getChildFile(legacy.to), moves,
                      OnCollision::Uniquify);

    relinkMediaPaths([&moves](const juce::String& path) -> juce::String {
        const auto it = moves.find(path);
        return it == moves.end() ? juce::String() : it->second;
    });

    // Remove old temp directory if it's empty or under the temp root
    auto tempRoot =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(kTempRootDir);
    if (oldDir.isAChildOf(tempRoot)) {
        oldDir.deleteRecursively();
    }
}

void ProjectManager::foldLegacyMediaDirectories(const juce::File& mediaRoot) {
    if (mediaRoot == juce::File() || !mediaRoot.isDirectory())
        return;

    std::map<juce::String, juce::String> moves;
    for (const auto& legacy : kLegacyMediaSubdirs)
        moveMediaTree(mediaRoot.getChildFile(legacy.from), mediaRoot.getChildFile(legacy.to), moves,
                      OnCollision::Skip);

    const auto rootPath = mediaRoot.getFullPathName();
    const auto resolve = [&moves, &rootPath, &mediaRoot](const juce::String& path) -> juce::String {
        const auto it = moves.find(path);
        if (it != moves.end())
            return it->second;

        // A project folded on an earlier load but never saved still names the
        // retired folders in its .mgd, so follow the name into the replacement
        // root. That is unambiguous only because the fold skips collisions: a
        // file that would have had to share a name never moved, so a name that
        // has left the retired root and is present in the replacement can only
        // be the same file. Uniquifying here instead would let a stale path
        // land on an unrelated file that merely shares its name.
        for (const auto& legacy : kLegacyMediaSubdirs) {
            const auto prefix = rootPath + juce::File::getSeparatorString() + legacy.from +
                                juce::File::getSeparatorString();
            if (!path.startsWith(prefix))
                continue;
            if (juce::File(path).existsAsFile())
                return {};  // Skipped by the fold — it still lives where it says.

            const auto moved =
                mediaRoot.getChildFile(legacy.to).getChildFile(path.substring(prefix.length()));
            if (moved.existsAsFile())
                return moved.getFullPathName();
        }
        return {};
    };

    // The paths in the .mgd on disk still name folders that just went away, so
    // the project genuinely differs from its file until it is saved again.
    if (relinkMediaPaths(resolve))
        markDirty();
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
        // Cancel — abort the operation. Empty lastError_ so callers can
        // distinguish "user cancelled" from "save actually failed" and
        // suppress the spurious error dialog they would otherwise show.
        lastError_.clear();
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
            // Untitled project: run the Save-As file picker inline rather than
            // dead-ending with a "use Save As first" prompt and aborting the
            // outer New/Open/Close flow. JUCE_MODAL_LOOPS_PERMITTED is on for
            // this build, so a modal FileChooser is fine here.
            juce::FileChooser chooser(
                "Save Project As",
                juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.mgd", true);
            if (!chooser.browseForFileToSave(true)) {
                // User cancelled the chooser — treat as overall cancel.
                lastError_.clear();
                return false;
            }
            auto file = chooser.getResult();
            if (!file.getFileExtension().equalsIgnoreCase(".mgd"))
                file = file.withFileExtension("mgd");
            if (!saveProjectAs(file)) {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                       "Save Failed",
                                                       "Could not save project: " + lastError_);
                return false;
            }
        }
    }

    // result == 2: Don't Save — proceed without saving
    return true;
}

}  // namespace magda
