#pragma once

#include <string>
#include <vector>

#include "core/TypeIds.hpp"

/**
 * @file RackNesting.hpp
 * @brief What "a rack contains itself" means, said once.
 *
 * Every op under such an instance would be emitted again under the key it
 * already has, and the differ hash-joins on that key: a duplicate does not
 * fail, it carries one op's state into another. So it is refused rather than
 * depth-limited, which would turn a modelling error into a rendering one.
 *
 * Stated here because several passes walk the same tree and all of them have to
 * refuse the same instance: the plan compiler, the helpers it asks before
 * emitting, and the parameter table, which addresses a rack's macros by id.
 */

namespace magda::engine {

/**
 * @brief The rack instances open around the point a walk has reached,
 *        outermost first. Every pass starts a track holding none.
 */
class RackNesting {
  public:
    /// Whether @p rack is already open, which is what an instance containing
    /// itself looks like from inside it.
    bool encloses(RackId rack) const;

    /**
     * @brief The cycle @p rack closes: `rack 8 contains itself: R4 > R8 > R4 > R8`.
     *
     * The whole path rather than the repeat alone, because which instance to
     * remove is the question the report exists to answer. The caller appends
     * what its own pass did about it, which differs.
     */
    std::string cycle(RackId rack) const;

    /// Opens a rack instance for as long as this lives. A guard rather than a
    /// push and a pop, because every walk that descends has somewhere it
    /// returns early from.
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
