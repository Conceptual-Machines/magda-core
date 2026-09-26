#include "ProjectManager.hpp"

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <unordered_set>

#include "../audio/AudioThumbnailManager.hpp"
#include "../audio/sampling/SamplerMedia.hpp"
#include "../core/AppPaths.hpp"
#include "../core/AutomationManager.hpp"
#include "../core/ClipManager.hpp"
#include "../core/Config.hpp"
#include "../core/TempoUtils.hpp"
#include "../core/TrackManager.hpp"
#include "../core/UndoManager.hpp"
#include "../engine/AudioEngine.hpp"
#include "engine/AudioEngineChoice.hpp"
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
// Written into the media root when a fold actually moves something, so a later
// load can tell where a file went instead of guessing from its name (#2170).
static const char* const kMediaMovesFile = ".magda-media-moves.json";
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

std::vector<juce::File> getTempRoots() {
    std::vector<juce::File> roots;
    const auto addUnique = [&roots](const juce::File& root) {
        if (root != juce::File() && std::find(roots.begin(), roots.end(), root) == roots.end())
            roots.push_back(root);
    };

    addUnique(getWritableTempRoot());
    addUnique(juce::File::getSpecialLocation(juce::File::tempDirectory));
    addUnique(juce::File("/tmp"));
    addUnique(juce::File("/private/tmp"));
    return roots;
}

bool isManagedTempMediaDirectory(const juce::File& directory) {
    if (!directory.isDirectory() || !directory.getFileName().startsWith(kTempPrefix))
        return false;

    for (const auto& root : getTempRoots()) {
        if (directory.isAChildOf(root.getChildFile(kTempRootDir)))
            return true;
    }
    return false;
}

juce::File createTempMediaDirectoryOnDisk() {
    auto tempRoot = getWritableTempRoot().getChildFile(kTempRootDir);
    tempRoot.createDirectory();

    if (!tempRoot.isDirectory()) {
        tempRoot = juce::File("/tmp").getChildFile(kTempRootDir);
        tempRoot.createDirectory();
    }

    const auto timestamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
    auto directory = tempRoot.getNonexistentChildFile(kTempPrefix + timestamp, {});
    directory.createDirectory();

    if (!directory.isDirectory()) {
        auto fallbackRoot = juce::File("/tmp").getChildFile(kTempRootDir);
        fallbackRoot.createDirectory();
        directory = fallbackRoot.getNonexistentChildFile(kTempPrefix + timestamp, {});
        directory.createDirectory();
    }
    return directory;
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

// Paths are recorded relative to the media root, with '/' separators, so the
// record survives the project folder being moved, renamed, or opened on another
// platform. Returns an empty string for a path outside the tree.
juce::String mediaRelativePath(const juce::File& mediaRoot, const juce::File& file) {
    if (!file.isAChildOf(mediaRoot))
        return {};
    return file.getRelativePathFrom(mediaRoot).replaceCharacter('\\', '/');
}

std::map<juce::String, juce::String> readMediaMoves(const juce::File& mediaRoot) {
    std::map<juce::String, juce::String> record;
    const auto file = mediaRoot.getChildFile(kMediaMovesFile);
    if (!file.existsAsFile())
        return record;

    const auto parsed = juce::JSON::parse(file.loadFileAsString());
    if (auto* object = parsed.getDynamicObject())
        for (const auto& entry : object->getProperties())
            record[entry.name.toString()] = entry.value.toString();
    return record;
}

void writeMediaMoves(const juce::File& mediaRoot,
                     const std::map<juce::String, juce::String>& record) {
    auto object = std::make_unique<juce::DynamicObject>();
    for (const auto& [from, to] : record)
        object->setProperty(from, to);
    mediaRoot.getChildFile(kMediaMovesFile)
        .replaceWithText(juce::JSON::toString(juce::var(object.release())));
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

// Move or copy every file under srcDir into dstDir, nested structure intact,
// recording where each one landed so the references to it can follow.
void moveMediaTree(const juce::File& srcDir, const juce::File& dstDir,
                   std::map<juce::String, juce::String>& moves, OnCollision onCollision,
                   ProjectManager::MediaTransfer transfer) {
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
        const auto landed = transfer == ProjectManager::MediaTransfer::Copy
                                ? srcFile.copyFileTo(dstFile)
                                : srcFile.moveFileTo(dstFile);
        if (landed)
            moves[srcFile.getFullPathName()] = dstFile.getFullPathName();
    }

    if (transfer == ProjectManager::MediaTransfer::Copy)
        return;

    // Only the empty skeleton is left once every file has moved out. A skipped
    // collision keeps the directory alive, and the next load tries it again.
    if (srcDir.findChildFiles(juce::File::findFiles, true).isEmpty())
        srcDir.deleteRecursively();
}

// Re-point everything that can name a media file — pooled clip sources, take
// paths, and sampler/drum-pad samples — through `resolve`, which returns the
// new path for a file that moved or an empty string for one that did not.
// Returns the distinct old paths that were actually re-pointed.
std::set<juce::String> relinkMediaPaths(
    const std::function<juce::String(const juce::String&)>& resolve) {
    auto& clipManager = ClipManager::getInstance();
    auto& pool = SourcePool::getInstance();
    auto& thumbs = AudioThumbnailManager::getInstance();

    std::vector<ClipId> updatedClipIds;
    std::unordered_set<SourceId> relinkedSources;
    std::set<juce::String> relinkedPaths;

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

            if (event.sourceId == INVALID_SOURCE_ID)
                continue;

            if (!relinkedSources.insert(event.sourceId).second) {
                touched = true;
                relinkedPaths.insert(oldPath);
                continue;
            }

            const auto owner = pool.relink(event.sourceId, newPath);
            if (owner == INVALID_SOURCE_ID)
                continue;
            if (owner != INVALID_SOURCE_ID && owner != event.sourceId) {
                // The destination was already pooled under another source: move
                // the events across rather than leaving two entries claiming
                // one file.
                clipManager.repointEventsToSource(event.sourceId, owner);
            }
            thumbs.invalidateFile(oldPath);
            thumbs.invalidateFile(newPath);
            touched = true;
            relinkedPaths.insert(oldPath);
        }

        if (auto* clip = clipManager.getClip(clipInfo.id)) {
            for (auto& take : clip->audio().takes) {
                const auto oldPath = take.filePath;
                const auto newPath = resolve(oldPath);
                if (newPath.isEmpty())
                    continue;
                thumbs.invalidateFile(oldPath);
                thumbs.invalidateFile(newPath);
                take.filePath = newPath;
                touched = true;
                relinkedPaths.insert(oldPath);
            }
        }

        if (touched)
            updatedClipIds.push_back(clipInfo.id);
    }

    if (!updatedClipIds.empty())
        clipManager.forceNotifyMultipleClipPropertiesChanged(updatedClipIds);

    // Collected samples live in imported/, so a media tree that moves takes the
    // samplers and drum pads pointing into it along too.
    for (auto& reference : magda::SamplerMedia::getInstance().references()) {
        const auto oldPath = reference.source.getFullPathName();
        const auto newPath = resolve(oldPath);
        if (newPath.isNotEmpty()) {
            reference.replace(juce::File(newPath));
            relinkedPaths.insert(oldPath);
        }
    }

    return relinkedPaths;
}

