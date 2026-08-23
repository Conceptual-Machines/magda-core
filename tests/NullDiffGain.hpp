#pragma once

#include <juce_core/juce_core.h>

#include "core/DeviceInfo.hpp"
#include "core/TypeIds.hpp"

/**
 * @file NullDiffGain.hpp
 * @brief The one device both engines run, and the contract that makes its
 *        renders comparable (#2123).
 *
 * The corpus had no device with a parameter until this slice: a Device op
 * resolved to a stand-in and the incumbent instantiated nothing, so the whole
 * of #1891 had never been put in front of the fork. A parameter nothing reads
 * is a parameter nothing can compare.
 *
 * This is the smallest device that fixes that. One parameter, linear, zero to
 * one, applied as a gain, so what it renders is the value of its own parameter
 * and a case that plays a constant into it draws the curve directly. No MAGDA
 * device runs under both engines yet (#1893, #1836), which is why it is written
 * for the corpus, in both legs, from one contract -- the arrangement the MIDI
 * capture already uses.
 *
 * Both legs owe the same law: a gain of `v` multiplies by `v`, with no
 * smoothing, because a smoother is state and two smoothers primed differently
 * never agree.
 *
 * They differ in when they read it. The engine reads the value at the sample
 * being written, which is what keeps a render a function of timeline position
 * (#2078); the fork settles a parameter at a block boundary and holds it. The
 * corpus does not paper over that: every `param.*` case steps its curves on the
 * half beat and lands its impulses on the beat, so the material is silent
 * wherever the two could disagree.
 */

namespace magda::nulldiff {

/// The device's id in the app's internal registry. Registered by the incumbent
/// leg, hidden from the browser, and only ever inside a test binary.
inline constexpr const char* kGainPluginId = "nulldiffgain";

/// The same device, one channel wide (#2139).
///
/// A separate registration rather than a flag on the one above, because what
/// makes a device mono is what its plugin reports when the graph asks how many
/// channels it has, and that is a property of the type in both engines: the
/// rack matrix reads it off the plugin at build time, and the plan reads it off
/// the model. Two ids keep the two answers to one question.
///
/// The DSP is identical, which is the point: what a mono case measures is the
/// bus narrowing and widening around the device, and a device that also sounded
/// different would put a second variable in it.
inline constexpr const char* kMonoGainPluginId = "nulldiffmonogain";

/// The one parameter it has. Index zero, because a device reads its own
/// parameters by the index it declared them at.
inline constexpr int kGainParamIndex = 0;

/// Unity, so a device nobody automated, modulated or linked is inaudible.
inline constexpr float kGainDefault = 1.0f;

/// The model parameter, as both legs read it.
///
/// The same range on both sides, so the model value and the value the fork's
/// parameter stores are the same number and neither leg converts. Two ranges
/// would put a conversion between the engines and the residual would measure
/// it.
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
 * whether a project has anything to attribute a difference to (#2078).
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

/**
 * @brief The same device at one channel in and one channel out.
 *
 * The widths are on the model because that is where the plan reads them
 * (PlanCompiler clamps `audioInputChannels` and `audioOutputChannels` to the
 * bus), and the incumbent reads its own from the plugin. Both have to say the
 * same thing or the case is measuring the disagreement between the two
 * declarations rather than what the engines do with a narrow device.
 */
inline magda::DeviceInfo monoGainDevice(magda::DeviceId id, float value = kGainDefault) {
    auto device = gainDevice(id, value);
    device.name = "Null Diff Mono Gain";
    device.pluginId = kMonoGainPluginId;
    device.audioInputChannels = 1;
    device.audioOutputChannels = 1;
    return device;
}

/// Whether @p device is one of these, asked the way both legs ask it. Either
/// width: they run the same gain, and what differs is the bus around it.
inline bool isGainDevice(const magda::DeviceInfo& device) {
    return device.pluginId == kGainPluginId || device.pluginId == kMonoGainPluginId;
}

/// Whether it is the narrow one.
inline bool isMonoGainDevice(const magda::DeviceInfo& device) {
    return device.pluginId == kMonoGainPluginId;
}

}  // namespace magda::nulldiff
