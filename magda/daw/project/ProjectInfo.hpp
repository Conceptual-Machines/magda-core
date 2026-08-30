#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>
#include <vector>

#include "../core/TempoUtils.hpp"
#include "version.hpp"

namespace magda {

struct ProjectTimelineMarker {
    int id = 0;
    double positionBeats = 0.0;
    juce::String name;
    std::uint32_t colourArgb = 0xFFFFC857;
};

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
 * writes the elements in any other order fails schema validation. Keep this
 * list in schema order and the four sites that map metadata - native save,
 * native load, DAWproject write, DAWproject read - all iterate it instead of
 * spelling out thirteen fields apiece and drifting apart.
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

/**
 * @brief Project-level settings and state
 *
 * Contains all project-level information including tempo, time signature,
 * loop settings, file path, and the ProjectMetadata credits block.
 */
struct ProjectInfo {
    juce::String name;
    juce::String filePath;  // .mgd file path

    // Playback settings
    double tempo = DEFAULT_BPM;
    int timeSignatureNumerator = DEFAULT_TIME_SIGNATURE_NUMERATOR;
    int timeSignatureDenominator = DEFAULT_TIME_SIGNATURE_DENOMINATOR;
    double projectLength = 240.0;  // seconds (legacy; derived from timelineLengthBars)
    double sampleRate = 44100.0;   // project working/render sample rate

    // Total timeline length (per-project; seeded from Config default for new projects)
    int timelineLengthBars = 256;

    // Render / bounce settings (per-project)
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
    ProjectInfo() : lastModified(juce::Time::getCurrentTime()) {}

    // Helper to update modification time
    void touch() {
        lastModified = juce::Time::getCurrentTime();
    }
};

}  // namespace magda
