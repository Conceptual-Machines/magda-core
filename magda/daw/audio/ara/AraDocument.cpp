#include "AraDocument.hpp"

#include <ARA_API/ARAVST3.h>
#include <pluginterfaces/vst/ivstcomponent.h>

#if JUCE_MAC
    #include <ARA_API/ARAAudioUnit.h>
    #include <AudioToolbox/AudioToolbox.h>
#endif

#include <algorithm>
#include <set>

namespace magda::daw::audio::ara {

namespace {

using RecordConverter = AraSourceConverter;

template <typename T, typename Ref> Ref hostRefOf(T* object) {
    return juce::ARAHostModel::ConversionFunctions<T*, Ref>::toHostRef(object);
}

}  // namespace

AraDocument::AraDocument(AraModelEvents events, AraTransportRequests transport)
    : events_(std::move(events)), transport_(std::move(transport)) {}

std::unique_ptr<AraDocument> AraDocument::create(juce::ARAFactoryWrapper factory,
                                                 std::unique_ptr<juce::AudioPluginInstance> anchor,
                                                 const juce::String& name,
                                                 engine::AudioFileReaderFactory& files,
                                                 AraModelEvents events,
                                                 AraTransportRequests transport) {
    if (factory.get() == nullptr || anchor == nullptr)
        return nullptr;

    std::unique_ptr<AraDocument> document(new AraDocument(std::move(events), std::move(transport)));
    document->files_ = &files;
    document->anchor_ = std::move(anchor);

    auto content = std::make_unique<AraContentAccess>();
    document->content_ = content.get();

    document->host_ = juce::ARAHostDocumentController::create(
        std::move(factory), name, std::make_unique<AraAudioAccess>(files),
        std::make_unique<AraArchiving>(), std::move(content),
        std::make_unique<AraModelUpdates>(document->events_),
        std::make_unique<AraPlaybackRequests>(document->transport_));

    if (document->host_ == nullptr)
        return nullptr;

    auto properties = juce::ARAHostModel::MusicalContext::getEmptyProperties();
    properties.name = "Project";
    properties.orderIndex = 0;
    properties.color = nullptr;
    document->context_ = std::make_unique<juce::ARAHostModel::MusicalContext>(
        hostRefOf<AraDocument, ARA::ARAMusicalContextHostRef>(document.get()),
        document->controller(), properties);
    return document;
}

AraDocument::~AraDocument() {
    if (host_ == nullptr)
        return;

    const juce::ARAEditGuard guard(controller());
    regions_.clear();
    modifications_.clear();
    sources_.clear();
    sequences_.clear();
    context_.reset();
}

ARA::Host::DocumentController& AraDocument::controller() const {
    return host_->getDocumentController();
}

void AraDocument::setMusicalContext(const engine::TempoMap& map) {
    content_->setMusicalContent(tempoEntriesFor(map), barSignaturesFor(map));

    const juce::ARAEditGuard guard(controller());
    controller().updateMusicalContextContent(context_->getPluginRef(), nullptr,
                                             ARA::ContentUpdateScopes::everythingIsAffected());
}

std::vector<std::string> AraDocument::syncRegions(const std::vector<AraRegionSpec>& regions) {
    std::vector<std::string> refused;
    const juce::ARAEditGuard guard(controller());

    std::set<EventKey> wantedEvents;
    std::set<SourceId> wantedSources;
    for (const auto& spec : regions) {
        wantedEvents.insert({spec.clipId, spec.eventId});
        wantedSources.insert(spec.sourceId);

        // A file moved to an identical copy keeps its source, and the edits on it (#3137);
        // any other file is a different source to the plugin.
        if (const auto found = sources_.find(spec.sourceId);
            found != sources_.end() && found->second->record.path != spec.sourcePath &&
            !repoint(*found->second, spec.sourcePath))
            dropSource(spec.sourceId);
    }

    for (auto it = regions_.begin(); it != regions_.end();) {
        const auto key = (it++)->first;
        if (!wantedEvents.contains(key))
            dropEvent(key);
    }

    for (auto it = sources_.begin(); it != sources_.end();) {
        const auto id = (it++)->first;
        if (!wantedSources.contains(id))
            dropSource(id);
    }

    for (const auto& spec : regions)
        if (auto* source = ensureSource(spec, refused))
            placeRegion(spec, *source);

    for (auto it = sequences_.begin(); it != sequences_.end();) {
        const auto track = it->first;
        const bool used = std::any_of(regions_.begin(), regions_.end(),
                                      [track](const auto& r) { return r.second.trackId == track; });
        it = used ? std::next(it) : sequences_.erase(it);
    }
    return refused;
}

void AraDocument::dropEvent(const EventKey& key) {
    regions_.erase(key);
    modifications_.erase(key);
}

void AraDocument::dropSource(SourceId id) {
    for (auto it = modifications_.begin(); it != modifications_.end();) {
        const auto key = it->first;
        const bool onSource = it->second.sourceId == id;
        ++it;
        if (onSource)
            dropEvent(key);
    }
    sources_.erase(id);
}

bool AraDocument::repoint(SourceEntry& source, const std::string& path) {
    const auto file = files_->open(path);
    if (file == nullptr || file->sampleRate() != source.record.sampleRate ||
        file->lengthInSamples() != source.record.lengthInSamples ||
        file->numChannels() != source.record.numChannels)
        return false;

    source.record.path = path;
    auto properties = juce::ARAHostModel::AudioSource::getEmptyProperties();
    properties.name = source.record.path.c_str();
    properties.persistentID = source.persistentId.c_str();
    properties.sampleCount = source.record.lengthInSamples;
    properties.sampleRate = source.record.sampleRate;
    properties.channelCount = source.record.numChannels;
    properties.merits64BitSamples = false;
    source.source->update(properties);
    return true;
}

AraDocument::SourceEntry* AraDocument::ensureSource(const AraRegionSpec& spec,
                                                    std::vector<std::string>& refused) {
    if (const auto found = sources_.find(spec.sourceId); found != sources_.end())
        return found->second.get();

    // The file's own facts rather than the pool's: ARA reads it at its own rate and width.
    const auto file = files_->open(spec.sourcePath);
    if (file == nullptr) {
        refused.push_back("clip " + std::to_string(spec.clipId) + " event " +
                          std::to_string(spec.eventId) + ": cannot open \"" + spec.sourcePath +
                          "\"");
        return nullptr;
    }

    auto entry = std::make_unique<SourceEntry>();
    entry->record = {.path = spec.sourcePath,
                     .sampleRate = file->sampleRate(),
                     .lengthInSamples = file->lengthInSamples(),
                     .numChannels = file->numChannels()};
    entry->persistentId =
        spec.sourcePersistentId.empty() ? "source:" + spec.sourcePath : spec.sourcePersistentId;

    auto properties = juce::ARAHostModel::AudioSource::getEmptyProperties();
    properties.name = spec.sourcePath.c_str();
    properties.persistentID = entry->persistentId.c_str();
    properties.sampleCount = entry->record.lengthInSamples;
    properties.sampleRate = entry->record.sampleRate;
    properties.channelCount = entry->record.numChannels;
    properties.merits64BitSamples = false;

    entry->source = std::make_unique<juce::ARAHostModel::AudioSource>(
        RecordConverter::toHostRef(&entry->record), controller(), properties);
    entry->source->enableAudioSourceSamplesAccess(true);

    auto* raw = entry.get();
    sources_.emplace(spec.sourceId, std::move(entry));
    return raw;
}

AraDocument::SequenceEntry& AraDocument::ensureSequence(TrackId track) {
    if (const auto found = sequences_.find(track); found != sequences_.end())
        return found->second;

    auto& entry = sequences_[track];
    entry.name = "Track " + std::to_string(track);

    auto properties = juce::ARAHostModel::RegionSequence::getEmptyProperties();
    properties.name = entry.name.c_str();
    properties.orderIndex = track;
    properties.musicalContextRef = context_->getPluginRef();
    properties.color = nullptr;
    entry.sequence = std::make_unique<juce::ARAHostModel::RegionSequence>(
        hostRefOf<SequenceEntry, ARA::ARARegionSequenceHostRef>(&entry), controller(), properties);
    return entry;
}

void AraDocument::placeRegion(const AraRegionSpec& spec, SourceEntry& source) {
    const EventKey key{spec.clipId, spec.eventId};

    // An event now reading another source (a comp render, say) is a new modification.
    if (const auto found = modifications_.find(key);
        found != modifications_.end() &&
        (found->second.sourceId != spec.sourceId ||
         (!spec.modificationPersistentId.empty() &&
          found->second.persistentId != spec.modificationPersistentId)))
        dropEvent(key);

    auto& modification = modifications_[key];
    if (modification.modification == nullptr) {
        modification.sourceId = spec.sourceId;
        modification.persistentId =
            spec.modificationPersistentId.empty()
                ? "clip:" + std::to_string(spec.clipId) + "/event:" + std::to_string(spec.eventId)
                : spec.modificationPersistentId;

        auto properties = juce::ARAHostModel::AudioModification::getEmptyProperties();
        properties.name = spec.name.c_str();
        properties.persistentID = modification.persistentId.c_str();
        modification.modification = std::make_unique<juce::ARAHostModel::AudioModification>(
            hostRefOf<ModificationEntry, ARA::ARAAudioModificationHostRef>(&modification),
            controller(), *source.source, properties);
    }

    auto& sequence = ensureSequence(spec.trackId);
    auto& region = regions_[key];

    // Unchanged properties are not re-sent: a plugin may take any update as a content change.
    if (region.region != nullptr && region.trackId == spec.trackId && region.name == spec.name &&
        region.timing == spec.timing)
        return;

    region.trackId = spec.trackId;
    region.name = spec.name;
    region.timing = spec.timing;

    auto properties = juce::ARAHostModel::PlaybackRegion::getEmptyProperties();
    properties.transformationFlags = ARA::kARAPlaybackTransformationNoChanges;
    properties.startInModificationTime = spec.timing.startInModification;
    properties.durationInModificationTime = spec.timing.durationInModification;
    properties.startInPlaybackTime = spec.timing.startInPlayback;
    properties.durationInPlaybackTime = spec.timing.durationInPlayback;
    properties.musicalContextRef = context_->getPluginRef();
    properties.regionSequenceRef = sequence.sequence->getPluginRef();
    properties.name = region.name.c_str();
    properties.color = nullptr;

    if (region.region == nullptr) {
        region.region = std::make_unique<juce::ARAHostModel::PlaybackRegion>(
            hostRefOf<RegionEntry, ARA::ARAPlaybackRegionHostRef>(&region), controller(),
            *modification.modification, properties);
        newRegions_.push_back(key);
    } else
        region.region->update(properties);
}

const ARA::ARAPlugInExtensionInstance* AraDocument::bind(juce::AudioPluginInstance& instance,
                                                         ARA::ARAPlugInInstanceRoleFlags roles) {
    constexpr auto kAllRoles =
        ARA::kARAPlaybackRendererRole | ARA::kARAEditorRendererRole | ARA::kARAEditorViewRole;
    const auto documentRef = controller().getRef();

    if (auto* vst3 = instance.getVST3Client()) {
        // ARA::IPlugInEntryPoint2's IID, spelled out: the definition lives in JUCE's VST3 code.
        const Steinberg::TUID entryPointIid =
            INLINE_UID(0xCD9A5913, 0xC9EB46D7, 0x96CA53AD, 0xD1DB89F5);
        ARA::IPlugInEntryPoint2* entryPoint = nullptr;
        if (auto* component = vst3->getIComponentPtr();
            component != nullptr &&
            component->queryInterface(entryPointIid, reinterpret_cast<void**>(&entryPoint)) ==
                Steinberg::kResultOk &&
            entryPoint != nullptr) {
            const auto* bound =
                entryPoint->bindToDocumentControllerWithRoles(documentRef, kAllRoles, roles);
            entryPoint->release();
            return bound;
        }
    }

#if JUCE_MAC
    if (auto* au = instance.getAudioUnitClient()) {
        auto size = static_cast<UInt32>(sizeof(ARA::ARAAudioUnitPlugInExtensionBinding));
        const auto expected = size;
        ARA::ARAAudioUnitPlugInExtensionBinding binding{ARA::kARAAudioUnitMagic, documentRef,
                                                        nullptr, kAllRoles, roles};
        if (AudioUnitGetProperty(au->getAudioUnitHandle(),
                                 ARA::kAudioUnitProperty_ARAPlugInExtensionBindingWithRoles,
                                 kAudioUnitScope_Global, 0, &binding, &size) == noErr &&
            size == expected && binding.inOutMagicNumber == ARA::kARAAudioUnitMagic &&
            binding.inDocumentControllerRef == documentRef)
            return binding.outPlugInExtension;
    }
#endif

    return nullptr;
}

void AraDocument::select(const ARA::ARAPlugInExtensionInstance* instance,
                         const std::vector<EventKey>& events) {
    if (instance == nullptr || instance->editorViewInterface == nullptr)
        return;

    std::vector<ARA::ARAPlaybackRegionRef> regionRefs;
    std::vector<ARA::ARARegionSequenceRef> sequenceRefs;
    for (const auto& key : events) {
        const auto found = regions_.find(key);
        if (found == regions_.end() || found->second.region == nullptr)
            continue;
        regionRefs.push_back(found->second.region->getPluginRef());
        if (const auto sequence = sequences_.find(found->second.trackId);
            sequence != sequences_.end())
            sequenceRefs.push_back(sequence->second.sequence->getPluginRef());
    }

    auto selection = juce::makeARASizedStruct(&ARA::ARAViewSelection::timeRange);
    selection.playbackRegionRefsCount = regionRefs.size();
    selection.playbackRegionRefs = regionRefs.data();
    selection.regionSequenceRefsCount = sequenceRefs.size();
    selection.regionSequenceRefs = sequenceRefs.data();
    selection.timeRange = nullptr;
    ARA::Host::EditorView(instance).notifySelection(&selection);
}

std::optional<AraDocument::EventKey> AraDocument::eventFor(
    ARA::ARAAudioModificationHostRef modification) const {
    for (const auto& [key, entry] : modifications_)
        if (hostRefOf<const ModificationEntry, ARA::ARAAudioModificationHostRef>(&entry) ==
            modification)
            return key;
    return std::nullopt;
}

std::optional<AraDocument::EventKey> AraDocument::eventFor(
    ARA::ARAPlaybackRegionHostRef region) const {
    for (const auto& [key, entry] : regions_)
        if (hostRefOf<const RegionEntry, ARA::ARAPlaybackRegionHostRef>(&entry) == region)
            return key;
    return std::nullopt;
}

std::string AraDocument::archiveId() const {
    const auto* factory = controller().getFactory();
    return factory != nullptr && factory->documentArchiveID != nullptr ? factory->documentArchiveID
                                                                       : std::string{};
}

bool AraDocument::canRead(const std::string& id) const {
    const auto* factory = controller().getFactory();
    if (factory == nullptr || id.empty())
        return false;
    if (factory->documentArchiveID != nullptr && id == factory->documentArchiveID)
        return true;
    for (ARA::ARASize i = 0; i < factory->compatibleDocumentArchiveIDsCount; ++i)
        if (id == factory->compatibleDocumentArchiveIDs[i])
            return true;
    return false;
}

std::optional<juce::MemoryBlock> AraDocument::store() {
    juce::MemoryOutputStream stream;
    if (!controller().storeObjectsToArchive(AraArchiveOutConverter::toHostRef(&stream), nullptr))
        return std::nullopt;
    return stream.getMemoryBlock();
}

std::vector<std::string> AraDocument::restore(const std::vector<AraRegionSpec>& regions,
                                              const AraArchiveIn& archive) {
    // One edit for both: the plugin injects archived state into objects it has just seen made.
    const juce::ARAEditGuard guard(controller());
    auto refused = syncRegions(regions);

    // Versioning is the host's to check before the call, not the plugin's to survive.
    if (!canRead(archive.archiveId)) {
        refused.push_back("its saved state is from a version of the plugin that cannot read it");
        return refused;
    }

    auto in = archive;
    if (!controller().restoreObjectsFromArchive(AraArchiveInConverter::toHostRef(&in), nullptr))
        refused.push_back("the plugin could not read its saved state");
    return refused;
}

void AraDocument::requestAnalysis() {
    const auto* factory = controller().getFactory();
    if (factory == nullptr || factory->analyzeableContentTypesCount == 0)
        return;

    for (const auto& [id, entry] : sources_) {
        std::vector<ARA::ARAContentType> incomplete;
        for (ARA::ARASize i = 0; i < factory->analyzeableContentTypesCount; ++i)
            if (controller().isAudioSourceContentAnalysisIncomplete(
                    entry->source->getPluginRef(), factory->analyzeableContentTypes[i]))
                incomplete.push_back(factory->analyzeableContentTypes[i]);
        if (!incomplete.empty())
            controller().requestAudioSourceContentAnalysis(entry->source->getPluginRef(),
                                                           incomplete.size(), incomplete.data());
    }
}

std::vector<AraDocument::EventKey> AraDocument::takeNewRegions() {
    std::vector<EventKey> created;
    for (const auto& key : std::exchange(newRegions_, {}))
        if (regions_.contains(key))
            created.push_back(key);
    return created;
}

juce::ARAHostModel::PlaybackRegion* AraDocument::region(ClipId clip, EventId event) const {
    const auto found = regions_.find({clip, event});
    return found == regions_.end() ? nullptr : found->second.region.get();
}

}  // namespace magda::daw::audio::ara
