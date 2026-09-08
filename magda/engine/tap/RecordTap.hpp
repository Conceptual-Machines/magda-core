#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * @file RecordTap.hpp
 * @brief What the pass in flight has reached, for whoever draws it (#2463).
 *
 * The fourth tap, following LaunchTap.hpp. A level tap reports a block, a value
 * tap a number, a launch tap a slot; this reports a recording pass: where it
 * started, how much it covers, the notes it has captured and the peaks its
 * audio reached.
 *
 * Written by the block that recorded the material, from the transport that
 * stamped it, so what is drawn and what is written cannot disagree. Nothing
 * polls, and no frame timer decides the length.
 *
 * A note is positioned here as io/MidiTakeRecorder.hpp positions it in the
 * finished clip: the same conversion, off the same pass origin, so the preview
 * and the clip do not resolve a beat differently.
 *
 * One writer, the audio thread. Any number of readers, none of them consuming.
 */

namespace magda::engine {

/// Where the pass is going. Slice 6 (#2464) is what fills in the slot half.
enum class RecordTarget : std::uint8_t { arrangement, slot };

/// What the pass is made of, and so what a reader draws.
enum class RecordMaterial : std::uint8_t { audio, midi };

/** @brief One note of the pass, in the beats the take will hold it at. */
struct RecordedNote {
    int noteNumber = 0;
    int velocity = 0;

    /// From the pass start, the same origin MidiTake's notes are relative to.
    double startBeat = 0.0;

    /// Up to the pass end while the note is still down.
    double lengthBeats = 0.0;
};

/** @brief One tick of the pass's audio, at RecordTap::samplesPerPeak(). */
struct RecordedPeak {
    float left = 0.0f;
    float right = 0.0f;
};

/** @brief How much of a pass a tap can describe. */
struct RecordTapSettings {
    RecordTarget target = RecordTarget::arrangement;

    /// Which slot, for a pass whose target is one.
    int scene = -1;

    /// Notes and peaks a pass may hold, sized here because the audio thread
    /// cannot allocate. Zero is a take nobody draws, which still reports its
    /// length.
    std::size_t maxNotes = 0;
    std::size_t maxPeaks = 0;

    /// How much of the take one peak covers. A pass's peaks are evenly spaced
    /// in its own samples, so a reader lays them across its length.
    int samplesPerPeak = 1024;
};

/**
 * @brief The pass in flight, published by the block that recorded it.
 *
 * The slots are atomic, relaxed on both sides, for the reason SampleRing.hpp
 * gives: plain memory read beside a live writer is a data race rather than a
 * ragged frame.
 *
 * Two things make a reading one pass's own rather than two.
 *
 * Where the pass is and how far it has got are the scalars a wrap resets
 * together, so they are published behind a sequence the reader retries over.
 * That window is those stores and nothing else, which is what makes the retry
 * converge.
 *
 * The notes are not in it: an array is as long as the pass is, and a reader
 * copying one inside a window would be racing the writer over a growing amount
 * of work. Each slot is published by its own identity instead -- cleared before
 * it is filled and written last -- and the identity carries the pass that
 * recorded it. A reader takes the slots whose identity names the pass it is
 * reporting and is unchanged either side of reading the position, so a wrap
 * during a read costs notes off the end of the list, never a note in the wrong
 * place.
 *
 * The peaks carry neither, and are the one thing here that can be ragged: a
 * wrap landing inside a read redraws a few ticks of the previous pass's audio,
 * for the frame it takes to ask again. Peaks are presentation and place
 * nothing.
 */
class RecordTap {
    static_assert(std::atomic<double>::is_always_lock_free,
                  "a record tap is written on the audio thread and must not take a lock");

  public:
    RecordTap(RecordMaterial material, const RecordTapSettings& settings)
        : material_(material),
          settings_(settings),
          notes_(settings.maxNotes),
          peaks_(settings.maxPeaks) {
        held_.fill(kNotHeld);
        heldSlots_.reserve(settings.maxNotes);
    }

    /** @brief The pass as it stands. */
    struct Reading {
        bool recording = false;
        RecordMaterial material = RecordMaterial::audio;
        RecordTarget target = RecordTarget::arrangement;
        int scene = -1;

        /// Where the pass sits on the timeline, and how much of it it covers.
        double startBeat = 0.0;
        double lengthBeats = 0.0;

        /// Which pass of the take this is. Zero until one has opened, which is
        /// what tells an armed track with a stopped transport from a pass of no
        /// length.
        std::uint32_t pass = 0;

        std::vector<RecordedNote> notes;
        std::vector<RecordedPeak> peaks;

        /// Notes and peaks the pass had no room for.
        std::int64_t notesLost = 0;
        std::int64_t peaksLost = 0;
    };

    /**
     * @brief Read the pass into @p into. Off the audio thread.
     *
     * Reuses @p into's storage, so a repaint reading every frame allocates
     * once.
     */
    void read(Reading& into) const;

    Reading read() const {
        Reading reading;
        read(reading);
        return reading;
    }

    int samplesPerPeak() const {
        return settings_.samplesPerPeak;
    }

    /**
     * @brief A pass begins at @p startBeat on the timeline. Audio thread.
     *
     * Leaves the notes and peaks of the pass it replaces behind: what is drawn
     * is the pass in flight, and a loop wrap starts another one.
     */
    void open(double startBeat);

    /// @brief The pass, and every note still down, reach @p lengthBeats.
    void extend(double lengthBeats);

    /// @brief No pass in flight. What the last one reached stands, so an
    /// overlay holds until the clip it became appears.
    void close() {
        recording_.store(false, std::memory_order_relaxed);
    }

    /// @brief A note went down at @p beat from the pass start. Audio thread.
    void noteOn(int channel, int note, int velocity, double beat);

    /// @brief The note came up at @p beat from the pass start. Audio thread.
    void noteOff(int channel, int note, double beat);

    /**
     * @brief Fold a block of the take's own audio into the pass's peaks.
     *
     * @p passSample is where the block's first sample sits in the pass, counted
     * in the samples the take wrote, so a peak lands where the audio behind it
     * did.
     */
    void addPeaks(juce::dsp::AudioBlock<const float> audio, int numSamples,
                  std::int64_t passSample);

  private:
    /// Channels the held table spans. A note is published without one, as
    /// MidiTake holds it, but two channels playing middle C are two notes.
    static constexpr std::size_t kChannels = 16;
    static constexpr std::size_t kNotesPerChannel = 128;

    static constexpr std::uint32_t kNotHeld = 0xffffffffU;

    /// Odd while the pass scalars are being replaced, even while they stand.
    /// The pass ordinal is what is left once that bit is taken off.
    static constexpr std::uint32_t kOpening = 1U;

    /// A reader only ever retries over a handful of stores. Past this the audio
    /// thread was preempted mid-open, and one frame of a start beside the next
    /// pass's length is cheaper than spinning until it is rescheduled.
    static constexpr int kReadAttempts = 64;

    /// A note's identity and which pass recorded it. Zero while the slot is
    /// being filled, which is what makes an overwrite visible to a reader
    /// partway through one.
    struct NoteSlot {
        std::atomic<std::uint64_t> identity{0};
        std::atomic<double> startBeat{0.0};
        std::atomic<double> lengthBeats{0.0};
    };

    static std::uint64_t packIdentity(std::uint32_t pass, int note, int velocity) {
        return static_cast<std::uint64_t>(note & 0x7f) |
               (static_cast<std::uint64_t>(velocity & 0x7f) << 8U) |
               (static_cast<std::uint64_t>(pass) << 32U);
    }

    static std::uint64_t packPeak(float left, float right) {
        return (static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(left)) << 32U) |
               std::bit_cast<std::uint32_t>(right);
    }

    static RecordedPeak unpackPeak(std::uint64_t word) {
        return {std::bit_cast<float>(static_cast<std::uint32_t>(word >> 32U)),
                std::bit_cast<float>(static_cast<std::uint32_t>(word & 0xffffffffULL))};
    }

    static std::size_t heldIndex(int channel, int note) {
        return ((static_cast<std::size_t>(channel) % kChannels) * kNotesPerChannel) +
               (static_cast<std::size_t>(note) & (kNotesPerChannel - 1));
    }

    /// One note into @p at, published by the identity written last.
    void writeSlot(std::uint32_t at, int note, int velocity, double beat);

    /// One tick's peak, folded into what the pass already put there. Assigned
    /// on the tick's first touch instead, which is what leaves the last pass's
    /// audio behind without clearing the array on every wrap.
    void writePeak(std::size_t slot, juce::dsp::AudioBlock<const float> audio, int offset,
                   int numSamples, bool first);

    RecordMaterial material_;
    RecordTapSettings settings_;

    std::vector<NoteSlot> notes_;
    std::vector<std::atomic<std::uint64_t>> peaks_;

    /// Which slot holds each channel's note while it is down. Audio thread
    /// only, like every index below it.
    std::array<std::uint32_t, kChannels * kNotesPerChannel> held_{};

    /// The slots still open, so extending them costs the chord rather than the
    /// table.
    std::vector<std::uint32_t> heldSlots_;

    /// The pass the writer is on, so a note can be tagged without reading the
    /// sequence back.
    std::uint32_t pass_ = 0;

    std::atomic<std::uint32_t> seq_{0};
    std::atomic<bool> recording_{false};
    std::atomic<double> startBeat_{0.0};
    std::atomic<double> lengthBeats_{0.0};

    std::atomic<std::size_t> numNotes_{0};
    std::atomic<std::int64_t> notesLost_{0};

    /// Peaks the pass has reached, which is what it holds once capped.
    std::atomic<std::int64_t> peaksNeeded_{0};
};

}  // namespace magda::engine
