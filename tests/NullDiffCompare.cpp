#include "NullDiffCompare.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <map>
#include <sstream>

namespace magda::nulldiff {

namespace {

constexpr double kNegativeInfinityDb = -std::numeric_limits<double>::infinity();

double toDb(double amplitude) {
    if (amplitude <= 0.0)
        return kNegativeInfinityDb;
    return 20.0 * std::log10(amplitude);
}

double fromDb(double db) {
    if (db == kNegativeInfinityDb)
        return 0.0;
    return std::pow(10.0, db / 20.0);
}

/// Channel-summed value, so a shift search reads one signal rather than
/// correlating each channel and hoping they agree.
double summed(const juce::AudioBuffer<float>& buffer, int sample) {
    double total = 0.0;
    for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
        total += static_cast<double>(buffer.getSample(channel, sample));
    return total;
}

/// Where the material starts, which is where a priming offset is visible and
/// where a case that drifts has not drifted yet.
int onsetOf(const juce::AudioBuffer<float>& buffer) {
    auto peak = 0.0;
    for (auto sample = 0; sample < buffer.getNumSamples(); ++sample)
        peak = std::max(peak, std::abs(summed(buffer, sample)));

    if (peak <= 0.0)
        return 0;

    const auto threshold = std::max(1.0e-4, peak * 0.01);
    for (auto sample = 0; sample < buffer.getNumSamples(); ++sample)
        if (std::abs(summed(buffer, sample)) >= threshold)
            return sample;

    return 0;
}

std::string sampleAddress(std::int64_t sample, double sampleRate) {
    char text[96];
    std::snprintf(text, sizeof(text), "sample %lld (%.4f s)", static_cast<long long>(sample),
                  sampleRate > 0.0 ? static_cast<double>(sample) / sampleRate : 0.0);
    return text;
}

// --- MIDI helpers ------------------------------------------------------------

constexpr int kNoteOff = 0x80;
constexpr int kNoteOn = 0x90;
constexpr int kControlChange = 0xB0;
constexpr int kPitchBend = 0xE0;

/// A controller stream is keyed by what a synth would keep separate. Pitch bend
/// has no controller number, so it takes one that no controller can.
struct ControllerKey {
    int channel = 1;
    int controller = 0;

