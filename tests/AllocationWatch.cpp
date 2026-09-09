#include "AllocationWatch.hpp"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <new>

/**
 * @file AllocationWatch.cpp
 * @brief The replacement operators behind AllocationWatch.hpp.
 *
 * Global, so every C++ allocation in the test binary is counted, including the
 * ones inside the engine. Everything forwards to malloc and free, which is what
 * the default operators do, so no other test behaves differently for this being
 * linked in.
 *
 * The counters are `thread_local` scalars and are therefore constant
 * initialised: reaching one cannot allocate, which it would have to not do
 * anyway, since it is reached from inside the allocator.
 */

namespace {

thread_local std::size_t g_allocations = 0;
thread_local std::size_t g_frees = 0;

void* rawAllocate(std::size_t size) {
    // Zero is a legal request and must still hand back something a matching
    // free can take.
    return std::malloc(size == 0 ? 1 : size);
}

void* rawAllocateAligned(std::size_t size, std::size_t alignment) {
    if (size == 0)
        size = 1;

#if defined(_WIN32)
    return _aligned_malloc(size, alignment);
#else
    // posix_memalign refuses an alignment below a pointer's, which an
    // over-aligned type smaller than one can still ask for.
    void* memory = nullptr;
    if (posix_memalign(&memory, std::max(alignment, sizeof(void*)), size) != 0)
        return nullptr;
    return memory;
#endif
}

void rawFreeAligned(void* memory) {
#if defined(_WIN32)
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}

void* allocateOrThrow(std::size_t size) {
    if (auto* memory = rawAllocate(size))
        return memory;
    throw std::bad_alloc();
}

void* allocateAlignedOrThrow(std::size_t size, std::size_t alignment) {
    if (auto* memory = rawAllocateAligned(size, alignment))
        return memory;
    throw std::bad_alloc();
}

}  // namespace

// The standard's own declarations name these parameters differently, and
// matching a standard library's internal spelling is not worth the trade.
// NOLINTBEGIN(readability-inconsistent-declaration-parameter-name)

void* operator new(std::size_t size) {
    ++g_allocations;
    return allocateOrThrow(size);
}

void* operator new[](std::size_t size) {
    ++g_allocations;
    return allocateOrThrow(size);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    ++g_allocations;
    return allocateAlignedOrThrow(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    ++g_allocations;
    return allocateAlignedOrThrow(size, static_cast<std::size_t>(alignment));
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    ++g_allocations;
    return rawAllocate(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    ++g_allocations;
    return rawAllocate(size);
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    ++g_allocations;
    return rawAllocateAligned(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    ++g_allocations;
    return rawAllocateAligned(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* memory) noexcept {
    ++g_frees;
    std::free(memory);
}

void operator delete[](void* memory) noexcept {
    ++g_frees;
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    ++g_frees;
    std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept {
    ++g_frees;
    std::free(memory);
}

void operator delete(void* memory, const std::nothrow_t&) noexcept {
    ++g_frees;
    std::free(memory);
}

void operator delete[](void* memory, const std::nothrow_t&) noexcept {
    ++g_frees;
    std::free(memory);
}

void operator delete(void* memory, std::align_val_t) noexcept {
    ++g_frees;
    rawFreeAligned(memory);
}

void operator delete[](void* memory, std::align_val_t) noexcept {
    ++g_frees;
    rawFreeAligned(memory);
}

void operator delete(void* memory, std::size_t, std::align_val_t) noexcept {
    ++g_frees;
    rawFreeAligned(memory);
}

void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept {
    ++g_frees;
    rawFreeAligned(memory);
}

void operator delete(void* memory, std::align_val_t, const std::nothrow_t&) noexcept {
    ++g_frees;
    rawFreeAligned(memory);
}

void operator delete[](void* memory, std::align_val_t, const std::nothrow_t&) noexcept {
    ++g_frees;
    rawFreeAligned(memory);
}

// NOLINTEND(readability-inconsistent-declaration-parameter-name)

namespace magda::test {

AllocationWatch::AllocationWatch() : allocationsAtStart_(g_allocations), freesAtStart_(g_frees) {}

AllocationWatch::~AllocationWatch() = default;

std::size_t AllocationWatch::allocations() const {
    return g_allocations - allocationsAtStart_;
}

std::size_t AllocationWatch::frees() const {
    return g_frees - freesAtStart_;
}

bool allocationWatchWorks() {
    const AllocationWatch watch;

    // Sized past any small-buffer optimisation and used afterwards, so the
    // allocation cannot be optimised out.
    std::unique_ptr<char[]> probe(new char[4096]);
    probe[0] = 1;

    return watch.allocations() > 0;
}

}  // namespace magda::test
