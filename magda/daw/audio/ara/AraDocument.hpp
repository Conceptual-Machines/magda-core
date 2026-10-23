#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "AraHostControllers.hpp"
#include "AraRegionTiming.hpp"
#include "core/ClipTypes.hpp"
#include "core/TypeIds.hpp"
#include "transport/TempoMap.hpp"

/**
 * @file AraDocument.hpp
 * @brief One plugin's ARA document for a project: the host side of its model graph (#1579).
 *
 * Owned by the adapter layer, outside any plan: the plugin's analysis lives as long as this
 * does, through every graph rebuild. Message thread throughout, except the controllers'
 * callbacks, which say for themselves.
 */

namespace magda::daw::audio::ara {

/** @brief One audio event the document should hold a playback region for. */
struct AraRegionSpec {
    ClipId clipId = INVALID_CLIP_ID;
    EventId eventId = INVALID_EVENT_ID;
    TrackId trackId = INVALID_TRACK_ID;
    SourceId sourceId = INVALID_SOURCE_ID;
    std::string sourcePath;

    AraRegionTiming timing;
    std::string name;

    /// The IDs the plugin's archive names these objects by; derived from the path and the
    /// event when empty. Only the first spec naming a source sets its ID.
    std::string sourcePersistentId;
    std::string modificationPersistentId;

    bool operator==(const AraRegionSpec&) const = default;
};

/**
 * @brief An ARA document controller and the sources, modifications and regions mirrored into it.
 *
 * The mapping is one audio source per pooled Source, one modification and one region per audio
 * event, one region sequence per track, and a single musical context for the project.
 */
class AraDocument {
  public:
    using EventKey = std::pair<ClipId, EventId>;

    /**
     * @brief A document over @p factory, or null when the plugin refuses to create one.
     *
     * @p anchor is an unbound instance of the same plugin, held until after the controller is
     * gone: JUCE's factory does not keep the plugin's module loaded, and on Linux a VST3 module
     * unloads with its last instance, taking the controller's code with it.
     */
    static std::unique_ptr<AraDocument> create(juce::ARAFactoryWrapper factory,
                                               std::unique_ptr<juce::AudioPluginInstance> anchor,
                                               const juce::String& name,
                                               engine::AudioFileReaderFactory& files,
                                               AraModelEvents events = {},
                                               AraTransportRequests transport = {});

    ~AraDocument();

    AraDocument(const AraDocument&) = delete;
    AraDocument& operator=(const AraDocument&) = delete;

    /** @brief Hand the plugin @p map's tempo and bar grid, and tell it they changed. */
    void setMusicalContext(const engine::TempoMap& map);

    /**
     * @brief Bring the document to exactly @p regions, in one edit.
     *
     * Regions, modifications and sources no spec names are removed; a source whose path moved
     * is recreated with everything on it. Returns a line per spec whose file could not be
     * opened, which gets no region.
     */
    std::vector<std::string> syncRegions(const std::vector<AraRegionSpec>& regions);

    /**
     * @brief Bind @p instance to this document with @p roles, or null when it will not.
     *
     * Before the instance is prepared: ARA fixes an instance's roles at binding. Done here
     * rather than through JUCE, whose wrapper hides the editor-view interface select() needs;
     * wrap the result in juce::ARAHostModel::PlugInExtensionInstance for the renderers.
     */
    const ARA::ARAPlugInExtensionInstance* bind(juce::AudioPluginInstance& instance,
                                                ARA::ARAPlugInInstanceRoleFlags roles);

    /**
     * @brief Tell @p instance's editor to show @p events' regions.
     *
     * Plugins such as Melodyne show, and analyse, only what the host's selection names.
     */
    void select(const ARA::ARAPlugInExtensionInstance* instance,
                const std::vector<EventKey>& events);

    /**
     * @brief The events whose regions were created since the last call, still present.
     *
     * A host assigns these to its renderers after the edit that made them; regions removed by a
     * sync leave the renderers on their own (JUCE's registry listens for their deletion).
     */
    std::vector<EventKey> takeNewRegions();

