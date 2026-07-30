#include "layout/FaustDeviceLayout.hpp"

#include <algorithm>
#include <cstdint>

namespace magda::daw::ui {

namespace {

// Linear scan for the param whose pool slot index matches `poolIdx`.
// Faust devices typically have ≤32 active params per page, so a linear
// search is cheap; if that ever changes, this is the obvious place to
// swap in a lookup table.
int findParamArrayIndex(const magda::DeviceInfo& device, int poolIdx) {
    for (int k = 0; k < static_cast<int>(device.parameters.size()); ++k) {
        if (device.parameters[static_cast<size_t>(k)].paramIndex == poolIdx)
            return k;
    }
    return -1;
}

bool gateEnabled(const magda::DeviceInfo& device, const magda::ParameterInfo& param) {
    if (param.gateSlotIndex < 0)
        return true;
    // Faust gates target pool slot indices, not device.parameters[] positions —
    // resolve via the same lookup as cellFor.
    const int gateArrayIdx = findParamArrayIndex(device, param.gateSlotIndex);
    if (gateArrayIdx < 0)
        return true;  // gate target absent — leave control enabled
    const float gateValue = device.parameters[static_cast<size_t>(gateArrayIdx)].currentValue;
    const bool gateTruth = (gateValue >= 0.5f);
    return param.gateNegated ? !gateTruth : gateTruth;
}

std::uint64_t pageLayoutSignature(const magda::DeviceInfo& device) {
    constexpr std::uint64_t offset = 1469598103934665603ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t signature = offset;
    const auto mix = [&signature](std::uint64_t value) {
        signature ^= value;
        signature *= prime;
    };

    mix(device.parameters.size());
    for (const auto& param : device.parameters) {
        mix(static_cast<std::uint64_t>(param.paramIndex));
        mix(static_cast<std::uint64_t>(param.group.hashCode64()));
    }
    return signature;
}

}  // namespace

const std::vector<FaustDeviceLayout::GroupPage>& FaustDeviceLayout::pagesFor(
    const magda::DeviceInfo& device) const {
    const auto signature = pageLayoutSignature(device);
    if (cacheValid_ && signature == cachedSignature_)
        return cachedPages_;

    struct Group {
        juce::String name;
        std::vector<int> paramArrayIndices;
    };

    std::vector<Group> groups;
    for (int i = 0; i < static_cast<int>(device.parameters.size()); ++i) {
        const auto& param = device.parameters[static_cast<size_t>(i)];
        const auto name = param.group.isEmpty() ? juce::String("Params") : param.group;
        auto it = std::find_if(groups.begin(), groups.end(),
                               [&name](const Group& group) { return group.name == name; });
        if (it == groups.end())
            groups.push_back({name, {i}});
        else
            it->paramArrayIndices.push_back(i);
    }

    if (groups.empty())
        groups.push_back({"Params", {}});

    cachedPages_.clear();
    for (const auto& group : groups) {
        const int count = static_cast<int>(group.paramArrayIndices.size());
        const int chunks = std::max(1, (count + kCellCount - 1) / kCellCount);
        for (int chunk = 0; chunk < chunks; ++chunk) {
            GroupPage page;
            page.name = group.name;
            page.chunkIndex = chunk;
            page.chunkCount = chunks;
            const int begin = chunk * kCellCount;
            const int end = std::min(count, begin + kCellCount);
            if (begin < end) {
                page.paramArrayIndices.insert(page.paramArrayIndices.end(),
                                              group.paramArrayIndices.begin() + begin,
                                              group.paramArrayIndices.begin() + end);
            }
            cachedPages_.push_back(std::move(page));
        }
    }

    cachedSignature_ = signature;
    cacheValid_ = true;
    return cachedPages_;
}

int FaustDeviceLayout::totalPages(const magda::DeviceInfo& device) const {
    return static_cast<int>(pagesFor(device).size());
}

juce::String FaustDeviceLayout::pageName(const magda::DeviceInfo& device, int pageIndex) const {
    const auto& pages = pagesFor(device);
    if (pageIndex < 0 || pageIndex >= static_cast<int>(pages.size()))
        return juce::String(pageIndex + 1);
    const auto& page = pages[static_cast<size_t>(pageIndex)];
    if (page.chunkCount <= 1)
        return page.name;
    return page.name + " " + juce::String(page.chunkIndex + 1) + "/" +
           juce::String(page.chunkCount);
}

ParamCell FaustDeviceLayout::cellFor(const magda::DeviceInfo& device, int cellIndex,
                                     int currentPage) const {
    ParamCell cell;
    const auto& pages = pagesFor(device);
    if (currentPage < 0 || currentPage >= static_cast<int>(pages.size()))
        return cell;
    const auto& page = pages[static_cast<size_t>(currentPage)];
    if (cellIndex < 0 || cellIndex >= static_cast<int>(page.paramArrayIndices.size()))
        return cell;

    const int paramArrayIdx = page.paramArrayIndices[static_cast<size_t>(cellIndex)];
    const auto& param = device.parameters[static_cast<size_t>(paramArrayIdx)];
    cell.mode = ParamCell::Mode::Filled;
    cell.paramArrayIndex = paramArrayIdx;
    cell.targetParamIndex = param.paramIndex >= 0 ? param.paramIndex : paramArrayIdx;
    cell.enabled = gateEnabled(device, param);
    return cell;
}

int FaustDeviceLayout::pageForParameter(const magda::DeviceInfo& device, int paramIndex) const {
    const auto& pages = pagesFor(device);
    for (int pageIndex = 0; pageIndex < static_cast<int>(pages.size()); ++pageIndex) {
        for (const int paramArrayIdx : pages[static_cast<size_t>(pageIndex)].paramArrayIndices) {
            if (device.parameters[static_cast<size_t>(paramArrayIdx)].paramIndex == paramIndex)
                return pageIndex;
        }
    }
    return -1;
}

}  // namespace magda::daw::ui
