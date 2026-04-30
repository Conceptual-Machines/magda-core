#pragma once

#include "focused_api.hpp"

namespace magda {

class AudioBridge;

/**
 * Live FocusedApi - uses DefaultChainContext for focus tracking,
 * TrackManager for macro reads / writes, and AudioBridge for plugin
 * lookups (needed by autoMapToFirstParams to enumerate device params).
 */
class FocusedApiLive : public FocusedApi {
  public:
    /** Wire (or rewire) the AudioBridge. Pass nullptr to detach.
     *  Optional: with no bridge, autoMapToFirstParams becomes a no-op. */
    void setAudioBridge(AudioBridge* bridge) {
        bridge_ = bridge;
    }

    bool hasFocus() const override;
    juce::String getFocusedName() const override;
    juce::String getMacroName(int idx) const override;
    float getMacroValue(int idx) const override;
    void setMacroValue(int idx, float value) override;
    void autoMapToFirstParams() override;

  private:
    AudioBridge* bridge_ = nullptr;
};

}  // namespace magda
