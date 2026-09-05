#include "plugins/MidiStrumPlugin.hpp"

#include <algorithm>
#include <cmath>

namespace magda::daw::audio {

const char* MidiStrumPlugin::xmlTypeName = "magda_strum";

namespace {

// ---------------------------------------------------------------------------
// Strum curve (ported from the Pluck/Percussion scheduler). Cubic-Bezier shape
// presets (some non-monotonic) rasterized into a LUT, then tiled by `cycles`.
// ---------------------------------------------------------------------------
struct Shape {
    const char* name;
    float c1x, c1y, c2x, c2y;
};

const std::array<Shape, 8>& shapes() {
    static const std::array<Shape, 8> s{{
        {"Linear", 0.33f, 0.33f, 0.66f, 0.66f},
        {"Ease In", 0.62f, 0.03f, 0.96f, 0.40f},
        {"Ease Out", 0.04f, 0.60f, 0.38f, 0.97f},
        {"Snap", 0.02f, 0.85f, 0.18f, 1.00f},
        {"Spike", 0.82f, 0.00f, 0.98f, 0.18f},
        {"S-Curve", 0.70f, 0.00f, 0.30f, 1.00f},
        {"Overshoot", 0.55f, -0.45f, 0.45f, 1.45f},
        {"Bounce", 0.30f, 1.40f, 0.70f, -0.40f},
    }};
    return s;
}

float bez1(float a, float b, float c, float d, float s) noexcept {
    const float m = 1.0f - s;
    return m * m * m * a + 3.0f * m * m * s * b + 3.0f * m * s * s * c + s * s * s * d;
}

void buildLut(int shapeIdx, std::array<float, 1024>& lut) {
    const auto& sh = shapes()[static_cast<size_t>(juce::jlimit(0, 7, shapeIdx))];
    constexpr int K = 512;
    std::array<float, K + 1> xs{}, ys{};
    for (int k = 0; k <= K; ++k) {
        const float s = static_cast<float>(k) / K;
        xs[static_cast<size_t>(k)] = bez1(0.0f, sh.c1x, sh.c2x, 1.0f, s);
        ys[static_cast<size_t>(k)] = bez1(0.0f, sh.c1y, sh.c2y, 1.0f, s);
    }
    int si = 0;
    for (int i = 0; i < 1024; ++i) {
        const float x = static_cast<float>(i) / 1023.0f;
        while (si + 1 <= K && xs[static_cast<size_t>(si + 1)] < x)
            ++si;
        const int j = std::min(si + 1, K);
        const float span = xs[static_cast<size_t>(j)] - xs[static_cast<size_t>(si)];
        const float f = span > 1.0e-6f ? (x - xs[static_cast<size_t>(si)]) / span : 0.0f;
        const float y = ys[static_cast<size_t>(si)] +
                        f * (ys[static_cast<size_t>(j)] - ys[static_cast<size_t>(si)]);
        lut[static_cast<size_t>(i)] = juce::jlimit(-0.5f, 1.5f, y);
    }
}

float sampleLut(const std::array<float, 1024>& lut, float t) noexcept {
    t = juce::jlimit(0.0f, 1.0f, t);
    const float p = t * 1023.0f;
    const int i0 = static_cast<int>(p);
    const int i1 = std::min(i0 + 1, 1023);
    const float fr = p - static_cast<float>(i0);
    return lut[static_cast<size_t>(i0)] * (1.0f - fr) + lut[static_cast<size_t>(i1)] * fr;
}

// `cycles` tiled copies of the curve across [0,1] -> repeated mini-strums.
float sampleCycled(const std::array<float, 1024>& lut, float u, int cycles) noexcept {
    cycles = juce::jmax(1, cycles);
    const float t = juce::jlimit(0.0f, 1.0f, u) * static_cast<float>(cycles);
    const int k = std::min(static_cast<int>(t), cycles - 1);
    return (static_cast<float>(k) + sampleLut(lut, t - static_cast<float>(k))) /
           static_cast<float>(cycles);
}

/// One slot's metadata. The ids, order and display ranges are pinned to what
/// the retired host-native plugin registered, because saved links address the
/// slots by index and projects store parameter values in display units.
ParameterInfo slotInfo(int index) {
    ParameterInfo info;
    info.paramIndex = index;

    const auto discrete = [&info](const char* id, const char* name, float def,
                                  std::vector<juce::String> choices) {
        info.stableId = id;
        info.name = name;
        info.scale = ParameterScale::Discrete;
        info.minValue = 0.0f;
        info.maxValue = static_cast<float>(choices.size() - 1);
        info.defaultValue = def;
        info.choices = std::move(choices);
    };

    switch (index) {
        case MidiStrumPlugin::kTrigger:
            discrete("trigger", "Trigger", 0.0f, {"Chord", "Loop"});
            break;

        case MidiStrumPlugin::kOrder:
            discrete("order", "Order", 0.0f, {"Up", "Down", "Up/Down", "As Played"});
            break;

        case MidiStrumPlugin::kShape: {
            std::vector<juce::String> names;
            for (const auto& shape : shapes())
                names.emplace_back(shape.name);
            discrete("shape", "Shape", 1.0f, std::move(names));  // Ease In
            break;
        }

        case MidiStrumPlugin::kCycles:
            discrete("cycles", "Cycles", 0.0f, {"1", "2", "3", "4", "5", "6", "7", "8"});
            break;

        case MidiStrumPlugin::kLoopSync:
            discrete("loopsync", "Loop Sync", 0.0f, {"Time", "Beat"});
            break;

        case MidiStrumPlugin::kLoopRate:
            discrete("looprate", "Loop Rate", 2.0f,  // 1/4
                     {"1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T"});
            break;

        case MidiStrumPlugin::kStrumLength:
            info.stableId = "strumlength";
            info.name = "Strum Length";
            info.unit = "ms";
            info.minValue = 1.0f;
            info.maxValue = 400.0f;
            info.defaultValue = 90.0f;
            break;

        case MidiStrumPlugin::kSyncInterval:
            info.stableId = "syncinterval";
            info.name = "Sync Interval";
            info.unit = "ms";
            info.minValue = 60.0f;
            info.maxValue = 2000.0f;
            info.defaultValue = 500.0f;
            break;

        default:
            break;
    }

    return info;
}

}  // namespace

// ===========================================================================

MidiStrumPlugin::MidiStrumPlugin() {
    for (int index = 0; index < kNumParams; ++index) {
        const auto info = slotInfo(index);
        domains_[static_cast<size_t>(index)] = ParameterUtils::domainOf(info);
        values_[static_cast<size_t>(index)] =
            ParameterUtils::realToNormalized(info.defaultValue, info);
    }

    buildLut(displayIndex(kShape), lut_);
    lutShape_ = displayIndex(kShape);
}

MidiStrumPlugin::~MidiStrumPlugin() = default;

ParameterInfo MidiStrumPlugin::parameterInfo(int index) const {
    if (index < 0 || index >= kNumParams)
        return {};
    return slotInfo(index);
}

float MidiStrumPlugin::parameterValue(int index) const {
    if (index < 0 || index >= kNumParams)
        return 0.0f;
    return values_[static_cast<size_t>(index)];
}

void MidiStrumPlugin::setParameterValue(int index, float value) {
    if (index < 0 || index >= kNumParams)
        return;
    values_[static_cast<size_t>(index)] = juce::jlimit(0.0f, 1.0f, value);
}

float MidiStrumPlugin::displayValue(int index) const {
    return ParameterUtils::normalizedToReal(values_[static_cast<size_t>(index)],
                                            domains_[static_cast<size_t>(index)]);
}

int MidiStrumPlugin::displayIndex(int index) const {
    return juce::roundToInt(displayValue(index));
}

double MidiStrumPlugin::loopRateToBeats(int rateIndex) {
    // Index order must match the UI division list: 1/1, 1/2, 1/4, 1/4T, 1/8,
    // 1/8T, 1/16, 1/16T. Value is the loop length in quarter-note beats.
    switch (juce::jlimit(0, kNumLoopRates - 1, rateIndex)) {
        case 0:
            return 4.0;  // 1/1
        case 1:
            return 2.0;  // 1/2
        case 2:
            return 1.0;  // 1/4
        case 3:
            return 2.0 / 3.0;  // 1/4T
        case 4:
            return 0.5;  // 1/8
        case 5:
            return 1.0 / 3.0;  // 1/8T
        case 6:
            return 0.25;  // 1/16
        case 7:
            return 1.0 / 6.0;  // 1/16T
        default:
            return 1.0;
    }
}

int MidiStrumPlugin::loopIntervalSamples(const DeviceProcessContext& context) const {
    const auto mode = static_cast<LoopSync>(displayIndex(kLoopSync));
    if (mode == LoopSync::Beat) {
        const double beats = loopRateToBeats(displayIndex(kLoopRate));
        const double bpm =
            context.tempoMap != nullptr
                ? juce::jmax(1.0, context.tempoMap->bpmAtSeconds(context.timelineStartSeconds))
                : 120.0;
        const double secs = beats * 60.0 / bpm;
        return static_cast<int>(secs * sampleRate_);
    }
    const float ms = displayValue(kSyncInterval);
    return static_cast<int>(ms * 0.001f * sampleRate_);
}

void MidiStrumPlugin::prepare(const DevicePrepareContext& context) {
    MidiMagdaDevice::prepare(context);
    resetStrumState();
}

void MidiStrumPlugin::reset() {
    resetStrumState();
}

void MidiStrumPlugin::resetStrumState() {
    heldCount_ = 0;
    pendingCount_ = 0;
    soundingCount_ = 0;
    clock_ = 0;
    collectLeft_ = -1;
    syncLeft_ = 0;
}

void MidiStrumPlugin::addHeld(int note, int velocity, std::uint32_t sourceId) {
    removeHeld(note);
    if (heldCount_ < MAX_HELD)
        held_[static_cast<size_t>(heldCount_++)] = {note, velocity, noteOrder_++, sourceId};
}

void MidiStrumPlugin::removeHeld(int note) {
    int keep = 0;
    for (int i = 0; i < heldCount_; ++i)
        if (held_[static_cast<size_t>(i)].note != note)
            held_[static_cast<size_t>(keep++)] = held_[static_cast<size_t>(i)];
    heldCount_ = keep;
}

void MidiStrumPlugin::queuePending(const Pending& event) {
    if (pendingCount_ < MAX_PENDING)
        pending_[static_cast<size_t>(pendingCount_++)] = event;
}

void MidiStrumPlugin::scheduleReleaseAll() {
    // Note-ons still waiting belong to a chord that is over, so they are
    // dropped rather than fired and left hanging until the next release.
    int keep = 0;
    for (int i = 0; i < pendingCount_; ++i)
        if (!pending_[static_cast<size_t>(i)].gateOn)
            pending_[static_cast<size_t>(keep++)] = pending_[static_cast<size_t>(i)];
    pendingCount_ = keep;

    for (int i = 0; i < soundingCount_; ++i) {
        const auto& note = sounding_[static_cast<size_t>(i)];
        queuePending({clock_, note.note, 0, false, note.sourceId});
    }
    // Every sounding note now has its note-off queued for this block, so a
    // second call in the same block must not queue a duplicate.
    soundingCount_ = 0;
}

void MidiStrumPlugin::scheduleStrum() {
    if (heldCount_ == 0)
        return;

    // A strum supersedes the pass before it in both modes: what that pass left
    // ringing is released, and what it had not played yet is dropped. Chord
    // mode used to re-strum straight over the sounding notes, so a chord change
    // gave a downstream instrument a second note-on per pitch with no note-off
    // between them, and one hung voice each (#2363).
    scheduleReleaseAll();

    auto begin = ordered_.begin();
    auto end = begin + heldCount_;
    std::copy(held_.begin(), held_.begin() + heldCount_, begin);

    const int ord = displayIndex(kOrder);
    if (ord == 0)  // Up
        std::sort(begin, end, [](const Held& a, const Held& b) { return a.note < b.note; });
    else if (ord == 1)  // Down
        std::sort(begin, end, [](const Held& a, const Held& b) { return a.note > b.note; });
    else if (ord == 2) {  // Up-Down
        std::sort(begin, end, [](const Held& a, const Held& b) { return a.note < b.note; });
        for (int i = heldCount_ - 2; i >= 1; --i)
            *end++ = ordered_[static_cast<size_t>(i)];
    } else  // As Played
        std::sort(begin, end, [](const Held& a, const Held& b) { return a.order < b.order; });

    const int N = static_cast<int>(end - begin);
    const float W = displayValue(kStrumLength) * 0.001f * static_cast<float>(sampleRate_);
    const int cyc = displayIndex(kCycles) + 1;  // index 0..7 -> 1..8

    for (int i = 0; i < N; ++i) {
        const float u = (N == 1) ? 0.0f : static_cast<float>(i) / static_cast<float>(N - 1);
        const float onset = juce::jlimit(0.0f, W, sampleCycled(lut_, u, cyc) * W);
        const auto& source = ordered_[static_cast<size_t>(i)];
        queuePending({clock_ + static_cast<std::int64_t>(onset), source.note,
                      juce::jlimit(1, 127, source.velocity), true, source.sourceId});
    }
}

void MidiStrumPlugin::process(DeviceProcessContext& context) {
    if (context.midiIn == nullptr || context.midiOut == nullptr || context.numSamples <= 0)
        return;
    const auto& in = *context.midiIn;
    auto& midi = *context.midiOut;
    const int n = context.numSamples;

    // Stop mid-strum: the upstream clip stops sending note-offs, so release
    // everything sounding and drop the latched chord on the playing->stopped
    // edge to avoid hung notes downstream. (The note-offs queued here fire in
    // step 3 below.)
    if (wasPlaying_ && !context.isPlaying) {
        scheduleReleaseAll();
        heldCount_ = 0;
        collectLeft_ = -1;
        syncLeft_ = 0;
    }
    wasPlaying_ = context.isPlaying;

    // Rebuild the curve LUT when Shape changes.
    const int shp = displayIndex(kShape);
    if (shp != lutShape_) {
        buildLut(shp, lut_);
        lutShape_ = shp;
    }

    const bool chordMode = static_cast<Trigger>(displayIndex(kTrigger)) == Trigger::Chord;

    // --- 1. Latch the held chord from incoming MIDI.
    const bool wasEmpty = heldCount_ == 0;
    for (int eventIndex = 0; eventIndex < in.size(); ++eventIndex) {
        const auto& msg = in.message(eventIndex);
        if (msg.isNoteOn()) {
            addHeld(msg.getNoteNumber(), msg.getVelocity(), in.sourceId(eventIndex));
            collectLeft_ = juce::jmax(1, static_cast<int>(0.03 * sampleRate_));
        } else if (msg.isNoteOff()) {
            removeHeld(msg.getNoteNumber());
        } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
            heldCount_ = 0;
        }
    }

