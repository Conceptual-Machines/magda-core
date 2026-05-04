#pragma once

#include "focused_api.hpp"

namespace magda {

/**
 * Live FocusedApi - uses DefaultChainContext for focus tracking and
 * TrackManager for macro reads / writes.
 */
class FocusedApiLive : public FocusedApi {
  public:
    bool hasFocus() const override;
    juce::String getFocusedName() const override;
    juce::String getMacroName(int idx) const override;
    float getMacroValue(int idx) const override;
    void setMacroValue(int idx, float value) override;
    void engageAutoMap() override;
    void clearAutoMap() override;
    void cycleDevice(int direction) override;
};

}  // namespace magda
