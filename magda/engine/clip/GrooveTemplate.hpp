#pragma once

#include <juce_core/juce_core.h>

#include <string>
#include <vector>

/**
 * @file GrooveTemplate.hpp
 * @brief What a groove template does to a beat, and where the templates live.
 *
 * A native port of the one thing MAGDA still takes from Tracktion here: a
 * lateness table, a notes-per-beat grid and one formula. Small enough to
 * reimplement exactly rather than approximately, which is the same call slice 5
 * made for the transient detector.
 *
 * Compiled the way the warp map is (WarpMap.hpp): the clip's strength is folded
 * into the table when the snapshot is compiled, because whether strength applies
 * at all is the template's own `parameterized` flag and that is a fact about the
 * template rather than about the block. What reaches the audio thread is a table
 * that already means what it says.
 *
 * The lookup runs ON the audio thread, and that is the one part of a MIDI clip
 * which cannot be resolved ahead of the block. Groove is anchored to the project
 * grid rather than to the clip, so a looped clip whose loop length is not a
 * whole multiple of the template's period grooves each pass differently. The
 * fork delivers that by re-timing its whole sequence once per pass and grooving
 * it there (LoopedMidiEventGenerator::setLoopIndex), which is the behaviour, not
 * an artefact of how it unrolls. Baking before the fold would be a divergence on
 * every odd-length loop.
 *
 * So the lookup allocates nothing, touches no string and is two array reads and
 * a lerp.
 */

namespace magda::engine {

/**
 * @brief A groove, compiled against one clip's strength.
 *
 * Empty is the identity and is what a clip with no groove, a template that does
 * not exist, and a template whose latenesses are all zero all get. Every caller
 * is therefore groove-agnostic, exactly as an empty WarpMap makes them
 * warp-agnostic.
 */
class GrooveTemplate {
  public:
    /// The fork's clamps, kept because they are what a stored template was
    /// written under: 2 to 1024 notes, 1 to 8 notes per beat, latenesses in
    /// [-1, 1].
    static constexpr int kMinNotes = 2;
    static constexpr int kMaxNotes = 1024;
    static constexpr int kMinNotesPerBeat = 1;
    static constexpr int kMaxNotesPerBeat = 8;

    GrooveTemplate() = default;

    /**
     * @brief Compile @p latenesses into a table that already carries @p
     *        strength.
     *
     * @p numNotes is the pattern length in grid steps and may exceed the
     * latenesses given, in which case the rest are zero: the fork reads its
     * table through a juce::Array, which answers zero past the end, and a clip
     * saved that way has to play the same.
     */
    static GrooveTemplate compile(const std::vector<float>& latenesses, int numNotes,
                                  int notesPerBeat, bool parameterized, float strength);

    bool empty() const {
        return latenesses_.empty();
    }

    /// Where @p beat moves to, in the domain it was given in. The identity when
    /// empty.
    double groovyBeat(double beat) const;

    /**
     * @brief The furthest this table can move any beat, in beats.
     *
     * Exact rather than a guess: the displacement is half a lerp between two
     * latenesses divided by the notes-per-beat, so its bound is half the largest
     * lateness over the same. What asks is the block's event search, which has
     * to widen by this much or lose events the groove moved into the block.
     */
    double maxDisplacementBeats() const {
        return maxDisplacement_;
    }

  private:
    /// Strength already folded in, and resized to the pattern length so a
    /// lookup cannot fall off the end.
    std::vector<float> latenesses_;
    int notesPerBeat_ = 2;
    double maxDisplacement_ = 0.0;
};

/**
 * @brief The named templates, as whoever owns them holds them.
 *
 * Handed to the snapshot compiler the way the source table and the tempo map
 * are, rather than looked up behind the caller's back: the engine reaches no
 * singleton, and a compile is a pure function of what it was given.
 *
 * An empty set is legal and means no clip grooves, which is what the engine gets
 * until the app is switched over to it.
 */
class GrooveTemplateSet {
  public:
    struct Entry {
        std::string name;
        std::vector<float> latenesses;
        int numNotes = 16;
        int notesPerBeat = 2;
        bool parameterized = false;
    };

    /// Parse a <GROOVETEMPLATES> document, which is the shape the fork keeps
    /// these in and therefore the shape any settings file already holds.
    static GrooveTemplateSet parse(const juce::XmlElement& document);

    void add(Entry entry);

    /// Compiled against @p strength, or the identity when @p name names
    /// nothing. Not finding a template is an ordinary answer rather than a
    /// failure: a project can name a groove this installation does not have.
    GrooveTemplate compile(const std::string& name, float strength) const;

    bool contains(const std::string& name) const;

    std::size_t size() const {
        return entries_.size();
    }

  private:
    std::vector<Entry> entries_;
};

}  // namespace magda::engine
