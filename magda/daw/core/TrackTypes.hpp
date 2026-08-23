#pragma once

namespace magda {

/**
 * @brief What a track is.
 *
 * One axis, and it is the structural one: where a track's signal comes from and
 * who owns the track. It is deliberately NOT a statement about what kind of
 * material a track carries, because that distinction no longer exists -- a
 * Media track holds audio clips and MIDI clips alike, hosts instruments and
 * effects, and takes either kind of input.
 *
 * That is why the ordinals skip 1 and 2: they were Instrument and MIDI, and
 * when those collapsed into one hybrid track the survivor kept the name
 * `Audio`, which then read as a kind for years after it had stopped being one.
 * Renaming it to Media is the point of this enum's current shape -- the numbers
 * are unchanged and pinned, so nothing on disk moves.
 *
 * What each type can do is declared once, in TrackTypeTraits below, rather than
 * re-derived at each call site. Sixteen call sites used to answer questions
 * like "may a clip go here" with their own list of types to exclude, no two
 * lists agreeing, and #2172 is what that produced: a file drop and a clip drag
 * held different views of the same track.
 */
enum class TrackType {
    Media = 0,     // The regular track: clips of any kind, instruments, effects, input
    Group = 3,     // Contains child tracks, routing hub
    Aux = 4,       // Receives from sends
    Master = 5,    // Final output
    MultiOut = 6,  // Output track for multi-out instrument pair
    Chord = 7      // Singleton chord-progression track (monitor-only, excluded from render)
};

/**
 * @brief The facts a track type declares about itself.
 *
 * Every question the app asks of a track type is a field here, so that asking
 * it is a lookup rather than an exclusion list. Adding a track type is a new
 * row in traitsOf() below, and the switch there has no default: a type that
 * forgets to declare itself does not compile.
 */
struct TrackTypeTraits {
    /// Owns a lane in the arrangement that clips live on.
    bool hasTimeline;

    /// A user may put an arbitrary clip on that lane -- drop a file, drag one
    /// in, nudge one over. Narrower than hasTimeline on purpose. A Chord track
    /// has clips and they are progressions, so an ordinary MIDI clip landing
    /// there would change what the track means; a MultiOut lane is erased with
    /// its output pair without taking its clips along, so anything put there is
    /// orphaned the moment the user switches that output off.
    bool acceptsUserClips;

    /// Takes external audio/MIDI input, so it can be monitored and record-armed.
    /// Buses only pass on what reaches them from elsewhere.
    bool takesExternalInput;

    /// Can host an instrument device. Buses and the master process signal from
    /// elsewhere and never originate it.
    bool hostsInstrument;

    /// Occupies a row in the scrolling arrangement columns. Aux returns do not:
    /// they have their own fixed strip below the arrangement, so a row here as
    /// well would render the same track twice, and the header and content
    /// columns would drift apart by that track's height as you scroll.
    bool occupiesArrangementRow;

    /// Can contain child tracks.
    bool canHaveChildren;

    /// Contributes to the rendered mix. The chord track is monitor-only.
    bool isRendered;

    /// At most one exists in a project, created on demand rather than by the
    /// user adding a track.
    bool isSingleton;

    /// Exists because a device's output pair does, and is destroyed with it.
    bool isDeviceOwned;
};

/**
 * @brief The traits of @p type.
 *
 * The switch is exhaustive and has no default, so -Wswitch fails the build on a
 * type that was added without declaring what it is. That is the whole
 * mechanism: the compiler asks the questions this enum used to leave to
 * whoever wrote the next call site.
 */
constexpr TrackTypeTraits traitsOf(TrackType type) {
    switch (type) {
        case TrackType::Media:
            return {/* hasTimeline */ true,
                    /* acceptsUserClips */ true,
                    /* takesExternalInput */ true,
                    /* hostsInstrument */ true,
                    /* occupiesArrangementRow */ true,
                    /* canHaveChildren */ false,
                    /* isRendered */ true,
                    /* isSingleton */ false,
                    /* isDeviceOwned */ false};

        case TrackType::Group:
            return {false, false, false, false, true, true, true, false, false};

        case TrackType::Aux:
            return {false, false, false, false, false, false, true, false, false};

        case TrackType::Master:
            return {false, false, true, false, true, false, true, true, false};

        case TrackType::MultiOut:
            return {true, false, true, true, true, false, true, false, true};

        case TrackType::Chord:
            return {true, false, true, true, true, false, false, true, false};
    }

    // Unreachable for any declared type; a value cast in from outside the enum
    // gets the most restrictive answer rather than the most permissive one.
    return {false, false, false, false, false, false, false, false, false};
}

/**
 * @brief Get display name for track type
 */
inline const char* getTrackTypeName(TrackType type) {
    switch (type) {
        case TrackType::Media:
            return "Media";
        case TrackType::Group:
            return "Group";
        case TrackType::Aux:
            return "Aux";
        case TrackType::Master:
            return "Master";
        case TrackType::MultiOut:
            return "MultiOut";
        case TrackType::Chord:
            return "Chord";
    }
    return "Unknown";
}

/**
 * @brief Deserialize a track type from an integer, mapping legacy values.
 *
 * Old projects stored Instrument=1 and MIDI=2; both map to Media now, which is
 * the track they had already become before it was named that.
 */
inline TrackType trackTypeFromInt(int v) {
    switch (v) {
        case 0:
        case 1:
        case 2:
            return TrackType::Media;
        case 3:
            return TrackType::Group;
        case 4:
            return TrackType::Aux;
        case 5:
            return TrackType::Master;
        case 6:
            return TrackType::MultiOut;
        case 7:
            return TrackType::Chord;
        default:
            return TrackType::Media;
    }
}

/**
 * @brief Check if track type can have children
 */
inline bool canHaveChildren(TrackType type) {
    return traitsOf(type).canHaveChildren;
}

}  // namespace magda
