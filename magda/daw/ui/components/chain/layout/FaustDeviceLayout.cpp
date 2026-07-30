#include "layout/FaustDeviceLayout.hpp"

#include <algorithm>
#include <vector>

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

int maxPoolIndex(const magda::DeviceInfo& device) {
    int maxIdx = -1;
    for (const auto& p : device.parameters)
        maxIdx = std::max(maxIdx, p.paramIndex);
    return maxIdx;
}

struct GroupPage {
    juce::String name;
    std::vector<int> paramArrayIndices;
    int chunkIndex = 0;
    int chunkCount = 1;
};

std::vector<GroupPage> groupedPages(const magda::DeviceInfo& device) {
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
        if (it == groups.end()) {
            groups.push_back({name, {i}});
        } else {
            it->paramArrayIndices.push_back(i);
        }
    }

    if (groups.empty())
        groups.push_back({"Params", {}});

    std::vector<GroupPage> pages;
    for (const auto& group : groups) {
        const int count = static_cast<int>(group.paramArrayIndices.size());
        const int chunks = std::max(1, (count + FaustDeviceLayout::kCellCount - 1) /
                                           FaustDeviceLayout::kCellCount);
        for (int chunk = 0; chunk < chunks; ++chunk) {
            GroupPage page;
            page.name = group.name;
            page.chunkIndex = chunk;
            page.chunkCount = chunks;
            const int begin = chunk * FaustDeviceLayout::kCellCount;
            const int end = std::min(count, begin + FaustDeviceLayout::kCellCount);
            if (begin < end) {
                page.paramArrayIndices.insert(page.paramArrayIndices.end(),
                                              group.paramArrayIndices.begin() + begin,
                                              group.paramArrayIndices.begin() + end);
            }
            pages.push_back(std::move(page));
        }
    }
    return pages;
}

juce::String poolPageName(const magda::DeviceInfo& device, int pageIndex) {
    const int firstSlot = pageIndex * FaustDeviceLayout::kCellCount;
    const int lastSlot = firstSlot + FaustDeviceLayout::kCellCount;
    juce::String candidate;
    bool foundParam = false;

    for (const auto& param : device.parameters) {
        if (param.paramIndex < firstSlot || param.paramIndex >= lastSlot)
            continue;
        foundParam = true;
        if (param.group.isEmpty())
            return juce::String(pageIndex + 1);
        if (candidate.isEmpty())
            candidate = param.group;
        else if (candidate != param.group)
            return juce::String(pageIndex + 1);
    }

    if (!foundParam || candidate.isEmpty())
        return juce::String(pageIndex + 1);

    // The candidate may name this page only if none of its controls straddle
    // into another 32-slot block.
    for (const auto& param : device.parameters) {
        if (param.group == candidate &&
            (param.paramIndex < firstSlot || param.paramIndex >= lastSlot))
            return juce::String(pageIndex + 1);
    }
    return candidate;
}

}  // namespace

int FaustDeviceLayout::totalPages(const magda::DeviceInfo& device) const {
    if (pageMode_ == PageMode::Groups)
        return static_cast<int>(groupedPages(device).size());

    const int hi = maxPoolIndex(device);
    if (hi < 0)
        return 1;
    return std::max(1, (hi + 1 + kCellCount - 1) / kCellCount);
}

juce::String FaustDeviceLayout::pageName(const magda::DeviceInfo& device, int pageIndex) const {
    if (pageMode_ == PageMode::PoolSlots)
        return poolPageName(device, pageIndex);

    const auto pages = groupedPages(device);
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
    if (pageMode_ == PageMode::Groups) {
        ParamCell cell;
        const auto pages = groupedPages(device);
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

    const int targetPoolIdx = currentPage * kCellCount + cellIndex;
    const int paramArrayIdx = findParamArrayIndex(device, targetPoolIdx);

    ParamCell cell;
    if (paramArrayIdx < 0) {
        // No param at this pool slot — the cell is truly empty.
        cell.mode = ParamCell::Mode::Hidden;
        return cell;
    }

    const auto& param = device.parameters[static_cast<size_t>(paramArrayIdx)];
    cell.mode = ParamCell::Mode::Filled;
    cell.paramArrayIndex = paramArrayIdx;
    cell.targetParamIndex = param.paramIndex >= 0 ? param.paramIndex : paramArrayIdx;
    cell.enabled = gateEnabled(device, param);
    return cell;
}

}  // namespace magda::daw::ui
