// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AraTrackEditing.hpp"

#include <map>
#include <set>
#include <tuple>

#include "EngineProject.hpp"
#include "EngineRuntimeFactory.hpp"
#include "ara/AraDocument.hpp"
#include "clip/ClipSnapshotCompiler.hpp"
#include "plugins/engine/EngineExternalDevice.hpp"

namespace magda::daw::engine_host {

namespace ara = audio::ara;
using ExternalDevice = audio::engine_adapter::EngineExternalDevice;
using EventKey = ara::AraDocument::EventKey;

namespace {

constexpr auto kRoles =
    ARA::kARAPlaybackRendererRole | ARA::kARAEditorRendererRole | ARA::kARAEditorViewRole;
constexpr const char* kArchiveMagic = "MAGDA-ARA 1";

std::optional<ara::AraArchiveIn> readArchive(const juce::File& file) {
    juce::FileInputStream in(file);
    if (!in.openedOk() || in.readNextLine() != kArchiveMagic)
        return std::nullopt;
    ara::AraArchiveIn archive;
    archive.archiveId = in.readNextLine().toStdString();
    in.readIntoMemoryBlock(archive.data);
    return archive;
}

bool writeArchive(const juce::File& file, const std::string& archiveId,
                  const juce::MemoryBlock& data) {
    file.getParentDirectory().createDirectory();
    juce::TemporaryFile temp(file);
    {
        juce::FileOutputStream out(temp.getFile());
        if (!out.openedOk())
            return false;
        out << kArchiveMagic << "\n" << juce::String(archiveId) << "\n";
        out.write(data.getData(), data.getSize());
    }
    return temp.overwriteTargetFileWithTemporary();
}

/** @brief The regions' keys with the tracks and files they are on, which renderers hold them by. */
auto shapeOf(const std::vector<ara::AraRegionSpec>& regions) {
    std::map<EventKey, std::tuple<TrackId, SourceId, std::string>> shape;
    for (const auto& region : regions)
        shape[{region.clipId, region.eventId}] = {region.trackId, region.sourceId,
                                                  region.sourcePath};
    return shape;
}

}  // namespace

// ============================================================================
// Document
// ============================================================================

struct AraTrackEditing::Document {
    juce::String id;
    juce::String name;
    EngineFileReaders files;
    std::unique_ptr<ara::AraDocument> ara;
    std::vector<Binding*> bindings;
    std::vector<ara::AraRegionSpec> regions;
    bool started = false;
};

// ============================================================================
// Binding
// ============================================================================

/**
 * @brief One ARA instance on one track: its renderers and the regions they hold.
 *
 * Lives on the device and dies before its plugin, which is what ARA asks of an extension and
 * the regions it was given.
 */
class AraTrackEditing::Binding final : public ExternalDevice::Attachment {
  public:
    Binding(std::shared_ptr<Document> document, TrackId track, ExternalDevice& device,
            const ARA::ARAPlugInExtensionInstance* extension)
        : document_(std::move(document)), track_(track), device_(device), extension_(extension) {
        const juce::ARAHostModel::PlugInExtensionInstance wrapped{extension};
        playback_ = wrapped.getPlaybackRendererInterface();
        editor_ = wrapped.getEditorRendererInterface();
        document_->bindings.push_back(this);
    }

    ~Binding() override {
        // Out of every plan by now, so nothing processes it; unprepared, as ARA wants for this.
        auto& plugin = device_.plugin();
        const juce::ScopedLock lock(plugin.getCallbackLock());
        plugin.releaseResources();

        // JUCE 9.0.3's registry destructor erases from the map it walks, so both die empty.
        for (const auto& key : assigned_)
            if (auto* region = document_->ara->region(key.first, key.second)) {
                playback_.remove(*region);
                editor_.remove(*region);
            }
        std::erase(document_->bindings, this);

        // The document has to outlive this plugin's extension, which goes with the device
        // after this returns.
        juce::MessageManager::callAsync([document = std::move(document_)] {});
    }

    void editorOpened() override {
        // The editor shows, and the plugin analyses, what the selection names.
        document_->ara->select(extension_, keys());
    }

    TrackId track() const {
        return track_;
    }

    ExternalDevice& device() {
        return device_;
    }

    /** @brief Whether this track's regions differ from what the renderers hold. */
    bool stale() const {
        return keys() != std::vector<EventKey>(assigned_.begin(), assigned_.end());
    }

    /**
     * @brief Give the renderers this track's regions; the plugin is unprepared.
     *
     * @p created are regions the last edit made, possibly under a key whose old region it
     * deleted: those are new to the renderers whatever this held for the key.
     */
    void assign(const std::set<EventKey>& created) {
        // A deleted region has already left the renderers through its deletion listener.
        std::erase_if(assigned_, [this, &created](const EventKey& key) {
            return created.contains(key) ||
                   document_->ara->region(key.first, key.second) == nullptr;
        });
        for (const auto& key : keys()) {
            auto* region = document_->ara->region(key.first, key.second);
            if (region == nullptr || !assigned_.insert(key).second)
                continue;
            if (playback_.isValid())
                playback_.add(*region);
            if (editor_.isValid())
                editor_.add(*region);
        }
    }