juce::StringArray pathComponents(juce::String path) {
    path = path.replaceCharacter('\\', '/');
    juce::StringArray components;
    components.addTokens(path, "/", {});
    components.removeEmptyStrings();
    return components;
}

int commonPathSuffixLength(const juce::String& first, const juce::String& second) {
    const auto firstParts = pathComponents(first);
    const auto secondParts = pathComponents(second);
    int score = 0;
    for (int firstIndex = firstParts.size() - 1, secondIndex = secondParts.size() - 1;
         firstIndex >= 0 && secondIndex >= 0 &&
         firstParts[firstIndex].equalsIgnoreCase(secondParts[secondIndex]);
         --firstIndex, --secondIndex) {
        ++score;
    }
    return score;
}

}  // namespace

ProjectCreationSettings ProjectManager::captureCreationSettingsFromConfig() {
    const auto& config = Config::getInstance();
    ProjectCreationSettings settings;
    settings.timelineLengthBars = config.getDefaultTimelineLengthBars();
    settings.defaults.zoomViewBars = config.getDefaultZoomViewBars();
    settings.defaults.autoCrossfade = config.getAutoCrossfadeByDefault();
    settings.defaults.overlapPlaysBoth = config.getClipOverlapPlaysBoth();
    settings.defaults.chordPreview = config.getChordPreviewOnByDefault();
    settings.defaults.postFxPostFader = config.getPostFxPostFaderByDefault();
    settings.defaults.clipColourMode = config.getClipColourMode();

    const auto customPalette = config.getTrackColourPalette();
    settings.defaults.colourPalette.reserve(settings.defaults.colourPalette.size() +
                                            customPalette.size());
    for (const auto& entry : customPalette)
        settings.defaults.colourPalette.push_back({entry.colour, juce::String(entry.name)});

    return settings;
}

