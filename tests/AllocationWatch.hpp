#pragma once

#include <cstddef>

/**
 * @file AllocationWatch.hpp
 * @brief Heap traffic on one thread, for the blocks that must not have any.
 *
 * The engine's claim is that nothing on the callback allocates, locks or frees
 * (exec/EngineSession.hpp), and a plan swap is where it is easiest to break:
 * the block after one renders through objects that were built a moment ago on
 * another thread. A case that only checked the audio would not notice.
 *
 * Counts `operator new` and `operator delete`, which is every C++ allocation in
 * the binary and not a raw `std::malloc` underneath one. So a zero is not proof
 * that nothing was allocated at all; it is proof that nothing the language
 * allocated was, which is what the code under it is written in.
 *
 * Per thread, so a background thread doing its own work while a case runs is
 * not counted against the block.
 */

namespace magda::test {

/**
 * @brief Counts allocations and frees on this thread while it stands.
 *
 * Nesting is allowed and each watch counts what happened inside its own scope.
 */
class AllocationWatch {
  public:
    AllocationWatch();
    ~AllocationWatch();

    AllocationWatch(const AllocationWatch&) = delete;
    AllocationWatch& operator=(const AllocationWatch&) = delete;
    AllocationWatch(AllocationWatch&&) = delete;
    AllocationWatch& operator=(AllocationWatch&&) = delete;

    std::size_t allocations() const;
    std::size_t frees() const;

    /// Both, which is what a case asserting on neither of them wants.
    std::size_t total() const {
        return allocations() + frees();
    }

  private:
    std::size_t allocationsAtStart_ = 0;
    std::size_t freesAtStart_ = 0;
};

/**
 * @brief Whether the counters are wired to this binary's operator new.
 *
 * Asked rather than assumed: a sanitizer runtime supplies its own, and a case
 * that asserted on a watch counting nothing would pass for the wrong reason.
 * Answered by allocating and seeing whether the count moved.
 */
bool allocationWatchWorks();

}  // namespace magda::test
