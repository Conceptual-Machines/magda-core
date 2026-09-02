#include "clip/ClipSnapshotDump.hpp"

#include <iomanip>
#include <sstream>

namespace magda::engine {
namespace {

std::string fixed(double value, int decimals) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(decimals) << value;
    return out.str();
}

std::string hex(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << value;
    return out.str();
}

/// The file's name, never its path: a golden test that carried an absolute
/// path would pass on the machine that wrote it and nowhere else.
std::string fileName(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

const char* curveName(FadeCurve curve) {
    switch (curve) {
        case FadeCurve::Linear:
            return "lin";
        case FadeCurve::Convex:
            return "cvx";
        case FadeCurve::Concave:
            return "ccv";
        case FadeCurve::SCurve:
            return "scv";
    }
    return "lin";
}

std::string spanText(const SnapshotSpan& span) {
    return fixed(span.beats.start, 3) + ".." + fixed(span.beats.end, 3) + "b " +
           fixed(span.seconds.start, 3) + ".." + fixed(span.seconds.end, 3) + "s";
}

void dumpHoles(std::ostringstream& out, const std::vector<SnapshotSpan>& holes) {
    for (const auto& hole : holes)
        out << "    hole " << spanText(hole) << "\n";
}

void dumpEvent(std::ostringstream& out, const AudioEventPlayback& event) {
    out << "    event " << event.eventId << " src=" << event.sourceId
        << " file=" << (event.filePath.empty() ? std::string("-") : fileName(event.filePath))
        << " rate=" << fixed(event.sourceSampleRate, 0) << " span=" << spanText(event.span)
        << " anchor=" << event.anchorSamples;

    if (event.loopEnabled)
        out << " loop=" << event.loopStartSamples << "+" << event.loopLengthSamples;
    else
        out << " loop=off";

    out << " stretch=" << event.timeStretchMode << " speed=" << fixed(event.speedRatio, 3)
        << " bpm=" << fixed(event.interpBpm, 1);

    if (event.autoTempo)
        out << " auto-tempo";
    // Off the flag, because a warped event with nothing left in its map is
    // still warped and still plays on the beat face. Points rather than
    // markers: what the clip plays is the compiled map, and a dump echoing the
    // model's count would hide exactly the case the compile diagnoses.
    if (event.warpEnabled)
        out << " warp=" << event.warp.points.size()
            << " warp-max=" << fixed(event.warp.maxSourcePerWarp(), 3);
    if (event.analogPitch)
        out << " analog-pitch";
    if (event.autoPitch)
        out << " auto-pitch=" << event.autoPitchMode;
    if (event.pitchChange != 0.0f)
        out << " pitch=" << fixed(event.pitchChange, 2);
    if (event.transpose != 0)
        out << " transpose=" << event.transpose;
    if (event.reversed)
        out << " reversed";
    if (!event.leftChannelActive || !event.rightChannelActive)
        out << " channels=" << (event.leftChannelActive ? "L" : "-")
            << (event.rightChannelActive ? "R" : "-");
    if (event.gainDb != 0.0f)
        out << " gain=" << fixed(event.gainDb, 1);

    // The event's own fades, printed only when it has any: for the one event
    // that spans its clip these repeat what the clip line already showed, and
    // what they are here for is the clip that holds several.
    if (event.fadeInSeconds > 0.0 || event.fadeOutSeconds > 0.0) {
        out << " fade=" << fixed(event.fadeInSeconds, 3) << "/" << fixed(event.fadeOutSeconds, 3)
            << " curve=" << curveName(event.fadeInCurve) << "/" << curveName(event.fadeOutCurve);
        if (event.fadeInBehaviour != 0 || event.fadeOutBehaviour != 0)
            out << " behaviour=" << event.fadeInBehaviour << "/" << event.fadeOutBehaviour;
    }

    out << "\n";
}

void dumpAudioClip(std::ostringstream& out, const AudioClipPlayback& clip) {
    out << "  audio clip=" << clip.clipId << " span=" << spanText(clip.span)
        << " fade=" << fixed(clip.fadeInSeconds, 3) << "/" << fixed(clip.fadeOutSeconds, 3)
        << " curve=" << curveName(clip.fadeInCurve) << "/" << curveName(clip.fadeOutCurve)
        << " behaviour=" << clip.fadeInBehaviour << "/" << clip.fadeOutBehaviour
        << " gain=" << fixed(clip.gainDb, 1) << " pan=" << fixed(clip.pan, 2)
        << " launch=" << clip.launchFadeSamples << "\n";

    dumpHoles(out, clip.silenced);
    for (const auto& event : clip.events)
        dumpEvent(out, event);
}

void dumpMidiClip(std::ostringstream& out, const MidiClipPlayback& clip) {
    // The compiled list rather than the model's, because that is what plays:
    // notes are already note-on/note-off pairs and curves are already the
    // messages they send, so a count here is a count of what reaches a synth.
    auto noteOns = 0;
    auto noteOffs = 0;
    for (const auto& event : clip.events.events) {
        if (event.isNoteOn())
            ++noteOns;
        else if (event.isNoteOff())
            ++noteOffs;
    }

    out << "  midi clip=" << clip.clipId << " span=" << spanText(clip.span)
        << " events=" << clip.events.events.size() << " on=" << noteOns << " off=" << noteOffs
        << " ctl=" << clip.events.controllers.size();

    if (clip.events.mpe)
        out << " mpe";

    if (clip.fold.loopEnabled)
        out << " loop=" << fixed(clip.fold.loopStartBeats, 3) << "+"
            << fixed(clip.fold.loopLengthBeats, 3);
    else
        out << " loop=off";

    out << " offset=" << fixed(clip.fold.offsetBeats, 3)
        << " trim=" << fixed(clip.fold.trimOffsetBeats, 3);

    if (!clip.groove.empty())
        out << " groove=" << fixed(clip.groove.maxDisplacementBeats(), 4);

    out << "\n";
    dumpHoles(out, clip.silenced);
}

}  // namespace

std::string dumpClipSnapshot(const ClipSnapshot& snapshot) {
    std::ostringstream out;
    out << "magda-clip-snapshot v" << snapshot.version << "\n";
    out << "tempo=" << hex(snapshot.tempoFingerprint) << " tracks=" << snapshot.tracks.size()
        << "\n";

    for (const auto& track : snapshot.tracks) {
        out << "track " << track.trackId << " audio=" << track.audio.size()
            << " midi=" << track.midi.size() << " session=" << track.session.size() << "\n";
        for (const auto& clip : track.audio)
            dumpAudioClip(out, clip);
        for (const auto& clip : track.midi)
            dumpMidiClip(out, clip);

        // The session, in the same detail as the arrangement. A dump that
        // printed only the counts would let two materially different session
        // snapshots compare identical, which is the one thing this file exists
        // to prevent (#2301).
        for (const auto& slot : track.session) {
            out << "  slot scene=" << slot.sceneIndex << " length=" << fixed(slot.lengthBeats, 3)
                << "b audio=" << slot.audio.size() << " midi=" << slot.midi.size() << "\n";
            for (const auto& clip : slot.audio)
                dumpAudioClip(out, clip);
            for (const auto& clip : slot.midi)
                dumpMidiClip(out, clip);
        }
    }

    for (const auto& diagnostic : snapshot.diagnostics)
        out << "diagnostic: " << diagnostic << "\n";

    return out.str();
}

}  // namespace magda::engine