void ProjectManager::seedProjectFromConfig(ProjectInfo& project) {
    const auto& config = Config::getInstance();
    auto settings = captureCreationSettingsFromConfig();
    project.timelineLengthBars = settings.timelineLengthBars;
    project.defaults = std::move(settings.defaults);

    project.sampleRate = config.getRenderSampleRate();
    project.renderBitDepth = config.getRenderBitDepth();
    project.bounceBitDepth = config.getBounceBitDepth();

    // Credits describing the person rather than the work. Only the fields
    // flagged for it are seeded - a stored default for the title or the year
    // would be wrong in every project after the first.
    const auto& metadataDefaults = config.getProjectMetadataDefaults();
    for (const auto& field : kProjectMetadataFields) {
        if (!field.seededFromDefaults)
            continue;
        const auto entry = metadataDefaults.find(field.key);
        if (entry != metadataDefaults.end())
            project.metadata.*field.member = juce::String(entry->second);
    }
}

void ProjectManager::applyConfigPaletteToCurrentProject() {
    if (!isProjectOpen_)
        return;

    auto palette = captureCreationSettingsFromConfig().defaults.colourPalette;
    if (currentProject_.defaults.colourPalette == palette)
        return;
    currentProject_.defaults.colourPalette = std::move(palette);
    markDirty();
}

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

    startAutoSaveTimer(kDefaultAutoSaveIntervalMs);
}

/**
 * @brief Start autosaving, if there is a message thread to autosave on.
 *
 * A headless host reaches this singleton too, and has no message loop: the
 * test binary does, through the timeline's tempo sync. Making a timer there
 * costs the process its exit, because JUCE tears its timer thread down after
 * the message manager, waiting on it without a deadline.
 */
void ProjectManager::startAutoSaveTimer(int intervalMs) {
    if (juce::MessageManager::getInstanceWithoutCreating() == nullptr)
        return;

    if (autoSaveTimer_ == nullptr)
        autoSaveTimer_ = std::make_unique<juce::TimedCallback>([this] { autoSaveTick(); });

    autoSaveTimer_->startTimer(intervalMs);
}