    /**
     * @brief Ask the plugin to analyse every source it has not finished with.
     *
     * For the content types its factory says it can analyse; some plugins wait for this call
     * rather than starting on their own when a source appears.
     */
    void requestAnalysis();

    /** @brief The region for one event, or null. */
    juce::ARAHostModel::PlaybackRegion* region(ClipId clip, EventId event) const;

    std::size_t sourceCount() const {
        return sources_.size();
    }
    std::size_t regionCount() const {
        return regions_.size();
    }

    ARA::Host::DocumentController& controller() const;

    /** @brief The event a modification the plugin reported on belongs to, if it is still here. */
    std::optional<EventKey> eventFor(ARA::ARAAudioModificationHostRef modification) const;

    /** @brief The event a region the plugin reported on belongs to, if it is still here. */
    std::optional<EventKey> eventFor(ARA::ARAPlaybackRegionHostRef region) const;

    /** @brief The ID the plugin's factory writes its archives under. */
    std::string archiveId() const;

    /** @brief Whether this plugin can read an archive written under @p id. */
    bool canRead(const std::string& id) const;

    /** @brief The whole document as the plugin archives it, or nothing when it refuses. */
    std::optional<juce::MemoryBlock> store();

    /**
     * @brief Build @p regions and inject @p archive's state into them, in one edit.
     *
     * Objects are matched by persistent ID, so @p regions must carry the IDs the archive was
     * stored with. Returns the lines syncRegions() refused, plus one if the plugin could not
     * read the archive.
     */
    std::vector<std::string> restore(const std::vector<AraRegionSpec>& regions,
                                     const AraArchiveIn& archive);

  private:
    struct SourceEntry {
        AraSourceRecord record;
        std::string persistentId;
        std::unique_ptr<juce::ARAHostModel::AudioSource> source;
    };

    struct ModificationEntry {
        SourceId sourceId = INVALID_SOURCE_ID;
        std::string persistentId;
        std::unique_ptr<juce::ARAHostModel::AudioModification> modification;
    };

    struct RegionEntry {
        TrackId trackId = INVALID_TRACK_ID;
        std::string name;
        AraRegionTiming timing;
        std::unique_ptr<juce::ARAHostModel::PlaybackRegion> region;
    };

    struct SequenceEntry {
        std::string name;
        std::unique_ptr<juce::ARAHostModel::RegionSequence> sequence;
    };

    AraDocument(AraModelEvents events, AraTransportRequests transport);

    void dropEvent(const EventKey& key);
    void dropSource(SourceId id);
    /** @brief Point @p source at @p path if that file has the same shape; false if not. */
    bool repoint(SourceEntry& source, const std::string& path);
    SourceEntry* ensureSource(const AraRegionSpec& spec, std::vector<std::string>& refused);
    SequenceEntry& ensureSequence(TrackId track);
    void placeRegion(const AraRegionSpec& spec, SourceEntry& source);

    // Before the controller: its interfaces hold references to these.
    AraModelEvents events_;
    AraTransportRequests transport_;
    engine::AudioFileReaderFactory* files_ = nullptr;
    AraContentAccess* content_ = nullptr;

    // Outlives the controller: it is what keeps the controller's module mapped.
    std::unique_ptr<juce::AudioPluginInstance> anchor_;

    std::unique_ptr<juce::ARAHostDocumentController> host_;

    // After the controller, in dependency order: destroyed regions first, context last.
    std::unique_ptr<juce::ARAHostModel::MusicalContext> context_;
    std::map<TrackId, SequenceEntry> sequences_;
    std::map<SourceId, std::unique_ptr<SourceEntry>> sources_;
    std::map<EventKey, ModificationEntry> modifications_;
    std::map<EventKey, RegionEntry> regions_;
    std::vector<EventKey> newRegions_;
};

}  // namespace magda::daw::audio::ara
