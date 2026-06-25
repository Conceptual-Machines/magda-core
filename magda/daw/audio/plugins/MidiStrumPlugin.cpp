#include "plugins/MidiStrumPlugin.hpp"

#include <algorithm>
#include <cmath>

namespace magda::daw::audio {

const char* MidiStrumPlugin::xmlTypeName = "magda_strum";

namespace StrumIDs {
static const juce::Identifier trigger("strumTrigger");
static const juce::Identifier order("strumOrder");
static const juce::Identifier shape("strumShape");
static const juce::Identifier cycles("strumCycles");
static const juce::Identifier strumLength("strumLength");
static const juce::Identifier syncInterval("strumSyncInterval");
}  // namespace StrumIDs

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

}  // namespace

// ===========================================================================

MidiStrumPlugin::MidiStrumPlugin(const te::PluginCreationInfo& info) : MidiDevicePlugin(info) {
    auto um = getUndoManager();
    trigger.referTo(state, StrumIDs::trigger, um, 0);
    order.referTo(state, StrumIDs::order, um, 0);
    shape.referTo(state, StrumIDs::shape, um, 1);  // Ease In
    cycles.referTo(state, StrumIDs::cycles, um, 0);
    strumLength.referTo(state, StrumIDs::strumLength, um, 90.0f);
    syncInterval.referTo(state, StrumIDs::syncInterval, um, 500.0f);

    triggerParam = addParam("trigger", "Trigger", {0.0f, 1.0f, 1.0f});
    orderParam = addParam("order", "Order", {0.0f, 3.0f, 1.0f});
    shapeParam = addParam("shape", "Shape", {0.0f, 7.0f, 1.0f});
    cyclesParam = addParam("cycles", "Cycles", {0.0f, 7.0f, 1.0f});
    strumLengthParam = addParam("strumlength", "Strum Length", {1.0f, 400.0f});
    syncIntervalParam = addParam("syncinterval", "Sync Interval", {60.0f, 2000.0f});

    triggerParam->setParameterFromHost(static_cast<float>(trigger.get()),
                                       juce::dontSendNotification);
    orderParam->setParameterFromHost(static_cast<float>(order.get()), juce::dontSendNotification);
    shapeParam->setParameterFromHost(static_cast<float>(shape.get()), juce::dontSendNotification);
    cyclesParam->setParameterFromHost(static_cast<float>(cycles.get()), juce::dontSendNotification);
    strumLengthParam->setParameterFromHost(strumLength.get(), juce::dontSendNotification);
    syncIntervalParam->setParameterFromHost(syncInterval.get(), juce::dontSendNotification);

    buildLut(shape.get(), lut_);
    lutShape_ = shape.get();
    held_.reserve(16);
    pending_.reserve(128);
    sounding_.reserve(32);

    state.addListener(&paramSyncListener_);
}

MidiStrumPlugin::~MidiStrumPlugin() {
    state.removeListener(&paramSyncListener_);
    notifyListenersOfDeletion();
}

void MidiStrumPlugin::syncParamFromProperty(const juce::Identifier& property) {
    if (property == StrumIDs::trigger && triggerParam)
        triggerParam->setParameterFromHost(static_cast<float>(trigger.get()),
                                           juce::dontSendNotification);
    else if (property == StrumIDs::order && orderParam)
        orderParam->setParameterFromHost(static_cast<float>(order.get()),
                                         juce::dontSendNotification);
    else if (property == StrumIDs::shape && shapeParam)
        shapeParam->setParameterFromHost(static_cast<float>(shape.get()),
                                         juce::dontSendNotification);
    else if (property == StrumIDs::cycles && cyclesParam)
        cyclesParam->setParameterFromHost(static_cast<float>(cycles.get()),
                                          juce::dontSendNotification);
    else if (property == StrumIDs::strumLength && strumLengthParam)
        strumLengthParam->setParameterFromHost(strumLength.get(), juce::dontSendNotification);
    else if (property == StrumIDs::syncInterval && syncIntervalParam)
        syncIntervalParam->setParameterFromHost(syncInterval.get(), juce::dontSendNotification);
}

void MidiStrumPlugin::initialise(const te::PluginInitialisationInfo& info) {
    MidiDevicePlugin::initialise(info);
    resetStrumState();
}

void MidiStrumPlugin::deinitialise() {
    MidiDevicePlugin::deinitialise();
    resetStrumState();
}

void MidiStrumPlugin::reset() {
    resetStrumState();
}

void MidiStrumPlugin::resetStrumState() {
    held_.clear();
    pending_.clear();
    sounding_.clear();
    clock_ = 0;
    collectLeft_ = -1;
    syncLeft_ = 0;
}

float MidiStrumPlugin::controlValue(te::AutomatableParameter* p,
                                    const juce::CachedValue<float>& cv) const {
    return p ? p->getCurrentValue() : cv.get();
}

int MidiStrumPlugin::controlIndex(te::AutomatableParameter* p,
                                  const juce::CachedValue<int>& cv) const {
    return p ? juce::roundToInt(p->getCurrentValue()) : cv.get();
}

void MidiStrumPlugin::scheduleReleaseAll() {
    for (int note : sounding_)
        pending_.push_back({clock_, note, 0, false});
}

