#include "DeviceInfo.hpp"

#include <atomic>

#include "RackInfo.hpp"

namespace magda {

std::uint64_t nextPluginAssignmentGeneration() {
    static std::atomic<std::uint64_t> generation{0};
    return generation.fetch_add(1, std::memory_order_relaxed) + 1;
}

// Out of line because RackInfo is only complete once RackInfo.hpp has been
// seen, and RackInfo.hpp includes DeviceInfo.hpp, so the header cannot see it.

PadRack::PadRack() = default;
PadRack::~PadRack() = default;
PadRack::PadRack(PadRack&& other) noexcept = default;
PadRack& PadRack::operator=(PadRack&& other) noexcept = default;

PadRack::PadRack(const PadRack& other)
    : rack_(other.rack_ != nullptr ? std::make_unique<RackInfo>(*other.rack_) : nullptr) {}

PadRack& PadRack::operator=(const PadRack& other) {
    if (this != &other)
        rack_ = other.rack_ != nullptr ? std::make_unique<RackInfo>(*other.rack_) : nullptr;
    return *this;
}

void PadRack::reset(std::unique_ptr<RackInfo> rack) {
    rack_ = std::move(rack);
}

}  // namespace magda
