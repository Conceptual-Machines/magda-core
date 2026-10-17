#include <memory>

#include "extensions/Extension.hpp"

namespace magda::extensions::stub {

namespace {

/** @brief Proves the extension hooks link and run; it changes nothing the user sees. */
class StubExtension final : public Extension {
  public:
    const char* id() const override {
        return "stub";
    }
};

[[maybe_unused]] const bool stubExtensionRegistered = registerExtension(
    []() -> std::unique_ptr<Extension> { return std::make_unique<StubExtension>(); });

}  // namespace

}  // namespace magda::extensions::stub
