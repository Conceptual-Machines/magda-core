#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "../core/DefaultColourPalette.hpp"
#include "../core/TempoUtils.hpp"
#include "../core/TypeIds.hpp"
#include "version.hpp"

namespace magda {

struct ProjectTimelineMarker {
    int id = 0;
    double positionBeats = 0.0;
    juce::String name;
    std::uint32_t colourArgb = 0xFFFFC857;
};

inline constexpr int kDefaultSessionSceneCount = 8;

/** Durable metadata for one ordered Session row. */
struct ProjectScene {
    SceneId id = INVALID_SCENE_ID;
    juce::String name;
    std::uint32_t colourArgb = 0;

    bool operator==(const ProjectScene&) const = default;
};

inline ProjectScene makeDefaultProjectScene(SceneId id, int zeroBasedIndex) {
    return {id, "Scene " + juce::String(zeroBasedIndex + 1),
            kDefaultColourPalette[static_cast<std::size_t>(zeroBasedIndex) %
                                  kDefaultColourPalette.size()]
                .colour};
}

inline void ensureProjectSceneCount(struct ProjectInfo& info, int count);

/**
 * @brief Author-facing project metadata (title, credits, rights).
 *
 * The field set is DAWproject's MetaData element verbatim - see
 * third_party/dawproject/MetaData.xsd - so the whole block maps one to one onto
 * metadata.xml in both directions with nothing to reconcile. Every field is an
 * optional string; empty means "not set" and is not written to either format.
 *
 * `year` is a string rather than an int because the schema says xs:string, and
 * because the things people actually type there ("1998", "2003-2005", "MMXIV")
 * do not all survive a parse to int.
 */
struct ProjectMetadata {
    juce::String title;
    juce::String artist;
    juce::String album;
    juce::String originalArtist;
    juce::String composer;
    juce::String songwriter;
    juce::String producer;
    juce::String arranger;
    juce::String year;
    juce::String genre;
    juce::String copyright;
    juce::String website;
    juce::String comment;

    bool isEmpty() const;
};

/// One metadata field, the two names it goes by outside the struct, and whether
/// a new project can be seeded with a stored value for it.
struct ProjectMetadataField {
    const char* key;      ///< .mgd JSON property, and the StringTable leaf under
                          ///< "project_settings.metadata."
    const char* element;  ///< DAWproject <MetaData> child element name
    juce::String ProjectMetadata::*member;

    /// True for the fields that describe the person rather than the work, which
    /// are the same in every project somebody makes and are worth keeping a
    /// per-install default for. False for title, album, original artist, year
    /// and comment: a stored default for those would be wrong every time.
    bool seededFromDefaults;
};

/**
 * Every metadata field, in the order MetaData.xsd declares them.
 *
 * The order is load-bearing: metaData is an xs:sequence, so an export that
 * writes the elements in any other order fails schema validation, and the
 * Project Settings dialog lays the fields out in the order it reads them here.
 *
 * Keep the list in schema order and everything that touches metadata iterates
 * it rather than spelling out thirteen fields apiece and drifting apart: the
 * four mapping sites (native save, native load, DAWproject write, DAWproject
 * read), the two dialogs that build a row per field, and the seeding in
 * ProjectManager::newProject.
 */
inline constexpr std::array<ProjectMetadataField, 13> kProjectMetadataFields{{
    {"title", "Title", &ProjectMetadata::title, false},
    {"artist", "Artist", &ProjectMetadata::artist, true},
    {"album", "Album", &ProjectMetadata::album, false},
    {"originalArtist", "OriginalArtist", &ProjectMetadata::originalArtist, false},
    {"composer", "Composer", &ProjectMetadata::composer, true},
    {"songwriter", "Songwriter", &ProjectMetadata::songwriter, true},
    {"producer", "Producer", &ProjectMetadata::producer, true},
    {"arranger", "Arranger", &ProjectMetadata::arranger, true},
    {"year", "Year", &ProjectMetadata::year, false},
    {"genre", "Genre", &ProjectMetadata::genre, true},
    {"copyright", "Copyright", &ProjectMetadata::copyright, true},
    {"website", "Website", &ProjectMetadata::website, true},
    {"comment", "Comment", &ProjectMetadata::comment, false},
}};

inline bool ProjectMetadata::isEmpty() const {
    for (const auto& field : kProjectMetadataFields)
        if ((this->*field.member).isNotEmpty())
            return false;
    return true;
}

struct ProjectColourEntry {
    std::uint32_t colour = kDefaultColourPalette.front().colour;
    juce::String name = kDefaultColourPalette.front().name;

