#include "DeviceParameterChangeHandler.hpp"

#include <algorithm>
#include <cmath>

#include "ParamHostComponent.hpp"
#include "ParamSlotComponent.hpp"
#include "audio/AudioBridge.hpp"
#include "audio/plugins/compiled/CompiledFaustInterface.hpp"
#include "core/DeviceInfo.hpp"
#include "core/TrackManager.hpp"
#include "engine/AudioEngine.hpp"

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

void refreshEngineAwareCompiledModeSlot(magda::DeviceInfo& device, magda::DeviceId deviceId,
                                        int changedParamIndex, ParamHostComponent& paramGrid) {
    int modeSlot = -1;

    if (auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine()) {
        if (auto* bridge = audioEngine->getAudioBridge()) {
            auto plugin = bridge->getPlugin(deviceId);
            if (auto* compiled =
                    dynamic_cast<daw::audio::compiled::ICompiledFaustPlugin*>(plugin.get())) {
                modeSlot = compiled->engineAwareModeSlot();
            }

            if (modeSlot >= 0 && changedParamIndex != modeSlot) {
                if (auto* proc = bridge->getDeviceProcessor(deviceId)) {
                    auto modeInfo = proc->getParameterInfo(modeSlot);

                    if (auto modeIt = findParameterInfo(device, modeSlot);
                        modeIt != device.parameters.end()) {
                        modeInfo.currentValue = modeIt->currentValue;
                        *modeIt = modeInfo;
                    }

                    if (auto* slot = paramGrid.getSlot(modeSlot))
                        slot->setParameterInfo(modeInfo);
                }
            }
        }
    }

    if (modeSlot < 0)
        return;

    const auto cell = paramGrid.getLayout().cellFor(device, modeSlot, paramGrid.getCurrentPage());
    if (auto* slot = paramGrid.getSlot(modeSlot))
        slot->setVisible(cell.mode == ParamCell::Mode::Filled);
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
    const int pageOffset = paramGrid.getCurrentPage() * paramsPerPage;
    const bool useVisibilityFilter = !device.visibleParameters.empty();

    for (int slotIndex = 0; slotIndex < paramsPerPage; ++slotIndex) {
        const int visibleParamIndex = pageOffset + slotIndex;

        int actualParamIndex = visibleParamIndex;
        if (useVisibilityFilter) {
            if (visibleParamIndex >= static_cast<int>(device.visibleParameters.size()))
                continue;
            actualParamIndex = device.visibleParameters[static_cast<size_t>(visibleParamIndex)];
        }

        if (actualParamIndex != paramIndex)
            continue;

        if (auto* slot = paramGrid.getSlot(slotIndex))
            slot->setParamValue(newValue);
        return;
    }
}

}  // namespace magda::daw::ui
