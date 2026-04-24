#include "UIScale.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <cstdlib>

#include "Config.hpp"

namespace magda {

namespace {

double clampScale(double s) {
    if (s < kMinUIScale)
        return kMinUIScale;
    if (s > kMaxUIScale)
        return kMaxUIScale;
    return s;
}

#if !JUCE_MAC
double autoScaleForDpi(double dpi) {
    if (dpi < 140.0)
        return 1.0;
    if (dpi <= 180.0)
        return 1.5;
    return 2.0;
}
#endif

}  // namespace

double dpiOnlyAutoScale() {
#if JUCE_MAC
    // macOS handles HiDPI via the backing store; setGlobalScaleFactor
    // stacks on top of that, doubling UI size on Retina displays.
    return 1.0;
#else
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        return autoScaleForDpi(d->dpi);
    return 1.0;
#endif
}

double resolveStartupScale() {
    if (const char* env = std::getenv("MAGDA_UI_SCALE")) {
        char* end = nullptr;
        double v = std::strtod(env, &end);
        if (end != env && v > 0.0)
            return clampScale(v);
    }
    double configured = Config::getInstance().getUIScale();
    if (configured > 0.0)
        return clampScale(configured);

    return dpiOnlyAutoScale();
}

void applyUIScale(double scale, bool persist) {
    scale = clampScale(scale);
    juce::Desktop::getInstance().setGlobalScaleFactor(static_cast<float>(scale));
    if (persist) {
        Config::getInstance().setUIScale(scale);
        Config::getInstance().save();
    }

    // Force every top-level window to re-lay-out and repaint. setGlobalScaleFactor
    // updates the peer's scale, but cached child layouts won't reflect it until
    // we kick a resize.
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i) {
        if (auto* c = desktop.getComponent(i)) {
            c->resized();
            c->repaint();
        }
    }
}

double stepUIScale(double current, int direction) {
    if (kUIScaleSteps.empty())
        return current;

    // Find index of the step nearest to `current`.
    size_t nearest = 0;
    double bestDiff = std::abs(current - kUIScaleSteps[0]);
    for (size_t i = 1; i < kUIScaleSteps.size(); ++i) {
        double d = std::abs(current - kUIScaleSteps[i]);
        if (d < bestDiff) {
            bestDiff = d;
            nearest = i;
        }
    }

    long next = static_cast<long>(nearest) + (direction > 0 ? 1 : -1);
    if (next < 0)
        next = 0;
    if (next >= static_cast<long>(kUIScaleSteps.size()))
        next = static_cast<long>(kUIScaleSteps.size()) - 1;
    return kUIScaleSteps[static_cast<size_t>(next)];
}

}  // namespace magda