ProjectManager::~ProjectManager() {
    autoSaveTimer_.reset();
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

    beginProjectTeardown();

    // The user either saved or explicitly abandoned the previous project.
    deleteAutosaveFile();

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
    seedProjectFromConfig(currentProject_);
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

void ProjectManager::seedCurrentProjectFromConfig() {
    if (!isProjectOpen_) {
        auto settings = captureCreationSettingsFromConfig();
        currentProject_.timelineLengthBars = settings.timelineLengthBars;
        currentProject_.defaults = std::move(settings.defaults);
    }
}

bool ProjectManager::saveProject() {
    if (currentFile_.getFullPathName().isEmpty() ||
        !currentFile_.getParentDirectory().isDirectory()) {
        lastError_ = "No file path set. Use Save As.";
        return false;
    }

    return saveProjectAs(currentFile_);
}

bool ProjectManager::saveProjectAs(const juce::File& file, MediaTransfer transfer) {
    const bool wasUntitled = currentFile_.getFullPathName().isEmpty();

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
        migrateMediaFiles(oldMediaDir, targetMediaDir, transfer);
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
    newProject.autosaveMediaDirectory.clear();

    // Which engine wrote it, so opening it under the other one knows whether
    // this project has been through that engine's migration (#2437).
    newProject.savedWithEngine = settingWordFor(chosenAudioEngine());
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
    currentFile_ = std::move(actualFile);
    isProjectOpen_ = true;
    mediaDirectory_ = std::move(targetMediaDir);

    clearDirty();
    deleteAutosaveFile();
    if (wasUntitled)
        discardUntitledAutosave();

    if (!wasOpen) {
        notifyProjectOpened();
    } else {
        notifyProjectSaved();
    }

    return true;
}

bool ProjectManager::loadProject(const juce::File& file,
                                 const std::function<void(const ProjectInfo&)>& onBeforeCommit) {
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

    commitStagedProject(staged, file, fileToLoad != file, onBeforeCommit);

    return true;
}

void ProjectManager::commitStagedProject(
    StagedProjectData& staged, const juce::File& file, bool recoveredFromAutosave,
    const std::function<void(const ProjectInfo&)>& onBeforeCommit) {
    beginProjectTeardown();
    deleteAutosaveFile();

    // Set tempo/time sig/loop on the audio engine BEFORE committing tracks & clips,
    // so that audio engine clip sync uses the correct BPM.
    if (onBeforeCommit)
        onBeforeCommit(staged.info);

    ProjectSerializer::commitStaged(staged);

    // Always the original file as the canonical project file, even when recovered from autosave.
    currentProject_ = staged.info;
    currentProject_.filePath = file.getFullPathName();
    currentFile_ = file;
    isProjectOpen_ = true;

    juce::String mediaDirName = file.getFileNameWithoutExtension() + "_Media";
    mediaDirectory_ = file.getParentDirectory().getChildFile(mediaDirName);
    ensureMediaSubdirectories(mediaDirectory_);

    // The previous project's undo stack references track and clip ids that are gone.
    UndoManager::getInstance().clearHistory();
    clearDirty();

    // Projects saved before #2170 still have the retired media roots on disk.
    // Runs after clearDirty() so the dirty flag it raises survives.
    foldLegacyMediaDirectories(mediaDirectory_);

    if (recoveredFromAutosave)
        markDirty();

    deleteAutosaveFile();
    notifyProjectOpened();

    if (onAfterLoad)
        onAfterLoad(currentProject_);
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
    const juce::File& file, const std::function<void(const ProjectInfo&)>& onBeforeCommit,
    const std::function<void(bool, const juce::String&)>& onComplete) {
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

    // Stage embedded media in a new directory without replacing the current
    // project's media root. The new root becomes active only after the import
    // has fully staged and its revision check succeeds.
    const auto importMediaDirectory = createTempMediaDirectoryOnDisk();
    ensureMediaSubdirectories(importMediaDirectory);
    const auto importedDir = importMediaDirectory.getChildFile(kImportedDir);

    // Join any previous background load before starting a new one.
    joinBackgroundThread();

    // Config is mutable on the message thread. Never read it from the loader.
    const auto creationSettings = captureCreationSettingsFromConfig();

    const auto startingRevision = mutationRevision_;
    const auto& fileCopy = file;
    loadThread_ = std::thread([fileCopy, importedDir, importMediaDirectory, startingRevision,
                               creationSettings, onBeforeCommit, onComplete, this]() {
        auto staged = std::make_shared<StagedProjectData>();
        const bool ok = ProjectSerializer::loadDawProjectAndStage(fileCopy, *staged, importedDir,
                                                                  creationSettings);
        juce::String error;
        if (!ok) {
            DBG("Failed to import DAWproject: " + ProjectSerializer::getLastError());
            error = ProjectSerializer::getLastError();
        }

        // Bounce back to the message thread for commit + notification.
        juce::MessageManager::callAsync([this, staged, ok, error, importMediaDirectory,
                                         startingRevision, onBeforeCommit, onComplete]() {
            if (ok) {
                if (mutationRevision_ != startingRevision) {
                    importMediaDirectory.deleteRecursively();
                    if (onComplete)
                        onComplete(false, kProjectChangedWhileLoading);
                    return;
                }

                beginProjectTeardown();
                deleteAutosaveFile();

                if (onBeforeCommit)
                    onBeforeCommit(staged->info);

                ProjectSerializer::commitStaged(*staged);

                currentProject_ = staged->info;
                currentProject_.filePath = {};
                currentFile_ = juce::File();
                mediaDirectory_ = importMediaDirectory;
                isProjectOpen_ = true;

                // An import has never been saved as a .mgd, so it starts dirty
                // — but the previous project's undo stack still has to go.
                UndoManager::getInstance().clearHistory();
                clearDirty();
                markDirty();
                notifyProjectOpened();

                if (onAfterLoad)
                    onAfterLoad(currentProject_);
            } else {
                importMediaDirectory.deleteRecursively();
            }

            if (onComplete)
                onComplete(ok, error);
        });
    });
}

void ProjectManager::loadProjectAsync(
    const juce::File& file, const std::function<void(const ProjectInfo&)>& onBeforeCommit,
    const std::function<void(bool, const juce::String&)>& onComplete) {
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
            fileToLoad = std::move(autosaveFile);
            recoveredFromAutosave = true;
        } else {
            autosaveFile.deleteFile();
        }
    }

    // Capture file path for the background thread
    auto fileCopy = fileToLoad;

    // Join any previous background load before starting a new one
    joinBackgroundThread();

    // Snapshot mutable Preferences before launching the background loader.
    const auto creationSettings = captureCreationSettingsFromConfig();

    const auto startingRevision = mutationRevision_;
    const auto& originalFile = file;

    // Launch background thread for I/O + parse + staging
    loadThread_ = std::thread([fileCopy, originalFile, recoveredFromAutosave, creationSettings,
                               onBeforeCommit, onComplete, startingRevision, this]() {
        auto staged = std::make_shared<StagedProjectData>();
        bool ok = ProjectSerializer::loadAndStage(fileCopy, *staged, creationSettings);
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

                commitStagedProject(*staged, originalFile, recoveredFromAutosave, onBeforeCommit);
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

    beginProjectTeardown();
    deleteAutosaveFile();

    // Clear all project content from singleton managers. Source ids are
    // project-scoped like clip ids, so the pool empties here rather than in
    // clearAllClips, which the project LOAD path also calls after staging.
    TrackManager::getInstance().clearAllTracks();
    ClipManager::getInstance().clearAllClips();
    SourcePool::getInstance().clear();
    AutomationManager::getInstance().clearAll();

    // Reset state
    currentProject_ = ProjectInfo();
    isProjectOpen_ = false;
    seedCurrentProjectFromConfig();
    currentFile_ = juce::File();
    mediaDirectory_ = juce::File();
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

SceneId ProjectManager::appendSessionScene() {
    const auto index = static_cast<int>(currentProject_.scenes.size());
    const auto id = currentProject_.nextSceneId++;
    currentProject_.scenes.push_back(makeDefaultProjectScene(id, index));
    markDirty();
    for (auto* listener : listeners_)
        listener->projectPropertiesChanged();
    return id;
}

bool ProjectManager::removeLastSessionScene() {
    if (currentProject_.scenes.size() <= 1)
        return false;
    currentProject_.scenes.pop_back();
    markDirty();
    for (auto* listener : listeners_)
        listener->projectPropertiesChanged();
    return true;
}

void ProjectManager::replaceSessionScenes(std::vector<ProjectScene> scenes, SceneId nextSceneId) {
    if (currentProject_.scenes == scenes && currentProject_.nextSceneId == nextSceneId)
        return;
    currentProject_.scenes = std::move(scenes);
    currentProject_.nextSceneId = nextSceneId;
    markDirty();
    for (auto* listener : listeners_)
        listener->projectPropertiesChanged();
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
        auto it = std::ranges::find(listeners_, listener);
        if (it == listeners_.end()) {
            listeners_.push_back(listener);
        }
    }
}

void ProjectManager::removeListener(ProjectManagerListener* listener) {
    listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), listener), listeners_.end());
}

