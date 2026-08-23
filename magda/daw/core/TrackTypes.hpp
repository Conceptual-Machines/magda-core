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
 * @brief What a user may put on a track's lane.
 *
 * Having a lane is not the same as taking anything that is aimed at it, and
 * the difference is per type rather than per gesture: a drop, a drag and a
 * nudge all ask this one question.
 */
enum class UserClipAcceptance {
    /// No lane, or a lane the user does not place clips on.
    None,

    /// Audio or MIDI, whichever the material is. The Media track.
    Any,

    /// MIDI only. A multi-out lane belongs to a device's output pair and the
    /// MIDI on it plays the parent instrument, the same way MidiInputRouter
    /// already routes live input through such a track to its parent. Audio has
    /// nothing to do there.
    ///
    /// Worth knowing rather than worth forbidding: deactivating the output pair
    /// erases the track without taking its clips along, so what is placed here
    /// does not outlive that pair.
    MidiOnly,

    /// Chord progressions only -- a .mid carrying CHORD: markers. The lane is
    /// typed rather than closed: dropping a progression on the chord track is
    /// the obvious gesture and it works, while an ordinary MIDI clip landing
    /// there would change what the track means.
    Progressions,
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
    bool hasTimeline = false;

    /// What a user may place on that lane -- drop a file, drag a clip in, nudge
    /// one over. Narrower than hasTimeline on purpose, and not a bool, because
    /// three lanes accept three different things and a bool could only say
    /// which of them were "not Media".
    UserClipAcceptance userClips = UserClipAcceptance::None;

    /// Takes external audio/MIDI input, so it can be monitored and record-armed.
    /// Buses only pass on what reaches them from elsewhere.
    bool takesExternalInput = false;

    /// Can host an instrument device. Buses and the master process signal from
    /// elsewhere and never originate it.
    bool hostsInstrument = false;

    /// Occupies a row in the scrolling arrangement columns. Aux returns do not:
    /// they have their own fixed strip below the arrangement, so a row here as
    /// well would render the same track twice, and the header and content
    /// columns would drift apart by that track's height as you scroll.
    bool occupiesArrangementRow = false;

    /// Can contain child tracks.
    bool canHaveChildren = false;

    /// Contributes to the rendered mix. The chord track is monitor-only.
    bool isRendered = false;

    /// At most one exists in a project, created on demand rather than by the
    /// user adding a track.
    bool isSingleton = false;

    /// Exists because a device's output pair does, and is destroyed with it.
    bool isDeviceOwned = false;
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
        // The regular track. The only one a user puts an arbitrary clip on.
        case TrackType::Media:
            return {
                .hasTimeline = true,
                .userClips = UserClipAcceptance::Any,
                .takesExternalInput = true,
                .hostsInstrument = true,
                .occupiesArrangementRow = true,
                .canHaveChildren = false,
                .isRendered = true,
                .isSingleton = false,
                .isDeviceOwned = false,
            };

        // Sums the children it owns. A bus: nothing originates here.
        case TrackType::Group:
            return {
                .hasTimeline = false,
                .userClips = UserClipAcceptance::None,
                .takesExternalInput = false,
                .hostsInstrument = false,
                .occupiesArrangementRow = true,
                .canHaveChildren = true,
                .isRendered = true,
                .isSingleton = false,
                .isDeviceOwned = false,
            };

        // Fed by sends. No arrangement row of its own -- aux returns live in
        // the fixed strip below, and a row here as well would draw them twice.
        case TrackType::Aux:
            return {
                .hasTimeline = false,
                .userClips = UserClipAcceptance::None,
                .takesExternalInput = false,
                .hostsInstrument = false,
                .occupiesArrangementRow = false,
                .canHaveChildren = false,
                .isRendered = true,
                .isSingleton = false,
                .isDeviceOwned = false,
            };

        // The one output. Takes input like a media track and originates none.
        case TrackType::Master:
            return {
                .hasTimeline = false,
                .userClips = UserClipAcceptance::None,
                .takesExternalInput = true,
                .hostsInstrument = false,
                .occupiesArrangementRow = true,
                .canHaveChildren = false,
                .isRendered = true,
                .isSingleton = true,
                .isDeviceOwned = false,
            };

        // A device's output pair, wearing a lane. MIDI on it plays the parent
        // instrument, which is what MidiInputRouter already does with live
        // input arriving here. Audio has nothing to do on such a lane.
        case TrackType::MultiOut:
            return {
                .hasTimeline = true,
                .userClips = UserClipAcceptance::MidiOnly,
                .takesExternalInput = true,
                .hostsInstrument = true,
                .occupiesArrangementRow = true,
                .canHaveChildren = false,
                .isRendered = true,
                .isSingleton = false,
                .isDeviceOwned = true,
            };

        // Auditions chords and stays out of the mix. Its lane is typed rather
        // than closed: progressions belong on it, ordinary clips do not.
        case TrackType::Chord:
            return {
                .hasTimeline = true,
                .userClips = UserClipAcceptance::Progressions,
                .takesExternalInput = true,
                .hostsInstrument = true,
                .occupiesArrangementRow = true,
                .canHaveChildren = false,
                .isRendered = false,
                .isSingleton = true,
                .isDeviceOwned = false,
            };
    }

    // Unreachable for any declared type; a value cast in from outside the enum
    // gets the most restrictive answer rather than the most permissive one.
    return {};
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
