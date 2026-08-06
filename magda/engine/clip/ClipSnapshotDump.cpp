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
    return fixed(span.startBeat, 3) + ".." + fixed(span.endBeat, 3) + "b " +
           fixed(span.startSeconds, 3) + ".." + fixed(span.endSeconds, 3) + "s";
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
    if (event.warpEnabled)
        out << " warp=" << event.warpMarkers.size();
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
    out << "  midi clip=" << clip.clipId << " span=" << spanText(clip.span)
        << " notes=" << clip.notes.size() << " cc=" << clip.cc.size()
        << " pb=" << clip.pitchBend.size();

    if (clip.loopEnabled)
        out << " loop=" << fixed(clip.loopStartBeats, 3) << "+" << fixed(clip.loopLengthBeats, 3);
    else
        out << " loop=off";

    out << " offset=" << fixed(clip.offsetBeats, 3) << " trim=" << fixed(clip.trimOffsetBeats, 3);

    if (!clip.grooveTemplate.empty())
        out << " groove=" << clip.grooveTemplate << "@" << fixed(clip.grooveStrength, 2);

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
            << " midi=" << track.midi.size() << "\n";
        for (const auto& clip : track.audio)
            dumpAudioClip(out, clip);
        for (const auto& clip : track.midi)
            dumpMidiClip(out, clip);
    }

    for (const auto& diagnostic : snapshot.diagnostics)
        out << "diagnostic: " << diagnostic << "\n";

    return out.str();
}

}  // namespace magda::engine
