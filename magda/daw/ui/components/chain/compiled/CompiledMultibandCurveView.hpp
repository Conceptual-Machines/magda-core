#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

#include "compiled/CompiledPluginPresentation.hpp"
#include "core/DeviceInfo.hpp"

namespace magda::daw::audio::compiled {
class MagdaMultibandCompiledPlugin;
}

namespace magda::daw::ui {

class CompiledMultibandCurveView final : public juce::Component,
                                         public CompiledDevicePanel,
                                         private juce::Timer {
  public:
    explicit CompiledMultibandCurveView(juce::String pluginId);

    int getPreferredHeight() const {
        return 140;
    }

    void setCompiledPlugin(magda::daw::audio::compiled::MagdaMultibandCompiledPlugin* plugin);
    void updateFromDevice(const magda::DeviceInfo& device) override;

    juce::Component& component() override {
        return *this;
    }
    void bindPlugin(te::Plugin* plugin) override;
    void setOnParameterChanged(std::function<void(int, float)> cb) override {
        onParameterChanged = std::move(cb);
    }
    void setOnLayoutChanged(std::function<void()> cb) override {
        onLayoutChanged_ = std::move(cb);
    }
    bool wantsFullBody() const override;
    int preferredHeight() const override {
        return getPreferredHeight();
    }

    std::function<void(int slotIndex, float displayValue)> onParameterChanged;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

  private:
    enum class Handle {
        None,
        LowXo,
        HighXo,
        LowLowerThreshold,
        LowUpperThreshold,
        LowBelowRatio,
        LowAboveRatio,
        LowAttack,
        LowRelease,
        LowLimit,
        MidLowerThreshold,
        MidUpperThreshold,
        MidBelowRatio,
        MidAboveRatio,
        MidAttack,
        MidRelease,
        MidLimit,
        HighLowerThreshold,
        HighUpperThreshold,
        HighBelowRatio,
        HighAboveRatio,
        HighAttack,
        HighRelease,
        HighLimit,
    };

#ifdef MAGDA_ENABLE_TEST_HOOKS
  public:
    enum class MagdaTestHandleKind {
        None,
        Crossover,
        LowerThreshold,
        UpperThreshold,
        BelowRatio,
        AboveRatio,
        Attack,
        Release,
        Limit,
    };

    struct MagdaTestPickedHandle {
        int band = -1;
        MagdaTestHandleKind kind = MagdaTestHandleKind::None;
    };

    MagdaTestPickedHandle magdaTestPickHandle(float x, float y) const {
        const auto handle = pickHandle(x, y);
        MagdaTestPickedHandle result{bandForHandle(handle), MagdaTestHandleKind::None};
        switch (handle) {
            case Handle::LowXo:
            case Handle::HighXo:
                result.kind = MagdaTestHandleKind::Crossover;
                break;
            case Handle::LowLowerThreshold:
            case Handle::MidLowerThreshold:
            case Handle::HighLowerThreshold:
                result.kind = MagdaTestHandleKind::LowerThreshold;
                break;
            case Handle::LowUpperThreshold:
            case Handle::MidUpperThreshold:
            case Handle::HighUpperThreshold:
                result.kind = MagdaTestHandleKind::UpperThreshold;
                break;
            case Handle::LowBelowRatio:
            case Handle::MidBelowRatio:
            case Handle::HighBelowRatio:
                result.kind = MagdaTestHandleKind::BelowRatio;
                break;
            case Handle::LowAboveRatio:
            case Handle::MidAboveRatio:
            case Handle::HighAboveRatio:
                result.kind = MagdaTestHandleKind::AboveRatio;
                break;
            case Handle::LowAttack:
            case Handle::MidAttack:
            case Handle::HighAttack:
                result.kind = MagdaTestHandleKind::Attack;
                break;
            case Handle::LowRelease:
            case Handle::MidRelease:
            case Handle::HighRelease:
                result.kind = MagdaTestHandleKind::Release;
                break;
            case Handle::LowLimit:
            case Handle::MidLimit:
            case Handle::HighLimit:
                result.kind = MagdaTestHandleKind::Limit;
                break;
            case Handle::None:
                break;
        }
        return result;
    }

    float magdaTestXForFrequency(float hz) const {
        return freqToX(hz);
    }
    float magdaTestYForDb(float db) const {
        return dbToY(db);
    }

  private:
#endif

    void timerCallback() override;
    void resampleFromPlugin();

    float xToFreq(float x) const;
    float freqToX(float hz) const;
    float dbToY(float db) const;
    float yToDb(float y) const;

    int bandAtX(float x) const;
    static int lowerThresholdSlotForBand(int band);
    static int upperThresholdSlotForBand(int band);
    static int belowRatioSlotForBand(int band);
    static int aboveRatioSlotForBand(int band);
    static int rangeSlotForBand(int band);
    static int limitSlotForBand(int band);
    static int attackSlotForBand(int band);
    static int releaseSlotForBand(int band);
    static int bandForHandle(Handle h);
    static bool isLimitHandle(Handle h);
    static bool isUpperThresholdHandle(Handle h);
    static bool isRatioHandle(Handle h);
    static bool isAboveRatioHandle(Handle h);
    static bool isTimingHandle(Handle h);
    static bool isReleaseTimingHandle(Handle h);
    int slotForHandle(Handle h) const;
    Handle pickHandle(float x, float y) const;

    magda::daw::audio::compiled::MagdaMultibandCompiledPlugin* compiledPlugin_ = nullptr;
    magda::DeviceInfo deviceSnapshot_;

    float lowXoHz_ = 120.0f;
    float highXoHz_ = 2500.0f;
    std::array<float, 3> attackMs_{{3.0f, 3.0f, 3.0f}};
    std::array<float, 3> releaseMs_{{120.0f, 120.0f, 120.0f}};
    std::array<float, 3> lowerThresholdDb_{{-48.0f, -48.0f, -48.0f}};
    std::array<float, 3> upperThresholdDb_{{-24.0f, -24.0f, -24.0f}};
    std::array<float, 3> belowRatio_{{8.0f, 8.0f, 8.0f}};
    std::array<float, 3> aboveRatio_{{8.0f, 8.0f, 8.0f}};
    std::array<float, 3> rangeDb_{{24.0f, 24.0f, 24.0f}};
    std::array<float, 3> limitDb_{{0.0f, 0.0f, 0.0f}};

    Handle hoveredHandle_ = Handle::None;
    Handle draggedHandle_ = Handle::None;
    float dragStartValue_ = 0.0f;
    juce::Rectangle<float> plotArea_;
    std::array<juce::Rectangle<float>, 3> attackAreas_{};
    std::array<juce::Rectangle<float>, 3> releaseAreas_{};
    std::array<juce::Rectangle<float>, 3> belowRatioAreas_{};
    std::array<juce::Rectangle<float>, 3> aboveRatioAreas_{};

    juce::Rectangle<float> collapseButtonArea_;
    bool collapseButtonHovered_ = false;
    int ratioScrollBand_ = -1;
    bool ratioScrollAbove_ = true;
    bool rangeScrollActive_ = false;

    std::function<void()> onLayoutChanged_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledMultibandCurveView)
};

}  // namespace magda::daw::ui