    // Chord released -> end what the strum is still holding or owes.
    if (heldCount_ == 0 && !wasEmpty) {
        scheduleReleaseAll();
        collectLeft_ = -1;
        syncLeft_ = 0;
    }

    // --- 2. Trigger logic (clock_ at block start is the strum base).
    if (chordMode) {
        if (collectLeft_ >= 0) {
            collectLeft_ -= n;
            if (collectLeft_ <= 0) {
                scheduleStrum();
                collectLeft_ = -1;
            }
        }
    } else if (heldCount_ != 0) {
        syncLeft_ -= n;
        if (syncLeft_ <= 0) {
            scheduleStrum();
            syncLeft_ = juce::jmax(1, loopIntervalSamples(context));
        }
    }

    // --- 3. Emit pending events that fall in this block, in time order
    //        (note-offs before note-ons at the same instant, so retriggers work).
    const double blockSecs = static_cast<double>(n) / sampleRate_;
    dueCount_ = 0;
    int stillPending = 0;
    for (int i = 0; i < pendingCount_; ++i) {
        const auto& event = pending_[static_cast<size_t>(i)];
        if (event.fireAt < clock_ + n)
            due_[static_cast<size_t>(dueCount_++)] = event;
        else
            pending_[static_cast<size_t>(stillPending++)] = event;
    }
    pendingCount_ = stillPending;

