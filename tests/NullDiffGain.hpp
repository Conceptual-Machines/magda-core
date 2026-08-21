#pragma once

#include <juce_core/juce_core.h>

#include "core/DeviceInfo.hpp"
#include "core/TypeIds.hpp"

/**
 * @file NullDiffGain.hpp
 * @brief The one device both engines run, and the contract that makes its
 *        renders comparable (#2123).
 *
 * The corpus had no device with a parameter until this slice, and could not
 * have one: a Device op resolved to a stand-in that passed signal through, and
 * the incumbent instantiated nothing at all. So the whole of #1891 -- the value
 * lane, automation, the modifier engines, macros at each scope -- had never
 * been put in front of the fork. A parameter that nothing reads is a parameter
 * that cannot be compared.
 *
 * This is the smallest device that fixes that. One parameter, linear, zero to
 * one, applied as a gain. Nothing else, and deliberately nothing else: what a
 * gain device renders is the value of its own parameter, so a case that plays a
 * constant into it renders the parameter's curve directly and a value wrong in
 * the fourth decimal is visible. It is the same argument `fades.curves` makes
 * about a fade.
 *
 * ## Why a purpose-built device rather than a real one
 *
 * No MAGDA device runs under both engines today, and nothing in this slice
 * makes one. The engine has no device implementations (#1893 and the device SDK
 * in #1836 are where they arrive), and the incumbent's live in `PluginManager`
 * behind the sync layer the corpus refuses to duplicate. A device written for
 * the corpus, in both legs, from one contract, is what lets the parameter
 * system be compared before the devices exist. It is the same arrangement the
 * MIDI capture already uses: a te::Plugin registered in the app's own registry,
 * hidden from the browser, and its opposite number in the native leg.
 *
 * ## What both legs owe
 *
 * **The same law.** A gain of `v` multiplies by `v`. No smoothing, no ramp, no
 * dB anywhere: a smoother is state, state is memory, and two smoothers primed
 * differently never agree. The device is arithmetic so that a residual is the
 * parameter rather than the device.
 *
 * **The value at the sample being written, on the engine's side.** The engine
 * resolves a parameter into segments and the device reads the one covering each
 * sample, which is what keeps a render a function of timeline position rather
 * than of how the callback was cut up (#2078). The fork settles a parameter at
 * a block boundary and holds it for the block, because that is what
 * `AutomatableParameter` does.
 *
 * Those two differ, and the corpus does not paper over it: a case is built so
 * that the material is silent wherever the two could disagree, which is inside
 * one block of a step. That is the rule this corpus lives by -- choose the
 * material so that a residual can only be a bug, never the tolerance -- and it
 * is why every `param.*` case steps its curves on the half beat while its
 * impulses land on the beat.
 */

namespace magda::nulldiff {

/// The device's id in the app's internal registry. Registered by the incumbent
/// leg, hidden from the browser, and only ever inside a test binary.
inline constexpr const char* kGainPluginId = "nulldiffgain";

/// The one parameter it has. Index zero, because a device reads its own
/// parameters by the index it declared them at.
inline constexpr int kGainParamIndex = 0;

/// What the parameter is worth when a case says nothing: unity, so a device
/// nobody automated, modulated or linked is inaudible.
inline constexpr float kGainDefault = 1.0f;

/// The model parameter, as both legs read it.
///
/// Zero to one, linear, and the same range on the TE side (`teMinValue` /
/// `teMaxValue`), so the model value and the value the fork's parameter stores
/// are the same number and neither leg converts. A parameter whose two ranges
/// differed would put a conversion between the engines and the residual would
/// be measuring it.
inline magda::ParameterInfo gainParameter(float value = kGainDefault) {
    magda::ParameterInfo info;
    info.paramIndex = kGainParamIndex;
    info.stableId = "gain";
    info.name = "Gain";
    info.minValue = 0.0f;
    info.maxValue = 1.0f;
    info.defaultValue = kGainDefault;
    info.currentValue = value;
    info.teMinValue = 0.0f;
    info.teMaxValue = 1.0f;
    info.scale = magda::ParameterScale::Linear;
    return info;
}

/**
 * @brief The model device, with its parameter at @p value.
 *
 * `format` is Internal and says so: the block-size gate reads it to decide
 * whether a project has anything to attribute a difference to (#2078), and a
 * stand-in mislabelled as a VST3 would hand the corpus an allowance it has no
 * use for and no right to.
 */
inline magda::DeviceInfo gainDevice(magda::DeviceId id, float value = kGainDefault) {
    magda::DeviceInfo device;
    device.id = id;
    device.name = "Null Diff Gain";
    device.pluginId = kGainPluginId;
    device.deviceType = magda::DeviceType::Effect;
    device.isInstrument = false;
    device.canReceiveMidi = false;
    device.format = magda::PluginFormat::Internal;
    device.parameters.push_back(gainParameter(value));
    return device;
}

/// Whether @p device is one of these, asked the way both legs ask it.
inline bool isGainDevice(const magda::DeviceInfo& device) {
    return device.pluginId == kGainPluginId;
}

}  // namespace magda::nulldiff