    bool operator<(const ControllerKey& other) const {
        return channel != other.channel ? channel < other.channel : controller < other.controller;
    }
};

constexpr int kPitchBendController = -1;

struct ControllerPoint {
    std::int64_t sample = 0;
    int value = 0;
};

using ControllerFunction = std::vector<ControllerPoint>;

std::map<ControllerKey, ControllerFunction> controllerFunctions(const MidiStream& stream,
                                                                std::int64_t shift) {
    std::map<ControllerKey, ControllerFunction> functions;

    for (const auto& event : stream) {
        ControllerKey key;
        key.channel = event.channel();

        int value = 0;
        if (event.type() == kControlChange) {
            key.controller = event.data1;
            value = event.data2;
        } else if (event.type() == kPitchBend) {
            key.controller = kPitchBendController;
            value = (static_cast<int>(event.data2) << 7) | static_cast<int>(event.data1);
        } else {
            continue;
        }

        functions[key].push_back({event.sample - shift, value});
    }

    for (auto& [key, points] : functions)
        std::stable_sort(points.begin(), points.end(),
                         [](const auto& a, const auto& b) { return a.sample < b.sample; });

    return functions;
}

/// The value the function holds at @p sample.
///
/// Before the first point it holds that point's value rather than nothing. A
/// controller nobody has set has no value at all, and the alternative
/// conventions both lie: zero invents a message neither engine sent, and
/// "undefined" would fail every case at instant zero over a difference in when
/// the first message goes out, which cannot be wrong when the value is the one
/// both agree on.
int valueAt(const ControllerFunction& points, std::int64_t sample) {
    if (points.empty())
        return 0;

    auto upper =
        std::upper_bound(points.begin(), points.end(), sample,
                         [](std::int64_t s, const ControllerPoint& p) { return s < p.sample; });
    if (upper == points.begin())
        return points.front().value;

    return std::prev(upper)->value;
}

/// Whether the function takes a value within @p tolerance of @p target at any
/// instant in [from, to]. Every value in the window is a candidate, not just
/// its extremes: a window holding 0 and 127 would otherwise accept anything
/// between them, which is most of the range a controller has.
bool holdsValueNear(const ControllerFunction& points, std::int64_t from, std::int64_t to,
                    int target, int tolerance) {
    if (points.empty())
        return false;

    if (std::abs(valueAt(points, from) - target) <= tolerance)
        return true;

    auto first =
        std::upper_bound(points.begin(), points.end(), from,
                         [](std::int64_t s, const ControllerPoint& p) { return s < p.sample; });
    for (auto point = first; point != points.end() && point->sample <= to; ++point)
        if (std::abs(point->value - target) <= tolerance)
            return true;

    return false;
}

/// Drop messages that repeat the value before them. The fork emits on its grid
/// whether the value moved or not, and a repeat says nothing about the curve:
/// counting it would make a stream that flattens early look longer than one
/// that stopped when it had nothing left to say.
ControllerFunction collapseRepeats(const ControllerFunction& points) {
    ControllerFunction collapsed;
    for (const auto& point : points)
        if (collapsed.empty() || collapsed.back().value != point.value)
            collapsed.push_back(point);
    return collapsed;
}

std::string describeKey(const ControllerKey& key) {
    char text[64];
    if (key.controller == kPitchBendController)
        std::snprintf(text, sizeof(text), "pitch bend ch%d", key.channel);
    else
        std::snprintf(text, sizeof(text), "cc%d ch%d", key.controller, key.channel);
    return text;
}

}  // namespace

// =============================================================================
// Audio
// =============================================================================

ShiftEstimate estimateShift(const juce::AudioBuffer<float>& native,
                            const juce::AudioBuffer<float>& incumbent, int maxShiftSamples) {
    ShiftEstimate estimate;

    if (native.getNumSamples() == 0 || incumbent.getNumSamples() == 0 || maxShiftSamples <= 0)
        return estimate;

    const auto onset = std::min(onsetOf(native), onsetOf(incumbent));

    // Wide enough that a wrong lag cannot correlate as well as the right one by
    // accident, which for periodic material means several periods.
    const auto available = std::min(native.getNumSamples(), incumbent.getNumSamples()) - onset;
    const auto window = std::min(std::max(4 * maxShiftSamples, 4096), std::max(0, available));
    if (window <= 0)
        return estimate;

    auto best = -1.0;
    auto bestLag = 0;

    for (auto lag = -maxShiftSamples; lag <= maxShiftSamples; ++lag) {
        double dot = 0.0;
        double nativeEnergy = 0.0;
        double incumbentEnergy = 0.0;

        for (auto index = 0; index < window; ++index) {
            const auto nativeIndex = onset + index;
            const auto incumbentIndex = nativeIndex + lag;
            if (nativeIndex >= native.getNumSamples())
                break;
            if (incumbentIndex < 0 || incumbentIndex >= incumbent.getNumSamples())
                continue;

            const auto a = summed(native, nativeIndex);
            const auto b = summed(incumbent, incumbentIndex);
            dot += a * b;
            nativeEnergy += a * a;
            incumbentEnergy += b * b;
        }

        if (nativeEnergy <= 0.0 || incumbentEnergy <= 0.0)
            continue;

        const auto correlation = dot / std::sqrt(nativeEnergy * incumbentEnergy);
        if (correlation > best) {
            best = correlation;
            bestLag = lag;
        }
    }

    estimate.correlation = best;
    estimate.samples = bestLag;

    // A comparator that slides until something matches will always find
    // something. What it finds on a pair that differs in content is a fiction
    // that makes the case pass, so a weak best lag is no lag at all.
    estimate.found = best >= 0.98;
    if (!estimate.found)
        estimate.samples = 0;

    return estimate;
}

AudioComparison compareAudio(const juce::AudioBuffer<float>& native,
                             const juce::AudioBuffer<float>& incumbent,
                             const AudioCompareOptions& options) {
    AudioComparison result;
    result.floorUsed = options.floorDb;
    result.lengthDifference = native.getNumSamples() - incumbent.getNumSamples();

    if (native.getNumChannels() != incumbent.getNumChannels()) {
        result.refusal = "channel counts differ: " + std::to_string(native.getNumChannels()) +
                         " against " + std::to_string(incumbent.getNumChannels());
        return result;
    }

    if (native.getNumSamples() == 0 || incumbent.getNumSamples() == 0) {
        result.refusal = "one render is empty";
        return result;
    }

    if (options.measureShift) {
        const auto estimate = estimateShift(native, incumbent, options.maxShiftSamples);
        result.shiftSamples = estimate.samples;
        result.shiftNotFound = !estimate.found;
    }

    const auto shift = result.shiftSamples;
    const auto begin = static_cast<std::int64_t>(std::max(0, -shift));
    const auto end = std::min(static_cast<std::int64_t>(native.getNumSamples()),
                              static_cast<std::int64_t>(incumbent.getNumSamples()) - shift);

    if (end <= begin) {
        result.refusal = "nothing overlaps once the shift is applied";
        return result;
    }

    const auto floorLinear = fromDb(options.floorDb);
    auto peak = 0.0;
    auto sumOfSquares = 0.0;

    for (auto channel = 0; channel < native.getNumChannels(); ++channel) {
        const auto* a = native.getReadPointer(channel);
        const auto* b = incumbent.getReadPointer(channel);

        for (auto index = begin; index < end; ++index) {
            const auto residual =
                static_cast<double>(a[index]) - static_cast<double>(b[index + shift]);
            const auto magnitude = std::abs(residual);

            if (magnitude > peak)
                peak = magnitude;
            sumOfSquares += residual * residual;

            if (result.firstDivergence < 0 && magnitude > floorLinear)
                result.firstDivergence = index;
        }
    }

    result.comparedSamples = end - begin;
    result.peakDb = toDb(peak);

    const auto count =
        static_cast<double>(result.comparedSamples) * static_cast<double>(native.getNumChannels());
    result.rmsDb = toDb(count > 0.0 ? std::sqrt(sumOfSquares / count) : 0.0);

    return result;
}

// =============================================================================
// MIDI
// =============================================================================

MidiLifetime pairNotes(const MidiStream& stream) {
    MidiLifetime lifetime;

    auto sorted = stream;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const MidiEvent& a, const MidiEvent& b) { return a.sample < b.sample; });

    // Keyed by channel and pitch, because that is what a synth keeps separate,
    // and first in first out within a key so that two notes of one pitch pair
    // the way they sounded.
    std::map<int, std::deque<MidiNoteSpan>> pending;

    for (const auto& event : sorted) {
        const auto type = event.type();
        const auto isOff = type == kNoteOff || (type == kNoteOn && event.data2 == 0);
        const auto isOn = type == kNoteOn && event.data2 > 0;

        if (!isOn && !isOff)
            continue;

        const auto key = (event.channel() << 8) | static_cast<int>(event.data1);

        if (isOn) {
            MidiNoteSpan note;
            note.channel = event.channel();
            note.pitch = event.data1;
            note.velocity = event.data2;
            note.onSample = event.sample;
            note.offSample = event.sample;
            pending[key].push_back(note);
            continue;
        }

        auto entry = pending.find(key);
        if (entry == pending.end() || entry->second.empty()) {
            ++lifetime.unmatchedOffs;
            continue;
        }

        auto note = entry->second.front();
        entry->second.pop_front();
        note.offSample = event.sample;
        lifetime.notes.push_back(note);
    }

    for (const auto& [key, notes] : pending)
        lifetime.hanging += static_cast<int>(notes.size());

    std::stable_sort(lifetime.notes.begin(), lifetime.notes.end(),
                     [](const MidiNoteSpan& a, const MidiNoteSpan& b) {
                         if (a.onSample != b.onSample)
                             return a.onSample < b.onSample;
                         if (a.channel != b.channel)
                             return a.channel < b.channel;
                         return a.pitch < b.pitch;
                     });

    return lifetime;
}

