#include "slot/DeviceParameterChangeHandler.hpp"

#include <algorithm>
#include <cmath>

#include "audio/AudioBridge.hpp"
#include "audio/plugins/compiled/CompiledFaustInterface.hpp"
#include "core/DeviceInfo.hpp"
#include "core/TrackManager.hpp"
#include "engine/AudioEngine.hpp"
#include "params/ParamHostComponent.hpp"
#include "params/ParamSlotComponent.hpp"

namespace magda::daw::ui {

namespace {

std::vector<magda::ParameterInfo>::iterator findParameterInfo(magda::DeviceInfo& device,
                                                              int paramIndex) {
    auto it = std::find_if(
        device.parameters.begin(), device.parameters.end(),
        [paramIndex](const magda::ParameterInfo& param) { return param.paramIndex == paramIndex; });

    if (it == device.parameters.end() && paramIndex >= 0 &&
        paramIndex < static_cast<int>(device.parameters.size())) {
        it = device.parameters.begin() + paramIndex;
    }

    return it;
}

int visibleIndexForParameter(const magda::DeviceInfo& device, int paramIndex) {
    if (device.visibleParameters.empty())
        return paramIndex;

    for (int i = 0; i < static_cast<int>(device.visibleParameters.size()); ++i)
        if (device.visibleParameters[static_cast<size_t>(i)] == paramIndex)
            return i;

    return -1;
}

}  // namespace

void ParameterLearnHighlightState::reset() {
    lockedParamIndex = -1;
    lockTimeMs = 0;
    lastValueByParam.clear();
}

void updateCachedParameterValue(magda::DeviceInfo& device, int paramIndex, float newValue) {
    if (auto it = findParameterInfo(device, paramIndex); it != device.parameters.end())
        it->currentValue = newValue;
}

bool refreshEngineAwareCompiledSlots(magda::DeviceInfo& device, magda::DeviceId deviceId,
                                     int changedParamIndex, ParamHostComponent& paramGrid) {
    int modeSlot = -1;
    bool layoutNeedsRefresh = false;

    if (auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine()) {
        if (auto* bridge = audioEngine->getAudioBridge()) {
            auto plugin = bridge->getPlugin(deviceId);
            daw::audio::compiled::ICompiledFaustPlugin* compiled = nullptr;
            compiled = dynamic_cast<daw::audio::compiled::ICompiledFaustPlugin*>(plugin.get());
            if (compiled != nullptr)
                modeSlot = compiled->engineAwareModeSlot();

            if (compiled != nullptr) {
                if (auto* proc = bridge->getDeviceProcessor(deviceId)) {
                    for (int slotIndex = 0; slotIndex < compiled->hostSlotCount(); ++slotIndex) {
                        if (auto paramIt = findParameterInfo(device, slotIndex);
                            paramIt != device.parameters.end()) {
                            auto refreshedInfo = proc->getParameterInfo(slotIndex);
                            refreshedInfo.currentValue = paramIt->currentValue;

                            if (paramIt->hidden != refreshedInfo.hidden)
                                layoutNeedsRefresh = true;

                            const bool refreshMetadata =
                                slotIndex == modeSlot || paramIt->hidden != refreshedInfo.hidden;
                            if (refreshMetadata)
                                *paramIt = refreshedInfo;

                            if (!layoutNeedsRefresh && slotIndex == modeSlot &&
                                changedParamIndex != modeSlot &&
                                modeSlot < paramGrid.getSlotCount()) {
                                if (auto* slot = paramGrid.getSlot(modeSlot))
                                    slot->setParameterInfo(refreshedInfo);
                            }
                        }
                    }
                }
            }
        }
    }

    if (modeSlot < 0 || modeSlot >= paramGrid.getSlotCount())
        return layoutNeedsRefresh;

    const auto cell = paramGrid.getLayout().cellFor(device, modeSlot, paramGrid.getCurrentPage());
    if (auto* slot = paramGrid.getSlot(modeSlot))
        slot->setVisible(cell.mode == ParamCell::Mode::Filled);
    return layoutNeedsRefresh;
}

void applyLearnModeParameterHighlight(magda::DeviceInfo& device, ParamHostComponent& paramGrid,
                                      int paramIndex, float newValue,
                                      ParameterLearnHighlightState& state,
                                      const std::function<void()>& onPageChanged) {
    if (!paramGrid.isLearnMode())
        return;

    constexpr float kLearnDeltaThreshold = 0.0005f;
    constexpr juce::uint32 kLearnLockMs = 500;

    auto& lastValue = state.lastValueByParam[paramIndex];
    const float delta = std::abs(newValue - lastValue);
    lastValue = newValue;

    const auto nowMs = juce::Time::getMillisecondCounter();
    const bool lockExpired =
        state.lockedParamIndex == -1 || (nowMs - state.lockTimeMs) > kLearnLockMs;
    const bool isLockedParam = paramIndex == state.lockedParamIndex;

    if (delta <= kLearnDeltaThreshold || (!isLockedParam && !lockExpired))
        return;

    state.lockedParamIndex = paramIndex;
    state.lockTimeMs = nowMs;

    const int visibleIndex = visibleIndexForParameter(device, paramIndex);
    if (visibleIndex < 0)
        return;

    const int cellsPerPage = paramGrid.getSlotCount();
    if (cellsPerPage <= 0)
        return;

    const int targetPage = visibleIndex / cellsPerPage;
    if (targetPage != paramGrid.getCurrentPage()) {
        const int totalPages = juce::jmax(1, paramGrid.getLayout().totalPages(device));
        device.currentParameterPage = targetPage;
        paramGrid.updatePageControls(targetPage, totalPages);
        if (onPageChanged)
            onPageChanged();
    }

    paramGrid.highlightSlot(visibleIndex % cellsPerPage);
}

void updateCurrentPageParameterSlotValue(const magda::DeviceInfo& device,
                                         ParamHostComponent& paramGrid, int paramIndex,
                                         float newValue) {
    const int paramsPerPage = paramGrid.getSlotCount();
    const int currentPage = paramGrid.getCurrentPage();

    // The grid cell that displays `paramIndex` is the one whose layout-
    // reported `paramArrayIndex` matches. For row-major layouts that's just
    // the cell whose index equals paramIndex, but column-major (EQ) and any
    // other re-mapping layout need an explicit lookup — otherwise a single-
    // param notify writes into the wrong cell (e.g. dragging B4 Gain ends
    // up changing whatever cell sits at grid index 14, which under the EQ
    // column-major mapping is a different band entirely).
    const auto& layout = paramGrid.getLayout();
    const auto findIt = std::find_if(
        device.parameters.begin(), device.parameters.end(),
        [paramIndex](const magda::ParameterInfo& p) { return p.paramIndex == paramIndex; });
    const int paramArrayIndex =
        (findIt != device.parameters.end())
            ? static_cast<int>(std::distance(device.parameters.begin(), findIt))
            : paramIndex;

    for (int slotIndex = 0; slotIndex < paramsPerPage; ++slotIndex) {
        const auto cell = layout.cellFor(device, slotIndex, currentPage);
        if (cell.mode != ParamCell::Mode::Filled)
            continue;
        if (cell.paramArrayIndex != paramArrayIndex)
            continue;
        if (auto* slot = paramGrid.getSlot(slotIndex))
            slot->setParamValue(newValue);
        return;
    }
}

}  // namespace magda::daw::ui