    auto* dueBegin = due_.data();
    auto* dueEnd = dueBegin + dueCount_;
    std::sort(dueBegin, dueEnd, [](const Pending& a, const Pending& b) {
        if (a.fireAt != b.fireAt)
            return a.fireAt < b.fireAt;
        return a.gateOn < b.gateOn;  // false (note-off) before true (note-on)
    });

    int lastDisplayNote = -1, lastDisplayVel = 0;
    for (auto* p = dueBegin; p != dueEnd; ++p) {
        const double tib =
            juce::jlimit(0.0, blockSecs, static_cast<double>(p->fireAt - clock_) / sampleRate_);
        if (p->gateOn) {
            // Up/Down sounds the inner notes twice in one pass. Close the first
            // one here, or the buffer carries two note-ons a pitch and only the
            // single note-off `sounding_` tracks (#2363).
            auto* soundingBegin = sounding_.data();
            auto* soundingEnd = soundingBegin + soundingCount_;
            auto* held = std::find_if(soundingBegin, soundingEnd,
                                      [p](const Sounding& s) { return s.note == p->note; });
            if (held != soundingEnd) {
                auto release = juce::MidiMessage::noteOff(1, p->note);
                release.setTimeStamp(tib);
                midi.addEvent({std::move(release), held->sourceId});
                held->sourceId = p->sourceId;
            } else if (soundingCount_ < MAX_HELD) {
                sounding_[static_cast<size_t>(soundingCount_++)] = {p->note, p->sourceId};
            }

            auto message =
                juce::MidiMessage::noteOn(1, p->note, static_cast<juce::uint8>(p->velocity));
            message.setTimeStamp(tib);
            midi.addEvent({std::move(message), p->sourceId});
            lastDisplayNote = p->note;
            lastDisplayVel = p->velocity;
        } else {
            auto message = juce::MidiMessage::noteOff(1, p->note);
            message.setTimeStamp(tib);
            midi.addEvent({std::move(message), p->sourceId});
            int keep = 0;
            for (int i = 0; i < soundingCount_; ++i)
                if (sounding_[static_cast<size_t>(i)].note != p->note)
                    sounding_[static_cast<size_t>(keep++)] = sounding_[static_cast<size_t>(i)];
            soundingCount_ = keep;
        }
    }
    if (lastDisplayNote >= 0)
        setMidiOutDisplay(lastDisplayNote, lastDisplayVel);
    else if (soundingCount_ == 0)
        clearMidiOutDisplay();

    clock_ += n;
}

std::vector<float> MidiStrumPlugin::curveOnsetPreview(int shapeIndex, int cyclesIndex, int count) {
    std::vector<float> out;
    if (count <= 0)
        return out;

    std::array<float, 1024> lut{};
    buildLut(shapeIndex, lut);
    const int cyc = juce::jlimit(0, 7, cyclesIndex) + 1;  // index 0..7 -> 1..8

    out.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const float u = (count == 1) ? 0.0f : static_cast<float>(i) / static_cast<float>(count - 1);
        out.push_back(juce::jlimit(0.0f, 1.0f, sampleCycled(lut, u, cyc)));
    }
    return out;
}

}  // namespace magda::daw::audio