void MidiStrumPlugin::scheduleStrum() {
    if (held_.empty())
        return;

    // Sync re-strum: stop whatever is currently ringing before the new pass.
    if (static_cast<Trigger>(controlIndex(triggerParam.get(), trigger)) == Trigger::Sync)
        scheduleReleaseAll();

    std::vector<Held> notes = held_;
    const int ord = controlIndex(orderParam.get(), order);
    if (ord == 0)  // Up
        std::sort(notes.begin(), notes.end(),
                  [](const Held& a, const Held& b) { return a.note < b.note; });
    else if (ord == 1)  // Down
        std::sort(notes.begin(), notes.end(),
                  [](const Held& a, const Held& b) { return a.note > b.note; });
    else if (ord == 2) {  // Up-Down
        std::sort(notes.begin(), notes.end(),
                  [](const Held& a, const Held& b) { return a.note < b.note; });
        for (int i = static_cast<int>(notes.size()) - 2; i >= 1; --i)
            notes.push_back(notes[static_cast<size_t>(i)]);
    } else  // As Played
        std::sort(notes.begin(), notes.end(),
                  [](const Held& a, const Held& b) { return a.order < b.order; });

    const int N = static_cast<int>(notes.size());
    const float W = controlValue(strumLengthParam.get(), strumLength) * 0.001f *
                    static_cast<float>(sampleRate_);
    const int cyc = controlIndex(cyclesParam.get(), cycles) + 1;  // index 0..7 -> 1..8

    for (int i = 0; i < N; ++i) {
        const float u = (N == 1) ? 0.0f : static_cast<float>(i) / static_cast<float>(N - 1);
        const float onset = juce::jlimit(0.0f, W, sampleCycled(lut_, u, cyc) * W);
        const int note = notes[static_cast<size_t>(i)].note;
        const int vel = juce::jlimit(1, 127, notes[static_cast<size_t>(i)].velocity);
        pending_.push_back({clock_ + static_cast<std::int64_t>(onset), note, vel, true});
    }
}

void MidiStrumPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!fc.bufferForMidiMessages)
        return;
    auto& midi = *fc.bufferForMidiMessages;
    const int n = fc.bufferNumSamples;
    if (n <= 0)
        return;

    // Rebuild the curve LUT when Shape changes.
    const int shp = controlIndex(shapeParam.get(), shape);
    if (shp != lutShape_) {
        buildLut(shp, lut_);
        lutShape_ = shp;
    }

    const bool chordMode =
        static_cast<Trigger>(controlIndex(triggerParam.get(), trigger)) == Trigger::Chord;

    // --- 1. Latch the held chord from incoming MIDI, then swallow the input.
    const bool wasEmpty = held_.empty();
    for (const auto& msg : midi) {
        if (msg.isNoteOn()) {
            const int note = msg.getNoteNumber();
            held_.erase(std::remove_if(held_.begin(), held_.end(),
                                       [note](const Held& h) { return h.note == note; }),
                        held_.end());
            held_.push_back({note, msg.getVelocity(), noteOrder_++});
            collectLeft_ = juce::jmax(1, static_cast<int>(0.03 * sampleRate_));
        } else if (msg.isNoteOff()) {
            const int note = msg.getNoteNumber();
            held_.erase(std::remove_if(held_.begin(), held_.end(),
                                       [note](const Held& h) { return h.note == note; }),
                        held_.end());
        } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
            held_.clear();
        }
    }
    midi.clear();  // the strum replaces the raw chord

    // Chord released -> let everything sounding ring off.
    if (held_.empty() && !wasEmpty) {
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
    } else if (!held_.empty()) {
        syncLeft_ -= n;
        if (syncLeft_ <= 0) {
            scheduleStrum();
            const float ms = controlValue(syncIntervalParam.get(), syncInterval);
            syncLeft_ = juce::jmax(1, static_cast<int>(ms * 0.001f * sampleRate_));
        }
    }

    // --- 3. Emit pending events that fall in this block, in time order
    //        (note-offs before note-ons at the same instant, so retriggers work).
    const double blockSecs = static_cast<double>(n) / sampleRate_;
    std::vector<Pending> due;
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->fireAt < clock_ + n) {
            due.push_back(*it);
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
    std::sort(due.begin(), due.end(), [](const Pending& a, const Pending& b) {
        if (a.fireAt != b.fireAt)
            return a.fireAt < b.fireAt;
        return a.gateOn < b.gateOn;  // false (note-off) before true (note-on)
    });

    int lastDisplayNote = -1, lastDisplayVel = 0;
    for (const auto& p : due) {
        const double tib =
            juce::jlimit(0.0, blockSecs, static_cast<double>(p.fireAt - clock_) / sampleRate_);
        if (p.gateOn) {
            midi.addMidiMessage(
                juce::MidiMessage::noteOn(1, p.note, static_cast<juce::uint8>(p.velocity)), tib,
                te::MPESourceID{});
            if (std::find(sounding_.begin(), sounding_.end(), p.note) == sounding_.end())
                sounding_.push_back(p.note);
            lastDisplayNote = p.note;
            lastDisplayVel = p.velocity;
        } else {
            midi.addMidiMessage(juce::MidiMessage::noteOff(1, p.note), tib, te::MPESourceID{});
            sounding_.erase(std::remove(sounding_.begin(), sounding_.end(), p.note),
                            sounding_.end());
        }
    }
    if (lastDisplayNote >= 0)
        setMidiOutDisplay(lastDisplayNote, lastDisplayVel);
    else if (sounding_.empty())
        clearMidiOutDisplay();

    clock_ += n;
}

void MidiStrumPlugin::restorePluginStateFromValueTree(const juce::ValueTree& v) {
    tracktion::copyPropertiesToCachedValues(v, trigger, order, shape, cycles, strumLength,
                                            syncInterval);
}

}  // namespace magda::daw::audio
