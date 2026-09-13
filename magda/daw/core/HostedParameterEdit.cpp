#include "HostedParameterEdit.hpp"

namespace magda {

juce::String describeEditStatus(EditStatus status) {
    switch (status) {
        case EditStatus::Accepted:
            return "accepted";
        case EditStatus::InvalidValue:
            return "not a normalised position";
        case EditStatus::UnknownParameter:
            return "no such parameter on the plugin";
        case EditStatus::Unavailable:
            return "no plugin to deliver to";
        case EditStatus::Closing:
            return "the engine is closing";
    }

    return "unknown";
}

}  // namespace magda
