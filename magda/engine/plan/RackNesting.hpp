#pragma once

#include <string>
#include <vector>

#include "core/TypeIds.hpp"

/**
 * @file RackNesting.hpp
 * @brief What "a rack contains itself" means, said once.
 *
 * A rack instance that contains itself, directly or through the instances
 * between, is a project the engine cannot compile. Every op under it would be
 * emitted a second time under the key it already has, and an OpKey is what the
 * differ hash-joins on: a duplicate does not fail, it carries one op's runtime
 * state into another. The parameter table has the same problem one layer up,
 * because a rack's macros and modifiers are addressed by the rack's id.
 *
 * So it is refused rather than depth-limited. A depth limit would turn a
 * modelling error into a rendering one: the plan would compile, and what it
 * rendered would be an arbitrary number of turns through a loop the project
 * does not mean.
 *
 * The rule lives here because more than one pass walks the same tree, and they
 * have to refuse the same instance. The plan compiler skips it; the helpers it
 * asks before emitting (what consumes MIDI, what a sidechain depends on, where
 * an instrument is) have to skip it too, or the compiler orders a track against
 * a dependency it never connects; and the parameter table has to skip it or
 * every macro under the loop reports itself as two things claiming one address.
 */

namespace magda::engine {

/**
 * @brief The rack instances open around the point a walk has reached.
 *
 * Outermost first, one entry per instance the walk descended through. A walk
 * that has not entered a rack holds none, which is the state every pass starts
 * a track in.
 */
class RackNesting {
  public:
    /// Whether @p rack is already open, which is what a rack instance that
    /// contains itself looks like from inside it.
    bool encloses(RackId rack) const;

    /**
     * @brief The cycle @p rack closes, as a diagnostic without its consequence.
     *
     * Reads `rack 8 contains itself: R4 > R8 > R4 > R8`, naming the whole path
     * from the outermost instance rather than only the repeat, because which
     * instance to remove is the question the report exists to answer and the
     * repeat alone does not say. The caller adds what its own pass did about
     * it, which differs: the compiler passes the signal through, the parameter
     * table leaves the scope's parameters out.
     */
    std::string cycle(RackId rack) const;

    /**
     * @brief Opens a rack instance for as long as this lives.
     *
     * A guard rather than a push and a pop, because every walk that descends
     * into a rack has somewhere it returns early from, and a stack that unwinds
     * by hand is a stack that eventually does not.
     */
    class Scope {
      public:
        Scope(RackNesting& nesting, RackId rack);
        ~Scope();

        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        Scope(Scope&&) = delete;
        Scope& operator=(Scope&&) = delete;

      private:
        RackNesting& nesting_;
    };

  private:
    std::vector<RackId> open_;
};

}  // namespace magda::engine
