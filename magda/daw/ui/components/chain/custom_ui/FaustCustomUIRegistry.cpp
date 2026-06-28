#include "custom_ui/FaustCustomUIRegistry.hpp"

namespace magda::daw::ui {

FaustCustomUIRegistry& FaustCustomUIRegistry::getInstance() {
    // Meyers singleton: thread-safe under C++11+ for the initialization
    // itself; the map's mutation/read still happens on the main thread,
    // which is the contract documented in the header.
    static FaustCustomUIRegistry instance;
    return instance;
}

void FaustCustomUIRegistry::registerView(FaustCustomViewKind kind, Factory factory) {
    factories_[kind] = std::move(factory);
}

std::unique_ptr<FaustCustomView> FaustCustomUIRegistry::create(
    FaustCustomViewKind kind, magda::daw::audio::FaustPlugin& plugin) const {
    if (kind == FaustCustomViewKind::None)
        return nullptr;
    auto it = factories_.find(kind);
    if (it == factories_.end() || !it->second)
        return nullptr;
    return it->second(plugin);
}

}  // namespace magda::daw::ui
