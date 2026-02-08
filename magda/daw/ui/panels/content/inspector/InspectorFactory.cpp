#include "InspectorFactory.hpp"

#include "ClipInspector.hpp"
#include "DeviceInspector.hpp"
#include "NoteInspector.hpp"
#include "TrackInspector.hpp"

namespace magda::daw::ui {

std::unique_ptr<BaseInspector> InspectorFactory::createInspector(magda::SelectionType type) {
    switch (type) {
        case magda::SelectionType::Track:
            return std::make_unique<TrackInspector>();

        case magda::SelectionType::Clip:
            return std::make_unique<ClipInspector>();

        case magda::SelectionType::Note:
            return std::make_unique<NoteInspector>();

        case magda::SelectionType::ChainNode:
            return std::make_unique<DeviceInspector>();

        case magda::SelectionType::None:
        default:
            return nullptr;
    }
}

}  // namespace magda::daw::ui
