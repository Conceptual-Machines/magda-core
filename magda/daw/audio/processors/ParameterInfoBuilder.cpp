#include "processors/ParameterInfoBuilder.hpp"

namespace magda {

ParameterInfo makeInfoFromTeParam(int index, te::AutomatableParameter* param) {
    ParameterInfo info;
    info.paramIndex = index;
    if (!param)
        return info;

    info.name = param->getParameterName();

    auto range = param->getValueRange();
    info.minValue = range.getStart();
    info.maxValue = range.getEnd();
    info.teMinValue = range.getStart();
    info.teMaxValue = range.getEnd();
    info.defaultValue = param->getDefaultValue().value_or(range.getStart());
    info.currentValue = param->getCurrentValue();

    // Only adopt the plugin's unit label when the range is a real range
    // (not normalized 0..1). External plugins (VST3/AU) often report
    // labels like "Hz" or "dB" even though their parameter operates in
    // normalized space. Using that label with a 0..1 range causes the
    // Hz/dB formatter to produce garbage (e.g. 0 * pow(1/0, x)).
    bool isNormalizedRange = (info.minValue >= -0.01f && info.minValue <= 0.01f &&
                              info.maxValue >= 0.99f && info.maxValue <= 1.01f);
    if (!isNormalizedRange)
        info.unit = param->getLabel();

    // Infer scale from TE's state count: a small state count indicates a
    // discrete/enum parameter (e.g. filter slope), everything else is
    // treated as linear. TE doesn't expose log/exp hints, so specialised
    // scales must be set by the caller when known.
    int numStates = param->getNumberOfStates();
    if (numStates == 2) {
        info.scale = ParameterScale::Boolean;
        info.modulatable = false;
    } else if (numStates > 0 && numStates <= 12) {
        info.scale = ParameterScale::Discrete;
    } else {
        info.scale = ParameterScale::Linear;
    }

    return info;
}

}  // namespace magda
