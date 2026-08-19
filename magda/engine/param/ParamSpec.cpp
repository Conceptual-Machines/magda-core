#include "param/ParamSpec.hpp"

namespace magda::engine {

ParamSpec paramSpecFrom(const magda::ParameterInfo& info) {
    ParamSpec spec;
    spec.domain = magda::ParameterUtils::domainOf(info);
    spec.modulatable = info.modulatable;
    // segmentAccurate stays false: see the header. A device asks for it; a
    // ParameterInfo has no opinion about it and never grows one, because the
    // question is what the device does with the value rather than what the
    // value is.
    return spec;
}

}  // namespace magda::engine