void ProjectManager::beginProjectTeardown() {
    // The transport first: a listener about to drop what it built for this
    // project should not be dropping it out from under a running render.
    resetTransportForProjectBoundary();

    for (auto* listener : listeners_) {
        listener->projectTeardown();
    }
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

std::vector<ProjectManager::MissingMediaFile> ProjectManager::getReferencedMediaFiles() const {
    std::vector<MissingMediaFile> referenced;
    std::map<juce::String, size_t> indexByPath;

    const auto add = [&referenced, &indexByPath](const juce::String& path) {
        if (path.isEmpty())
            return;

        const auto found = indexByPath.find(path);
        if (found != indexByPath.end()) {
            ++referenced[found->second].referenceCount;
            return;
        }

        indexByPath[path] = referenced.size();
        referenced.push_back({path, 1});
    };

    for (const auto& clip : ClipManager::getInstance().getClips()) {
        if (!clip.isAudio())
            continue;
        for (const auto& event : clip.audio().events)
            add(event.sourceFilePath());
        for (const auto& take : clip.audio().takes)
            add(take.filePath);
    }

    for (const auto& reference : magda::SamplerMedia::getInstance().references())
        add(reference.source.getFullPathName());

    std::ranges::sort(referenced, {}, [](const MissingMediaFile& file) { return file.path; });
    return referenced;
}

std::vector<ProjectManager::MissingMediaFile> ProjectManager::findMissingMediaFiles(
    const std::vector<MissingMediaFile>& referenced, const std::function<bool()>& shouldStop) {
    std::vector<MissingMediaFile> missing;
    for (const auto& file : referenced) {
        if (shouldStop && shouldStop())
            return {};
        const auto localFile = localFileForStoredMediaPath(file.path);
        if (localFile == juce::File{} || !localFile.existsAsFile())
            missing.push_back(file);
    }
    return missing;
}

std::vector<ProjectManager::MissingMediaFile> ProjectManager::getMissingMediaFiles() const {
    return findMissingMediaFiles(getReferencedMediaFiles());
}

juce::String ProjectManager::missingMediaFileName(juce::String path) {
    path = path.replaceCharacter('\\', '/');
    return path.fromLastOccurrenceOf("/", false, false);
}

juce::File ProjectManager::localFileForStoredMediaPath(juce::String path) {
    if (path.isEmpty())
        return {};

    const auto normalized = path.replaceCharacter('\\', '/');
    const bool hasDrive = normalized.length() >= 3 &&
                          juce::CharacterFunctions::isLetter(normalized[0]) &&
                          normalized[1] == ':' && normalized[2] == '/';
    const bool isUnc = normalized.startsWith("//");

#if JUCE_WINDOWS
    if (!hasDrive && !isUnc)
        return {};
    return juce::File(normalized.replaceCharacter('/', '\\'));
#else
    if (hasDrive || isUnc || !normalized.startsWithChar('/'))
        return {};
    return juce::File(normalized);
#endif
}

std::vector<ProjectManager::MissingMediaReplacement> ProjectManager::searchForMissingMedia(
    const std::vector<MissingMediaFile>& missing, const juce::File& directory,
    const std::function<bool()>& shouldStop) {
    if (!directory.isDirectory() || missing.empty())
        return {};

    std::map<juce::String, int> missingCountByName;
    for (const auto& file : missing)
        ++missingCountByName[missingMediaFileName(file.path).toLowerCase()];

    std::map<juce::String, std::vector<juce::File>> candidatesByName;
    for (const auto& entry :
         juce::RangedDirectoryIterator(directory, true, "*", juce::File::findFiles)) {
        if (shouldStop && shouldStop())
            return {};
        const auto file = entry.getFile();
        const auto name = file.getFileName().toLowerCase();
        if (missingCountByName.contains(name))
            candidatesByName[name].push_back(file);
    }

    // Build proposals first, then reject any replacement selected for two old
    // paths. A single kick.wav must never silently replace two different kicks.
    std::vector<MissingMediaReplacement> proposals;
    for (const auto& file : missing) {
        if (shouldStop && shouldStop())
            return {};

        const auto nameKey = missingMediaFileName(file.path).toLowerCase();
        const auto candidatesIt = candidatesByName.find(nameKey);
        if (candidatesIt == candidatesByName.end())
            continue;
        const auto& candidates = candidatesIt->second;

        if (candidates.size() == 1 && missingCountByName[nameKey] == 1) {
            proposals.push_back({file.path, candidates.front()});
            continue;
        }

        int bestScore = 0;
        int bestCount = 0;
        juce::File best;
        for (const auto& candidate : candidates) {
            const int score = commonPathSuffixLength(file.path, candidate.getFullPathName());
            if (score > bestScore) {
                bestScore = score;
                bestCount = 1;
                best = candidate;
            } else if (score == bestScore) {
                ++bestCount;
            }
        }
        // A score of one is the filename itself. Require at least one unique
        // trailing directory component before resolving duplicate names.
        if (bestScore > 1 && bestCount == 1)
            proposals.push_back({file.path, best});
    }

    std::map<juce::String, int> proposalCountByReplacement;
    for (const auto& proposal : proposals)
        ++proposalCountByReplacement[proposal.replacement.getFullPathName()];

    std::vector<MissingMediaReplacement> matches;
    for (auto& proposal : proposals)
        if (proposalCountByReplacement[proposal.replacement.getFullPathName()] == 1)
            matches.push_back(std::move(proposal));
    return matches;
}

int ProjectManager::relinkMissingMediaFiles(
    const std::vector<MissingMediaReplacement>& replacements) {
    if (replacements.empty())
        return 0;

    std::map<juce::String, juce::String> paths;
    for (const auto& replacement : replacements) {
        if (replacement.missingPath.isNotEmpty() && replacement.replacement.existsAsFile()) {
            paths[replacement.missingPath] = replacement.replacement.getFullPathName();
        }
    }
    if (paths.empty())
        return 0;

    const auto changedPaths = relinkMediaPaths([&paths](const juce::String& path) {
        const auto found = paths.find(path);
        return found == paths.end() ? juce::String{} : found->second;
    });
    if (changedPaths.empty())
        return 0;

    markDirty();
    return static_cast<int>(changedPaths.size());
}

bool ProjectManager::relinkMissingMediaFile(const juce::String& missingPath,
                                            const juce::File& replacement) {
    return relinkMissingMediaFiles({{missingPath, replacement}}) == 1;
}

void ProjectManager::createTempMediaDirectory() {
    mediaDirectory_ = createTempMediaDirectoryOnDisk();
}

void ProjectManager::ensureMediaSubdirectories(const juce::File& mediaRoot) {
    if (mediaRoot == juce::File())
        return;
    for (const auto* subdir : kMediaSubdirs) {
        mediaRoot.getChildFile(subdir).createDirectory();
    }
}

void ProjectManager::migrateMediaFiles(const juce::File& oldDir, const juce::File& newDir,
                                       MediaTransfer transfer) {
    if (!oldDir.isDirectory() || oldDir == newDir)
        return;

    // Uniquify rather than skip: the .mgd is written from the relinked model at
    // the end of this same save, so a file that lands under a new name is
    // recorded correctly, and nothing may be stranded in the directory being
    // left behind.
    std::map<juce::String, juce::String> moves;
    for (const auto* subdir : kMediaSubdirs)
        moveMediaTree(oldDir.getChildFile(subdir), newDir.getChildFile(subdir), moves,
                      OnCollision::Uniquify, transfer);

    // A Save-As from a project opened before #2170 can still be carrying the
    // retired roots, so fold them on the way across rather than stranding them.
    for (const auto& legacy : kLegacyMediaSubdirs)
        moveMediaTree(oldDir.getChildFile(legacy.from), newDir.getChildFile(legacy.to), moves,
                      OnCollision::Uniquify, transfer);

    relinkMediaPaths([&moves](const juce::String& path) -> juce::String {
        const auto it = moves.find(path);
        return it == moves.end() ? juce::String() : it->second;
    });

    // Remove old temp directory if it's empty or under the temp root. A copy
    // leaves everything it read where it was.
    if (transfer == MediaTransfer::Move && isManagedTempMediaDirectory(oldDir)) {
        oldDir.deleteRecursively();
    }
}

void ProjectManager::foldLegacyMediaDirectories(const juce::File& mediaRoot) {
    if (mediaRoot == juce::File() || !mediaRoot.isDirectory())
        return;

    auto record = readMediaMoves(mediaRoot);

    std::map<juce::String, juce::String> moves;
    for (const auto& legacy : kLegacyMediaSubdirs)
        moveMediaTree(mediaRoot.getChildFile(legacy.from), mediaRoot.getChildFile(legacy.to), moves,
                      OnCollision::Skip, MediaTransfer::Move);

    // Record what moved before relinking. The new paths only reach the .mgd
    // when the user next saves, so without this the next load would have
    // nothing but a filename to go on.
    if (!moves.empty()) {
        for (const auto& [from, to] : moves) {
            const auto fromRel = mediaRelativePath(mediaRoot, juce::File(from));
            const auto toRel = mediaRelativePath(mediaRoot, juce::File(to));
            if (fromRel.isNotEmpty() && toRel.isNotEmpty())
                record[fromRel] = toRel;
        }
        writeMediaMoves(mediaRoot, record);
    }

    const auto resolve = [&moves, &record, &mediaRoot](const juce::String& path) -> juce::String {
        const auto it = moves.find(path);
        if (it != moves.end())
            return it->second;

        // A project folded on an earlier load but never saved still names the
        // old location in its .mgd. Only the record of what a fold actually
        // moved can say where the file went: a destination that merely shares
        // the name may be an unrelated file, and the .mgd may name something
        // that was already missing before any fold ran.
        const auto relative = mediaRelativePath(mediaRoot, juce::File(path));
        if (relative.isEmpty())
            return {};

        const auto recorded = record.find(relative);
        if (recorded == record.end())
            return {};

        const auto moved = mediaRoot.getChildFile(recorded->second);
        return moved.existsAsFile() ? moved.getFullPathName() : juce::String();
    };

    // The paths in the .mgd on disk still name folders that just went away, so
    // the project genuinely differs from its file until it is saved again.
    if (!relinkMediaPaths(resolve).empty())
        markDirty();
}

void ProjectManager::cleanupStaleTempDirectories(const juce::File& protectedDirectory) {
    auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::days(kStaleTempDays);

    for (const auto& root : getTempRoots()) {
        const auto tempRoot = root.getChildFile(kTempRootDir);
        if (!tempRoot.isDirectory())
            continue;

        for (const auto& entry :
             juce::RangedDirectoryIterator(tempRoot, false, "*", juce::File::findDirectories)) {
            const auto dir = entry.getFile();
            if (!dir.getFileName().startsWith(kTempPrefix))
                continue;
            if (protectedDirectory != juce::File() && dir == protectedDirectory)
                continue;
            if (dir.getLastModificationTime() < cutoff)
                dir.deleteRecursively();
        }
    }
}

// ============================================================================
// Auto-Save
// ============================================================================

void ProjectManager::setAutoSaveEnabled(bool enabled, int intervalSeconds) {
    autoSaveEnabled_ = enabled;
    if (enabled) {
        startAutoSaveTimer(intervalSeconds * 1000);
    } else {
        autoSaveTimer_.reset();
    }
}

void ProjectManager::autoSaveTick() {
    performAutosave();
}

bool ProjectManager::performAutosave() {
    if (!autoSaveEnabled_ || !isDirty_)
        return false;

    // Capture live plugin state before serializing
    if (onBeforeSave)
        onBeforeSave();

    const bool isUntitled = currentFile_.getFullPathName().isEmpty();
    auto autosaveFile = isUntitled ? getUntitledAutosaveFile()
                                   : currentFile_.getParentDirectory().getChildFile(
                                         currentFile_.getFileName() + kAutosaveExtension);

    ProjectInfo autosaveInfo = currentProject_;
    autosaveInfo.autosaveMediaDirectory =
        isUntitled ? mediaDirectory_.getFullPathName() : juce::String();
    autosaveInfo.touch();

    return ProjectSerializer::saveToFile(autosaveFile, autosaveInfo);
}

void ProjectManager::deleteAutosaveFile() {
    if (currentFile_.getFullPathName().isEmpty()) {
        discardUntitledAutosave();
        if (isManagedTempMediaDirectory(mediaDirectory_))
            mediaDirectory_.deleteRecursively();
        return;
    }

    auto autosaveFile = getAutosaveFile(currentFile_);
    if (autosaveFile.existsAsFile())
        autosaveFile.deleteFile();
}

juce::File ProjectManager::getUntitledAutosaveFile() {
    return paths::dataDir().getChildFile("autosave").getChildFile("Untitled.autosave");
}

bool ProjectManager::hasUntitledAutosave() {
    return getUntitledAutosaveFile().existsAsFile();
}

void ProjectManager::discardUntitledAutosave() {
    const auto autosaveFile = getUntitledAutosaveFile();
    StagedProjectData staged;
    const bool hasRecoverableMedia =
        autosaveFile.existsAsFile() && ProjectSerializer::loadAndStage(autosaveFile, staged);

    if (autosaveFile.existsAsFile())
        autosaveFile.deleteFile();

    if (hasRecoverableMedia) {
        const juce::File mediaDirectory(staged.info.autosaveMediaDirectory);
        if (isManagedTempMediaDirectory(mediaDirectory))
            mediaDirectory.deleteRecursively();
    }
}

void ProjectManager::prepareForCleanShutdown() {
    autoSaveEnabled_ = false;
    autoSaveTimer_.reset();
    discardUntitledAutosave();
    if (currentFile_.getFullPathName().isEmpty() && isManagedTempMediaDirectory(mediaDirectory_))
        mediaDirectory_.deleteRecursively();
}

bool ProjectManager::promptUntitledAutosaveRecovery() {
    const auto autosaveFile = getUntitledAutosaveFile();
    if (!autosaveFile.existsAsFile())
        return false;

    return juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::QuestionIcon, "Recover Unsaved Project",
        "MAGDA found an autosave from an untitled project that did not close cleanly.\n\n"
        "Autosave saved: " +
            autosaveFile.getLastModificationTime().toString(true, true) +
            "\n\nWould you like to recover it?",
        "Recover", "Discard");
}