  private:
    std::vector<EventKey> keys() const {
        std::set<EventKey> found;
        for (const auto& region : document_->regions)
            if (region.trackId == track_ && document_->ara->region(region.clipId, region.eventId))
                found.insert({region.clipId, region.eventId});
        return {found.begin(), found.end()};
    }

    std::shared_ptr<Document> document_;
    TrackId track_;
    ExternalDevice& device_;
    const ARA::ARAPlugInExtensionInstance* extension_;
    juce::ARAHostModel::PlaybackRendererInterface playback_;
    juce::ARAHostModel::EditorRendererInterface editor_;
    std::set<EventKey> assigned_;
};

// ============================================================================
// AraTrackEditing
// ============================================================================

AraTrackEditing::AraTrackEditing(juce::AudioPluginFormatManager& formats,
                                 const juce::KnownPluginList& known,
                                 std::function<const engine::TempoMap&()> tempo)
    : formats_(formats), known_(known), tempo_(std::move(tempo)) {
    ClipManager::getInstance().addListener(this);
    ProjectManager::getInstance().addListener(this);
}

AraTrackEditing::~AraTrackEditing() {
    stopTimer();
    ProjectManager::getInstance().removeListener(this);
    ClipManager::getInstance().removeListener(this);
}

juce::File AraTrackEditing::archiveFile(const juce::File& mediaDirectory,
                                        const juce::String& pluginId) {
    return mediaDirectory.getChildFile("ara").getChildFile(
        juce::File::createLegalFileName(pluginId) + ".araarchive");
}

void AraTrackEditing::attach(TrackId track, ExternalDevice& device) {
    auto& plugin = device.plugin();
    if (!plugin.getPluginDescription().hasARAExtension)
        return;

    auto document = documentFor(plugin);
    if (document == nullptr)
        return;

    device.whileUnprepared([&] {
        const auto* extension = document->ara->bind(plugin, kRoles);
        if (extension == nullptr) {
            report(document->name + " refused to bind to its ARA document");
            return;
        }
        device.attach(std::make_unique<Binding>(document, track, device, extension));
        juce::Logger::writeToLog("[ara] bound " + document->name + " on track " +
                                 juce::String(track));
    });

    sync(*document);
    startTimerHz(10);
}

std::shared_ptr<AraTrackEditing::Document> AraTrackEditing::documentFor(
    juce::AudioPluginInstance& plugin) {
    // The scan's description, not the instance's: on Linux a VST3 instance names the .so in its
    // bundle as its file, which no format will open again.
    auto description = plugin.getPluginDescription();
    for (const auto& known : known_.getTypes())
        if (known.pluginFormatName == description.pluginFormatName &&
            known.uniqueId == description.uniqueId && known.name == description.name) {
            description = known;
            break;
        }
    const auto id = description.createIdentifierString();
    if (const auto found = documents_.find(id); found != documents_.end())
        return found->second;

    // VST3 and AUv2 answer at once, which is what lets the instance be bound before it is
    // prepared; only an AUv3, instantiated out of process, answers late.
    std::optional<juce::ARAFactoryWrapper> factory;
    juce::createARAFactoryAsync(plugin,
                                [&factory](juce::ARAFactoryWrapper f) { factory = std::move(f); });
    if (!factory.has_value()) {
        report(description.name + " (" + description.pluginFormatName +
               ") hands over its ARA factory late, which is not supported yet; use its VST3");
        return nullptr;
    }

    // Keeps the module loaded until the document is gone (AraDocument::create).
    juce::String error;
    auto anchor = formats_.createPluginInstance(
        description, ProjectManager::getInstance().getCurrentProjectInfo().sampleRate, 1024, error);
    if (anchor == nullptr) {
        report(description.name + " could not be opened for ARA: " + error);
        return nullptr;
    }

    auto document = std::make_shared<Document>();
    document->id = id;
    document->name = description.name;
    document->ara = ara::AraDocument::create(std::move(*factory), std::move(anchor),
                                             description.name, document->files);
    if (document->ara == nullptr) {
        report(description.name + " did not create an ARA document");
        return nullptr;
    }
    document->ara->setMusicalContext(tempo_());
    documents_[id] = document;
    juce::Logger::writeToLog("[ara] document for " + description.name);
    return document;
}

void AraTrackEditing::sync(Document& document) {
    // Every arrangement clip on a track that has this plugin, timed as the engine plays it.
    std::set<TrackId> tracks;
    for (const auto* binding : document.bindings)
        tracks.insert(binding->track());

    std::vector<engine::ClipLane> lanes;
    for (const auto& clip : ClipManager::getInstance().getArrangementClips()) {
        if (!clip.isAudio() || !tracks.contains(clip.trackId))
            continue;
        auto lane = std::ranges::find(lanes, clip.trackId, &engine::ClipLane::trackId);
        if (lane == lanes.end())
            lane = lanes.insert(lanes.end(), engine::ClipLane{.trackId = clip.trackId});
        lane->clips.push_back(clip);
    }

    std::vector<ara::AraRegionSpec> regions;
    const auto snapshot = engine::compileClipSnapshot(lanes, clipSources(), tempo_());
    for (const auto& track : snapshot.tracks)
        for (const auto& clip : track.audio)
            for (const auto& event : clip.events) {
                const auto* model = ClipManager::getInstance().getClip(clip.clipId);
                // A region plays once: a looping clip is played raw, as if it did not loop.
                auto once = event;
                once.loopEnabled = false;
                const auto timing = ara::regionTimingFor(clip, once);
                regions.push_back(
                    {.clipId = clip.clipId,
                     .eventId = event.eventId,
                     .trackId = track.trackId,
                     .sourceId = event.sourceId,
                     .sourcePath = event.filePath,
                     .timing = timing,
                     .name = model != nullptr ? model->name.toStdString() : std::string{},
                     // The pool's ID, saved with the project and kept through a relink.
                     .sourcePersistentId = "source:" + std::to_string(event.sourceId),
                     .modificationPersistentId = "clip:" + std::to_string(clip.clipId) +
                                                 "/event:" + std::to_string(event.eventId)});
            }

    // Region changes go to every instance; a new instance only needs its own regions handed over.
    const bool changed = !document.started || regions != document.regions;
    // Only adding or removing a renderer's region needs the plugin unprepared: a region that
    // just moves is edited while it plays, with no dry moment (#3137).
    const bool reshaped = !document.started || shapeOf(regions) != shapeOf(document.regions) ||
                          std::ranges::any_of(regions, [&document](const auto& region) {
                              return document.ara->region(region.clipId, region.eventId) == nullptr;
                          });
    std::vector<Binding*> touched;
    for (auto* binding : document.bindings)
        if (reshaped || binding->stale())
            touched.push_back(binding);
    if (touched.empty() && !changed)
        return;

    std::set<EventKey> created;
    const std::function<void()> work = [&] {
        if (changed) {
            std::vector<std::string> refused;
            const auto archive =
                archiveFile(ProjectManager::getInstance().getMediaDirectory(), document.id);
            const auto saved =
                !document.started && archive.existsAsFile() ? readArchive(archive) : std::nullopt;
            refused =
                saved ? document.ara->restore(regions, *saved) : document.ara->syncRegions(regions);
            document.regions = regions;
            document.started = true;
            for (const auto& key : document.ara->takeNewRegions())
                created.insert(key);
            document.ara->requestAnalysis();
            if (!refused.empty())
                report(document.name + ": " + juce::String(refused.front()));
            juce::Logger::writeToLog(
                "[ara] " + document.name + ": " + juce::String(static_cast<int>(regions.size())) +
                " regions on " + juce::String(static_cast<int>(tracks.size())) + " tracks" +
                (saved ? ", restored from the project" : ""));
        }
        for (auto* binding : touched)
            binding->assign(created);
    };

    // Every touched instance unprepared at once: a region they share goes for all of them.
    std::function<void(std::size_t)> pause = [&](std::size_t i) {
        if (i == touched.size())
            return work();
        touched[i]->device().whileUnprepared([&] { pause(i + 1); });
    };
    pause(0);
}

void AraTrackEditing::saveArchives(const juce::File& media) {
    for (const auto& [id, document] : documents_) {
        if (document->bindings.empty())
            continue;
        const auto data = document->ara->store();
        if (!data || !writeArchive(archiveFile(media, id), document->ara->archiveId(), *data))
            report(document->name + "'s edits could not be saved with the project");
    }
}

void AraTrackEditing::forgetProject() {
    // The devices keep their documents until they go themselves (Binding).
    documents_.clear();
    stopTimer();
}

void AraTrackEditing::report(const juce::String& message) const {
    juce::Logger::writeToLog("[ara] " + message);
    if (notify)
        notify(message);
}

void AraTrackEditing::clipsChanged() {
    for (auto& [id, document] : documents_)
        sync(*document);
}

void AraTrackEditing::clipPropertyChanged(ClipId) {
    for (auto& [id, document] : documents_)
        sync(*document);
}

void AraTrackEditing::projectSaved(const ProjectInfo&) {
    saveArchives(ProjectManager::getInstance().getMediaDirectory());
}

void AraTrackEditing::projectPropertiesChanged() {
    // Tempo moves every region's playback time, and the plugin's bar grid with it.
    for (auto& [id, document] : documents_) {
        document->ara->setMusicalContext(tempo_());
        sync(*document);
    }
}

void AraTrackEditing::timerCallback() {
    // A plugin reports analysis and edits only from inside this call.
    // A plugin no track holds any more is dropped with its document.
    std::erase_if(documents_, [](const auto& entry) { return entry.second->bindings.empty(); });
    for (auto& [id, document] : documents_)
        document->ara->controller().notifyModelUpdates();
    if (documents_.empty())
        stopTimer();
}

}  // namespace magda::daw::engine_host