MidiComparison compareMidi(const MidiStream& native, const MidiStream& incumbent,
                           const MidiCompareOptions& options) {
    MidiComparison result;

    const auto nativeLifetime = pairNotes(native);
    auto incumbentLifetime = pairNotes(incumbent);

    result.nativeHanging = nativeLifetime.hanging;
    result.incumbentHanging = incumbentLifetime.hanging;

    // Each stream is checked on its own before the two are compared. A note
    // left hanging by the fork is not a reason to accept one here.
    if (nativeLifetime.hanging > 0)
        result.problems.push_back("native left " + std::to_string(nativeLifetime.hanging) +
                                  " note(s) hanging");
    if (nativeLifetime.unmatchedOffs > 0)
        result.problems.push_back("native sent " + std::to_string(nativeLifetime.unmatchedOffs) +
                                  " note-off(s) for notes it never started");
    if (incumbentLifetime.hanging > 0)
        result.problems.push_back("incumbent left " + std::to_string(incumbentLifetime.hanging) +
                                  " note(s) hanging");
    if (incumbentLifetime.unmatchedOffs > 0)
        result.problems.push_back("incumbent sent " +
                                  std::to_string(incumbentLifetime.unmatchedOffs) +
                                  " note-off(s) for notes it never started");

    // The declared shift brings the incumbent into the native domain. Declared,
    // never fitted: the only thing it stands for is the fork dropping
    // midiOffset on an unlooped arranger clip.
    for (auto& note : incumbentLifetime.notes) {
        note.onSample -= options.noteShiftSamples;
        note.offSample -= options.noteShiftSamples;
    }

    // Everything neither side's comparison reaches, counted. The fork sends
    // channel pressure with every MPE note; dropping that in silence is how a
    // difference nobody counted becomes indistinguishable from no difference.
    const auto countUncompared = [](const MidiStream& stream) {
        auto uncompared = 0;
        for (const auto& event : stream) {
            const auto type = event.type();
            if (type != kNoteOn && type != kNoteOff && type != kControlChange && type != kPitchBend)
                ++uncompared;
        }
        return uncompared;
    };

    result.nativeUncompared = countUncompared(native);
    result.incumbentUncompared = countUncompared(incumbent);

    if (result.nativeUncompared != result.incumbentUncompared)
        result.problems.push_back("messages outside notes, controllers and pitch bend: " +
                                  std::to_string(result.nativeUncompared) +
                                  " in the native render against " +
                                  std::to_string(result.incumbentUncompared) + " in the incumbent");

    std::vector<char> matched(incumbentLifetime.notes.size(), 0);
    const auto tolerance = static_cast<std::int64_t>(options.noteToleranceSamples);
    const auto endEarly = static_cast<std::int64_t>(options.incumbentNoteEndEarlySamples);

    for (const auto& note : nativeLifetime.notes) {
        std::size_t found = incumbentLifetime.notes.size();

        for (std::size_t index = 0; index < incumbentLifetime.notes.size(); ++index) {
            if (matched[index])
                continue;

            const auto& other = incumbentLifetime.notes[index];
            if (other.channel != note.channel || other.pitch != note.pitch)
                continue;
            if (std::abs(other.onSample - note.onSample) > tolerance)
                continue;

            found = index;
            break;
        }

        if (found == incumbentLifetime.notes.size()) {
            ++result.notesOnlyInNative;
            if (result.problems.size() < 24)
                result.problems.push_back("note " + std::to_string(note.pitch) + " ch" +
                                          std::to_string(note.channel) + " at " +
                                          sampleAddress(note.onSample, options.sampleRate) +
                                          " is not in the incumbent");
            continue;
        }

        matched[found] = 1;
        ++result.notesCompared;

        const auto& other = incumbentLifetime.notes[found];
        if (other.velocity != note.velocity ||
            std::abs(other.offSample + endEarly - note.offSample) > tolerance) {
            ++result.notesMismatched;
            if (result.problems.size() < 24)
                result.problems.push_back(
                    "note " + std::to_string(note.pitch) + " ch" + std::to_string(note.channel) +
                    " at " + sampleAddress(note.onSample, options.sampleRate) + ": velocity " +
                    std::to_string(note.velocity) + " against " + std::to_string(other.velocity) +
                    ", ends at " + std::to_string(note.offSample) + " against " +
                    std::to_string(other.offSample));
        }
    }

    for (std::size_t index = 0; index < incumbentLifetime.notes.size(); ++index) {
        if (matched[index])
            continue;

        ++result.notesOnlyInIncumbent;
        const auto& note = incumbentLifetime.notes[index];
        if (result.problems.size() < 24)
            result.problems.push_back("note " + std::to_string(note.pitch) + " ch" +
                                      std::to_string(note.channel) + " at " +
                                      sampleAddress(note.onSample, options.sampleRate) +
                                      " is not in the native render");
    }

    result.notesMatch = result.notesOnlyInNative == 0 && result.notesOnlyInIncumbent == 0 &&
                        result.notesMismatched == 0 && nativeLifetime.hanging == 0 &&
                        nativeLifetime.unmatchedOffs == 0 && incumbentLifetime.hanging == 0 &&
                        incumbentLifetime.unmatchedOffs == 0;

    // --- controllers, as functions of time ---------------------------------
    //
    // The engine's stream IS the curve: a message goes out on every change of
    // the quantised value, so its step function is the quantised curve exactly.
    // The fork's is a sampling of that curve on a 1/16-beat grid. So the test
    // is not that the two functions agree instant for instant, which is false
    // by construction and gets worse the faster the curve moves: a pitch-bend
    // dive over a hundred milliseconds gets three grid points there and about a
    // hundred here, and at most instants the fork simply has not sent the value
    // the curve is at. The test is that the fork's sampling is a faithful one.
    //
    // Three things say that, and between them they catch everything a wrong
    // controller stream can be:
    //
    // - every message the fork sends lands on the engine's curve, within one
    //   grid step either way. A wrong value, a curve evaluated with the wrong
    //   tension and a stream arriving late all break this.
    // - the two cover the same span, first change to last change.
    // - they reach the same extremes.
    //
    // Repeats collapse first. The fork emits on its grid whether the value
    // moved or not, so a curve that flattens for two bars keeps producing
    // identical messages there; those carry no information and would otherwise
    // make its span look longer than the engine's.

    const auto nativeFunctions = controllerFunctions(native, 0);
    const auto incumbentFunctions = controllerFunctions(incumbent, options.noteShiftSamples);

    const auto slack = static_cast<std::int64_t>(
        std::llround(options.staleBeats * 60.0 / std::max(1.0, options.bpm) * options.sampleRate));

    result.controllersMatch = true;

    std::vector<ControllerKey> keys;
    for (const auto& [key, points] : nativeFunctions)
        keys.push_back(key);
    for (const auto& [key, points] : incumbentFunctions)
        if (nativeFunctions.find(key) == nativeFunctions.end())
            keys.push_back(key);
    std::sort(keys.begin(), keys.end());

    const auto fail = [&result](const std::string& message) {
        result.controllersMatch = false;
        if (result.problems.size() < 24)
            result.problems.push_back(message);
    };

    for (const auto& key : keys) {
        const auto nativeEntry = nativeFunctions.find(key);
        const auto incumbentEntry = incumbentFunctions.find(key);

        if (nativeEntry == nativeFunctions.end() || incumbentEntry == incumbentFunctions.end()) {
            fail(describeKey(key) + " is only in the " +
                 (nativeEntry == nativeFunctions.end() ? "incumbent" : "native render"));
            continue;
        }

        const auto curve = collapseRepeats(nativeEntry->second);
        const auto sampled = collapseRepeats(incumbentEntry->second);
        if (curve.empty() || sampled.empty()) {
            fail(describeKey(key) + " has no messages on one side");
            continue;
        }

        auto landed = true;
        for (const auto& point : sampled) {
            if (holdsValueNear(curve, point.sample - slack, point.sample + slack, point.value,
                               options.valueTolerance))
                continue;

            fail(describeKey(key) + ": the incumbent sends " + std::to_string(point.value) +
                 " at " + sampleAddress(point.sample, options.sampleRate) +
                 ", which the native curve is not at within a grid step (it is at " +
                 std::to_string(valueAt(curve, point.sample)) + " there)");

            // One message per controller. Twenty thousand instants of the same
            // disagreement is not twenty thousand findings.
            landed = false;
            break;
        }

        if (!landed)
            continue;

        if (std::abs(curve.front().sample - sampled.front().sample) > slack ||
            std::abs(curve.back().sample - sampled.back().sample) > slack)
            fail(describeKey(key) + ": the streams cover different spans, native " +
                 sampleAddress(curve.front().sample, options.sampleRate) + " to " +
                 sampleAddress(curve.back().sample, options.sampleRate) + ", incumbent " +
                 sampleAddress(sampled.front().sample, options.sampleRate) + " to " +
                 sampleAddress(sampled.back().sample, options.sampleRate));

        // What is deliberately NOT asserted: that the two reach the same
        // extremes. A grid sampler misses the peak of anything moving faster
        // than its grid, and missing it is the divergence rather than a symptom
        // of one. A dive of a hundred milliseconds gets four grid points at 120
        // bpm, so the fork's stream turns round at whatever the curve happened
        // to be at 94 ms and never sends the value at the bottom. Requiring
        // equal extremes would fail every fast curve in the corpus for being
        // exactly what it was predicted to be.
        //
        // Nothing is lost by leaving it out. A stream that stops early in time
        // is caught by the span, and a value the curve never takes is caught by
        // the landing test above, which is the only way an extreme can be wrong
        // rather than merely absent.
    }

    return result;
}

