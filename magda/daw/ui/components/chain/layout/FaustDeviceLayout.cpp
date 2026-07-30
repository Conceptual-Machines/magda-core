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
        // Width feeds the packing, so a change to it has to invalidate too.
        mix(static_cast<std::uint64_t>(param.widthCells));
    }
    mix(device.meters.size());
    for (const auto& meter : device.meters) {
        mix(static_cast<std::uint64_t>(meter.meterIndex));
        mix(static_cast<std::uint64_t>(meter.group.hashCode64()));
        mix(static_cast<std::uint64_t>(meter.widthCells));
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
        std::vector<int> meterArrayIndices;
    };

    std::vector<Group> groups;
    const auto groupFor = [&groups](const juce::String& rawName) -> Group& {
        const auto name = rawName.isEmpty() ? juce::String("Params") : rawName;
        auto it = std::find_if(groups.begin(), groups.end(),
                               [&name](const Group& group) { return group.name == name; });
        if (it != groups.end())
            return *it;
        groups.push_back({name, {}, {}});
        return groups.back();
    };

    for (int i = 0; i < static_cast<int>(device.parameters.size()); ++i)
        groupFor(device.parameters[static_cast<size_t>(i)].group).paramArrayIndices.push_back(i);

    // Meters join the page their author group names, after that group's
    // controls. A bargraph declared in a group MAGDA has not seen before opens
    // its own page, the same as a control would.
    for (int i = 0; i < static_cast<int>(device.meters.size()); ++i)
        groupFor(device.meters[static_cast<size_t>(i)].group).meterArrayIndices.push_back(i);

    if (groups.empty())
        groups.push_back({"Params", {}, {}});

    cachedPages_.clear();
    for (const auto& group : groups) {
        // Pack left to right, wrapping a control to the next row rather than
        // splitting it, and starting a new page when a row would overflow the
        // grid. A width wider than a row is clamped: an author asking for more
        // than the grid can give gets the whole row, not a broken layout.
        GroupPage page;
        page.name = group.name;
        page.cells.assign(kCellCount, PageCell{});
        int cursor = 0;

        const auto flush = [&]() {
            cachedPages_.push_back(page);
            page.cells.assign(kCellCount, PageCell{});
            cursor = 0;
        };

        const auto place = [&](PageCell cell, int requestedWidth) {
            const int span = std::clamp(requestedWidth, 1, kCellsPerRow);

            // Never straddle a row boundary: jump to the next row instead.
            if ((cursor % kCellsPerRow) + span > kCellsPerRow)
                cursor += kCellsPerRow - (cursor % kCellsPerRow);

            if (cursor + span > kCellCount) {
                flush();
            }

            cell.span = span;
            page.cells[static_cast<size_t>(cursor)] = cell;
            cursor += span;
        };

        for (const int paramArrayIdx : group.paramArrayIndices) {
            const auto& param = device.parameters[static_cast<size_t>(paramArrayIdx)];
            place({paramArrayIdx, -1, 1}, param.widthCells);
        }

        for (const int meterArrayIdx : group.meterArrayIndices) {
            const auto& meter = device.meters[static_cast<size_t>(meterArrayIdx)];
            place({-1, meterArrayIdx, 1}, meter.widthCells);
        }
        cachedPages_.push_back(std::move(page));
    }

    // Chunk numbering is only meaningful once a group needed more than one
    // page, so it is applied after packing decided how many that was.
    for (size_t i = 0; i < cachedPages_.size();) {
        size_t j = i;
        while (j < cachedPages_.size() && cachedPages_[j].name == cachedPages_[i].name)
            ++j;
        const int chunks = static_cast<int>(j - i);
        for (size_t k = i; k < j; ++k) {
            cachedPages_[k].chunkIndex = static_cast<int>(k - i);
            cachedPages_[k].chunkCount = chunks;
        }
        i = j;
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
    if (cellIndex < 0 || cellIndex >= static_cast<int>(page.cells.size()))
        return cell;

    const auto& pageCell = page.cells[static_cast<size_t>(cellIndex)];

    if (pageCell.meterArrayIndex >= 0) {
        cell.mode = ParamCell::Mode::Meter;
        cell.meterArrayIndex = pageCell.meterArrayIndex;
        cell.span = pageCell.span;
        return cell;
    }

    // -1 means empty, or absorbed by a wider control starting to the left.
    if (pageCell.paramArrayIndex < 0)
        return cell;

    const auto& param = device.parameters[static_cast<size_t>(pageCell.paramArrayIndex)];
    cell.mode = ParamCell::Mode::Filled;
    cell.paramArrayIndex = pageCell.paramArrayIndex;
    cell.targetParamIndex = param.paramIndex >= 0 ? param.paramIndex : pageCell.paramArrayIndex;
    cell.enabled = gateEnabled(device, param);
    cell.span = pageCell.span;
    return cell;
}

int FaustDeviceLayout::pageForParameter(const magda::DeviceInfo& device, int paramIndex) const {
    const auto& pages = pagesFor(device);
    for (int pageIndex = 0; pageIndex < static_cast<int>(pages.size()); ++pageIndex) {
        for (const auto& pageCell : pages[static_cast<size_t>(pageIndex)].cells) {
            if (pageCell.paramArrayIndex < 0)
                continue;
            if (device.parameters[static_cast<size_t>(pageCell.paramArrayIndex)].paramIndex ==
                paramIndex)
                return pageIndex;
        }
    }
    return -1;
}

}  // namespace magda::daw::ui