bool ProjectManager::recoverUntitledAutosave(
    const std::function<void(const ProjectInfo&)>& onBeforeCommit) {
    const auto autosaveFile = getUntitledAutosaveFile();
    if (!autosaveFile.existsAsFile()) {
        lastError_ = "No untitled autosave was found.";
        return false;
    }

    StagedProjectData staged;
    if (!ProjectSerializer::loadAndStage(autosaveFile, staged)) {
        lastError_ = "The untitled autosave could not be recovered. It may be corrupted or from "
                     "an incompatible version.";
        return false;
    }

    const auto previousMediaDirectory = mediaDirectory_;
    beginProjectTeardown();

    if (onBeforeCommit)
        onBeforeCommit(staged.info);

    ProjectSerializer::commitStaged(staged);

    const juce::File recoveredMediaDirectory(staged.info.autosaveMediaDirectory);
    const bool canReclaimMedia = isManagedTempMediaDirectory(recoveredMediaDirectory);
    if (previousMediaDirectory != recoveredMediaDirectory &&
        isManagedTempMediaDirectory(previousMediaDirectory))
        previousMediaDirectory.deleteRecursively();

    currentProject_ = staged.info;
    currentProject_.filePath.clear();
    currentProject_.autosaveMediaDirectory.clear();
    currentFile_ = juce::File();
    isProjectOpen_ = true;

    if (canReclaimMedia) {
        mediaDirectory_ = recoveredMediaDirectory;
    } else {
        createTempMediaDirectory();
    }
    ensureMediaSubdirectories(mediaDirectory_);

    UndoManager::getInstance().clearHistory();
    clearDirty();
    markDirty();
    notifyProjectOpened();

    if (onAfterLoad)
        onAfterLoad(currentProject_);

    return true;
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
