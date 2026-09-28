#pragma once

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "PanelContent.hpp"

namespace magda::daw::ui {

/**
 * @brief Factory for creating panel content instances
 *
 * Uses the registry pattern to allow content types to register themselves.
 * TabbedPanel uses this factory to create content instances lazily.
 */
class PanelContentFactory {
  public:
    using ContentCreator = std::function<std::unique_ptr<PanelContent>()>;

    /**
     * @brief Get the singleton instance
     */
    static PanelContentFactory& getInstance();

    // Delete copy/move operations for singleton
    PanelContentFactory(const PanelContentFactory&) = delete;
    PanelContentFactory& operator=(const PanelContentFactory&) = delete;
    PanelContentFactory(PanelContentFactory&&) = delete;
    PanelContentFactory& operator=(PanelContentFactory&&) = delete;

    /**
     * @brief Register a content type with its creator function
     */
    void registerContentType(PanelContentType type, ContentCreator creator);

    /**
     * @brief Create a content instance for the given type
     * @return New content instance, or nullptr if type not registered
     */
    std::unique_ptr<PanelContent> createContent(PanelContentType type);

    /**
     * @brief Check if a content type is registered
     */
    bool isRegistered(PanelContentType type) const;

    /**
     * @brief Get list of all registered content types
     */
    std::vector<PanelContentType> getAvailableTypes() const;

    /**
     * @brief Get the info for a content type
     */
    static PanelContentInfo getContentInfo(PanelContentType type);

  private:
    PanelContentFactory();
    ~PanelContentFactory() = default;

    void registerBuiltinTypes();

    std::unordered_map<PanelContentType, ContentCreator> creators_;
};

/**
 * @brief Helper template for self-registration of content types
 *
 * Usage in content .cpp file:
 *   static PanelContentRegistrar<MyContent> registrar(PanelContentType::MyType);
 */
template <typename T> class PanelContentRegistrar {
  public:
    explicit PanelContentRegistrar(PanelContentType type) {
        PanelContentFactory::getInstance().registerContentType(
            type, []() { return std::make_unique<T>(); });
    }
};

}  // namespace magda::daw::ui