// =============================================================================
// The report
// =============================================================================

const char* toString(Verdict verdict) {
    switch (verdict) {
        case Verdict::Null:
            return "null";
        case Verdict::Shift:
            return "shift";
        case Verdict::Midi:
            return "midi";
        case Verdict::ReportOnly:
            return "report";
    }
    return "?";
}

std::string formatDb(double db) {
    if (!std::isfinite(db))
        return "-inf";

    char text[32];
    std::snprintf(text, sizeof(text), "%.1f", db);
    return text;
}

std::string formatReport(const std::vector<CaseReport>& cases, double sampleRate, int blockSize) {
    std::ostringstream out;

    out << "magda-null-diff v1\n";
    out << "cases=" << cases.size() << " rate=" << static_cast<long long>(sampleRate)
        << " blockSize=" << blockSize << "\n";

    auto index = 0;
    for (const auto& report : cases) {
        char line[256];
        std::snprintf(line, sizeof(line), "[%2d] %-28s %-8s ", ++index, report.name.c_str(),
                      toString(report.verdict));
        out << line;

        if (!report.unmeasurable.empty()) {
            out << "unmeasurable: " << report.unmeasurable;
        } else if (report.hasMidi) {
            const auto& midi = report.midi;
            std::snprintf(line, sizeof(line), "notes=%d%s cc=%s", midi.notesCompared,
                          midi.notesMatch ? "" : "!", midi.controllersMatch ? "ok" : "differs");
            out << line;
        } else if (report.hasAudio) {
            const auto& audio = report.audio;
            std::snprintf(line, sizeof(line), "peak=%-9s rms=%-9s shift=%d",
                          formatDb(audio.peakDb).c_str(), formatDb(audio.rmsDb).c_str(),
                          audio.shiftSamples);
            out << line;
            if (!audio.refusal.empty())
                out << " refused: " << audio.refusal;
        }

        out << (report.passed ? "  ok" : "  FAIL") << "\n";
    }

    return out.str();
}

}  // namespace magda::nulldiff