    bool operator==(const ProjectColourEntry&) const = default;
};

/**
 * Defaults used when new content is added to this project.
 *
 * Config seeds this block when the project is created. From then on the saved
 * project owns the values, so opening it with a different user configuration
 * does not change how new tracks and clips are initialized.
 */
struct ProjectDefaults {
    int zoomViewBars = 32;
    bool autoCrossfade = true;
    bool overlapPlaysBoth = false;
    bool chordPreview = false;
    bool postFxPostFader = true;
    int clipColourMode = 0;  // 0 = inherit track, 1 = cycle through colourPalette
    std::vector<ProjectColourEntry> colourPalette = [] {
        std::vector<ProjectColourEntry> palette;
        palette.reserve(kDefaultColourPalette.size());
        for (const auto& entry : kDefaultColourPalette)
            palette.push_back({entry.colour, entry.name});
        return palette;
    }();

    std::uint32_t colourForIndex(int index) const {
        if (colourPalette.empty())
            return kDefaultColourPalette.front().colour;
        const auto positiveIndex = index < 0 ? 0U : static_cast<std::size_t>(index);
        return colourPalette[positiveIndex % colourPalette.size()].colour;
    }
};

inline constexpr int kDefaultTimelineLengthBars = 256;

struct ProjectCreationSettings {
    int timelineLengthBars = kDefaultTimelineLengthBars;
    ProjectDefaults defaults;
};

/**
 * @brief Project-level settings and state
 *
 * Contains all project-level information including tempo, time signature,
 * loop settings, file path, and the ProjectMetadata credits block.
 */
struct ProjectInfo {
    juce::String name;
    juce::String filePath;  // .mgd file path

    juce::String autosaveMediaDirectory;

    // Playback settings
    double tempo = DEFAULT_BPM;
    int timeSignatureNumerator = DEFAULT_TIME_SIGNATURE_NUMERATOR;
    int timeSignatureDenominator = DEFAULT_TIME_SIGNATURE_DENOMINATOR;
    double projectLength = 240.0;  // seconds (legacy; derived from timelineLengthBars)
    double sampleRate = 44100.0;   // project working/render sample rate

    // Total timeline length (per-project; seeded from Config default for new projects)
    int timelineLengthBars = kDefaultTimelineLengthBars;

    // Creation and initial-view defaults, captured from Config for new projects.
    ProjectDefaults defaults;

    // Render / bounce settings (per-project)
    /// The engine that last wrote this project, by its setting word
    /// (AudioEngineChoice.hpp). Empty in every project saved before the field
    /// existed, which is a Tracktion project by definition (#2437).
    juce::String savedWithEngine;

    int renderBitDepth = 24;  // 16, 24, 32
    int bounceBitDepth = 32;  // 16, 24, 32 (default 32-bit float for internal bounces)

    // Key signature
    int keyRoot = -1;    // 0=C, 1=C#, ..., 11=B; -1=none
    int keyQuality = 0;  // 0=major, 1=minor

    // Title and credits. `title` is distinct from `name`, which is the project's
    // own name and follows the .mgd file, so a song called "Blue" can live in
    // blue_v7.mgd - but an empty title means "inherit", and the name is what
    // gets shown and written. Nobody has to retype a name they already gave.
    ProjectMetadata metadata;

    // Loop settings (beats are authoritative, seconds derived from tempo)
    bool loopEnabled = false;
    double loopStartBeats = 0.0;
    double loopEndBeats = 0.0;

    // Named timeline markers (positions are stored in beats)
    std::vector<ProjectTimelineMarker> markers;

    // Session rows. Vector order is the zero-based sceneIndex presented at API
    // and engine boundaries; id is the durable identity used by clients.
    std::vector<ProjectScene> scenes;
    SceneId nextSceneId = 1;

    // Zoom/scroll state
    double horizontalZoom = -1.0;  // Pixels per beat (-1 = use default)
    double verticalZoom = 1.0;     // Track height multiplier
    int scrollX = 0;               // Horizontal scroll position
    int scrollY = 0;               // Vertical scroll position

    // Active view (0=Live/Session, 1=Arrange, 2=Mix, 3=Master)
    int activeView = 1;  // Default to Arrange

    // Version tracking
    juce::String version = MAGDA_VERSION;  // Magda version
    juce::Time lastModified;

    // Parameter aliases (UserProject layer, opaque JSON blob managed by AliasRegistry)
    juce::var paramAliases;

    // Project-scope bindings (opaque JSON blob managed by BindingRegistry)
    juce::var projectBindings;

    // Default constructor
    ProjectInfo() : lastModified(juce::Time::getCurrentTime()) {
        scenes.reserve(kDefaultSessionSceneCount);
        for (int index = 0; index < kDefaultSessionSceneCount; ++index)
            scenes.push_back(makeDefaultProjectScene(nextSceneId++, index));
    }

    // Helper to update modification time
    void touch() {
        lastModified = juce::Time::getCurrentTime();
    }
};

inline void ensureProjectSceneCount(ProjectInfo& info, int count) {
    count = std::max(0, count);
    while (static_cast<int>(info.scenes.size()) < count) {
        const auto index = static_cast<int>(info.scenes.size());
        info.scenes.push_back(makeDefaultProjectScene(info.nextSceneId++, index));
    }
}

}  // namespace magda
