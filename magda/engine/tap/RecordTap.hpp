#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * @file RecordTap.hpp
 * @brief What the pass in flight has reached, for whoever draws it (#2463).
 *
 * Owned by whoever set the recording up, not by the take, for the reason a
 * LevelTap is owned by the host: it outlives what writes it. A take is closed
 * and destroyed the moment it becomes a clip, and the overlay has to stand
 * until the clip appears in its place.
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
 * It publishes and nothing more. What a note is -- which release closes it,
 * what a second strike does -- is io/TakeNotes.hpp, driven by the take as well,
 * so the overlay and the clip are made of the same transitions.
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
 * ## What a reading promises
 *
 * Two kinds of thing live here and they are not promised alike, which is the
 * whole of the contract (#2527 review).
 *
 * **The pass and its notes are recording metadata, and a reading of them is one
 * moment's.** Which pass it is, where it starts, how many notes it holds and
 * what each of them is are published behind one revision, turned twice around
 * every change to any of them. A reader copies them and checks the revision
 * afterwards; if it moved, the copy is thrown away rather than handed over. So
 * a reading never pairs one pass's start with another's notes, or a note's
 * pitch with a different note's beat.
 *
 * A read that cannot settle returns false and leaves the reader's own reading
 * alone: the last good one is a better answer than a mixed one. The revision
 * turns once per block rather than once per note (@ref Change), so a reader
 * retries at most once a block and settles in the gap after it.
 *
 * **Lengths are the exception, deliberately.** A note still down grows every
 * block and the revision does not turn for it: its start cannot move while it
 * stands, so a length read a block later still belongs to that note. What a
 * reading does not promise is that two notes' lengths are from the same block.
 *
 * **The peaks are presentation and are not in the snapshot at all.** They place
 * nothing and drive nothing, and a wrap landing inside a read can redraw a few
 * ticks of the previous pass's audio for the frame it takes to ask again. This
 * is the approximation the contract admits to; everything above it is exact.
 *
 * ## What it costs the audio thread
 *
 * Bounded and allocation-free. A structural change is a handful of stores; a
 * block's extension is one store per note still down. The arrays are sized at
 * construction and never grow.
 *
 * One writer, the audio thread. Any number of readers, none of them consuming.
 */
class RecordTap {
    static_assert(std::atomic<double>::is_always_lock_free,
                  "a record tap is written on the audio thread and must not take a lock");

  public:
    /// A pass with no room for another note.
    static constexpr std::size_t kNoSlot = static_cast<std::size_t>(-1);

    /**
     * @brief Everything one block does to the pass, published as one change.
     *
     * A block's events are one transition, so a reader sees the pass before it
     * or after it and never inside it, and retries at most once a block rather
     * than once a note. Nesting is the ordinary case -- the calls below take
     * one each -- and only the outermost turns the revision.
     *
     * Audio thread, for the length of a block and no longer.
     */
    class Change {
      public:
        explicit Change(RecordTap& tap) : tap_(tap) {
            if (tap_.changing_++ == 0)
                tap_.revision_.fetch_add(kChanging, std::memory_order_release);
        }

        ~Change() {
            if (--tap_.changing_ == 0)
                tap_.revision_.fetch_add(kChanging, std::memory_order_release);
        }

        Change(const Change&) = delete;
        Change& operator=(const Change&) = delete;
        Change(Change&&) = delete;
        Change& operator=(Change&&) = delete;

      private:
        RecordTap& tap_;
    };

    RecordTap(RecordMaterial material, const RecordTapSettings& settings)
        : material_(material),
          settings_(settings),
          notes_(settings.maxNotes),
          peaks_(settings.maxPeaks) {}

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

      private:
        friend class RecordTap;

        /// Where a read is assembled. Swapped in only once it has been checked,
        /// so a read that does not settle leaves @ref notes as it found them.
        std::vector<RecordedNote> staging;
    };

    /**
     * @brief Read the pass into @p into. Off the audio thread.
     *
     * True when @p into is one moment's reading. False leaves it exactly as it
     * was, for a caller to keep drawing.
     *
     * Reuses @p into's storage, so a repaint reading every frame allocates
     * once.
     */
    bool read(Reading& into) const;

    int samplesPerPeak() const {
        return settings_.samplesPerPeak;
    }

    std::size_t maxNotes() const {
        return notes_.size();
    }

    /**
     * @brief A pass begins at @p startBeat on the timeline. Audio thread.
     *
     * Leaves the notes and peaks of the pass it replaces behind: what is drawn
     * is the pass in flight, and a loop wrap starts another one.
     */
    void open(double startBeat);

    /// @brief No pass in flight. What the last one reached stands, which the
    /// tap outliving the take is what makes possible: an overlay holds until
    /// the clip it became appears.
    void close() {
        recording_.store(false, std::memory_order_relaxed);
    }

    /// @brief The pass reaches @p lengthBeats. Not a change to what it holds,
    /// so a reader in the middle of one is not sent round again.
    void extend(double lengthBeats) {
        lengthBeats_.store(lengthBeats, std::memory_order_relaxed);
    }

    /**
     * @brief A note begins at @p beat from the pass start. Audio thread.
     *
     * The slot it took, for the caller to name it by afterwards, or @ref
     * kNoSlot when the pass is full.
     */
    std::size_t addNote(int noteNumber, int velocity, double beat);

    /// @brief @p slot is struck again: the same slot, a new note. What
    /// TakeNotes.hpp calls a replacement.
    void replaceNote(std::size_t slot, int noteNumber, int velocity, double beat);

    /// @brief The note in @p slot now runs to @p endBeat, held or released.
    void noteReaches(std::size_t slot, double endBeat);

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
    /// Odd while the pass or its notes are changing, even while they stand.
    static constexpr std::uint32_t kChanging = 1U;

    /// A reader retries over a handful of stores, and only while a block's
    /// events are being taken. Past this it says so rather than spinning.
    static constexpr int kReadAttempts = 16;

    struct NoteSlot {
        std::atomic<std::uint32_t> identity{0};
        std::atomic<double> startBeat{0.0};
        std::atomic<double> lengthBeats{0.0};
    };

    static std::uint32_t packIdentity(int note, int velocity) {
        return static_cast<std::uint32_t>(note & 0x7f) |
               (static_cast<std::uint32_t>(velocity & 0x7f) << 8U);
    }

    static std::uint64_t packPeak(float left, float right) {
        return (static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(left)) << 32U) |
               std::bit_cast<std::uint32_t>(right);
    }

    static RecordedPeak unpackPeak(std::uint64_t word) {
        return {std::bit_cast<float>(static_cast<std::uint32_t>(word >> 32U)),
                std::bit_cast<float>(static_cast<std::uint32_t>(word & 0xffffffffULL))};
    }

    /// One note into @p slot. Inside a @ref Change, which is what publishes it.
    void writeSlot(std::size_t slot, int noteNumber, int velocity, double beat);

    /// One tick's peak, folded into what the pass already put there. Assigned
    /// on the tick's first touch instead, which is what leaves the last pass's
    /// audio behind without clearing the array on every wrap.
    void writePeak(std::size_t slot, juce::dsp::AudioBlock<const float> audio, int offset,
                   int numSamples, bool first);

    RecordMaterial material_;
    RecordTapSettings settings_;

    std::vector<NoteSlot> notes_;
    std::vector<std::atomic<std::uint64_t>> peaks_;

    /// Changes open right now. Audio thread only, like everything a change
    /// touches.
    int changing_ = 0;

    std::atomic<std::uint32_t> revision_{0};
    std::atomic<std::uint32_t> pass_{0};
    std::atomic<bool> recording_{false};
    std::atomic<double> startBeat_{0.0};
    std::atomic<double> lengthBeats_{0.0};

    std::atomic<std::size_t> numNotes_{0};
    std::atomic<std::int64_t> notesLost_{0};

    /// Peaks the pass has reached, which is what it holds once capped.
    std::atomic<std::int64_t> peaksNeeded_{0};
};

}  // namespace magda::engine
