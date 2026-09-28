#pragma once

#include <functional>
#include <vector>

namespace magda::daw::ui {

/**
 * @brief Singleton for runtime-adjustable debug settings
 */
class DebugSettings {
  public:
    static DebugSettings& getInstance() {
        static DebugSettings instance;
        return instance;
    }

    // Bottom panel height
    int getBottomPanelHeight() const {
        return bottomPanelHeight_;
    }
    void setBottomPanelHeight(int height) {
        bottomPanelHeight_ = height;
        notifyListeners();
    }

    // Device slot width
    int getDeviceSlotWidth() const {
        return deviceSlotWidth_;
    }
    void setDeviceSlotWidth(int width) {
        deviceSlotWidth_ = width;
        notifyListeners();
    }

    // Button font size
    float getButtonFontSize() const {
        return buttonFontSize_;
    }
    void setButtonFontSize(float size) {
        buttonFontSize_ = size;
        notifyListeners();
    }

    // Param label font size
    float getParamLabelFontSize() const {
        return paramLabelFontSize_;
    }
    void setParamLabelFontSize(float size) {
        paramLabelFontSize_ = size;
        notifyListeners();
    }

    // Param value font size
    float getParamValueFontSize() const {
        return paramValueFontSize_;
    }
    void setParamValueFontSize(float size) {
        paramValueFontSize_ = size;
        notifyListeners();
    }

    // Listener for settings changes
    using Listener = std::function<void()>;
    void addListener(const Listener& listener) {
        listeners_.push_back(listener);
    }

    void notifyListeners() {
        for (auto& listener : listeners_) {
            listener();
        }
    }

  private:
    DebugSettings() = default;

    int bottomPanelHeight_ = 315;
    int deviceSlotWidth_ = 235;
    float buttonFontSize_ = 10.0f;
    float paramLabelFontSize_ = 10.0f;
    float paramValueFontSize_ = 12.0f;

    std::vector<Listener> listeners_;
};

}  // namespace magda::daw::ui
