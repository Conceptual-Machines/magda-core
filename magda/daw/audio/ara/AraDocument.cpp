#include "AraDocument.hpp"

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
                                                 const juce::String& name,
                                                 engine::AudioFileReaderFactory& files,
                                                 AraModelEvents events,
                                                 AraTransportRequests transport) {
    if (factory.get() == nullptr)
        return nullptr;

    std::unique_ptr<AraDocument> document(new AraDocument(std::move(events), std::move(transport)));
    document->files_ = &files;

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

        // A source whose file moved is a different source to the plugin.
        if (const auto found = sources_.find(spec.sourceId);
            found != sources_.end() && found->second->record.path != spec.sourcePath)
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
    entry->persistentId = "source:" + spec.sourcePath;

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
        found != modifications_.end() && found->second.sourceId != spec.sourceId)
        dropEvent(key);

    auto& modification = modifications_[key];
    if (modification.modification == nullptr) {
        modification.sourceId = spec.sourceId;
        modification.persistentId =
            "clip:" + std::to_string(spec.clipId) + "/event:" + std::to_string(spec.eventId);

        auto properties = juce::ARAHostModel::AudioModification::getEmptyProperties();
        properties.name = spec.name.c_str();
        properties.persistentID = modification.persistentId.c_str();
        modification.modification = std::make_unique<juce::ARAHostModel::AudioModification>(
            hostRefOf<ModificationEntry, ARA::ARAAudioModificationHostRef>(&modification),
            controller(), *source.source, properties);
    }

    auto& sequence = ensureSequence(spec.trackId);
    auto& region = regions_[key];
    region.trackId = spec.trackId;
    region.name = spec.name;

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

    if (region.region == nullptr)
        region.region = std::make_unique<juce::ARAHostModel::PlaybackRegion>(
            hostRefOf<RegionEntry, ARA::ARAPlaybackRegionHostRef>(&region), controller(),
            *modification.modification, properties);
    else
        region.region->update(properties);
}

juce::ARAHostModel::PlugInExtensionInstance AraDocument::bind(
    juce::AudioPluginInstance& instance, ARA::ARAPlugInInstanceRoleFlags roles) {
    constexpr auto kAllRoles =
        ARA::kARAPlaybackRendererRole | ARA::kARAEditorRendererRole | ARA::kARAEditorViewRole;
    return host_->bindDocumentToPluginInstance(instance, kAllRoles, roles);
}

juce::ARAHostModel::PlaybackRegion* AraDocument::region(ClipId clip, EventId event) const {
    const auto found = regions_.find({clip, event});
    return found == regions_.end() ? nullptr : found->second.region.get();
}

}  // namespace magda::daw::audio::ara
