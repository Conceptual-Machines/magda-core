#pragma once

#include <memory>

#include "BaseInspector.hpp"
#include "core/SelectionManager.hpp"

namespace magda::daw::ui {

/**
 * @brief Factory for creating specialized inspectors based on selection type
 *
 * Creates the appropriate inspector instance for:
 * - SelectionType::Track → TrackInspector
 * - SelectionType::Clip → ClipInspector
 * - SelectionType::Notes → NoteInspector
 * - SelectionType::ChainNode → DeviceInspector
 * - SelectionType::None → nullptr (no inspector needed)
 */
class InspectorFactory {
  public:
    /**
     * @brief Create an inspector for the given selection type
     * @param type The type of selection
     * @return Unique pointer to the appropriate inspector, or nullptr for SelectionType::None
     */
    static std::unique_ptr<BaseInspector> createInspector(magda::SelectionType type);
};

}  // namespace magda::daw::ui
