#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <type_traits>

namespace magda {

/**
 * @brief RAII wrapper for JUCE Drawables - ensures safe cleanup like Python's context manager
 *
 * Problem: JUCE Drawables have internal component hierarchies that must be cleaned up
 * in the correct order to avoid memory corruption.
 *
 * Solution: This wrapper automatically calls deleteAllChildren() before destruction,
 * preventing listener list corruption.
 *
 * Usage:
 * @code
 * class MyComponent {
 *     ManagedDrawable icon_;
 *
 * public:
 *     MyComponent() {
 *         icon_ = ManagedDrawable::create(svgData, svgSize);
 *         // Use icon_ like a regular unique_ptr
 *         if (icon_) {
 *             icon_->drawWithin(...);
 *         }
 *     }
 *     // Destructor automatically handles cleanup safely
 * };
 * @endcode
 */
class ManagedDrawable {
  public:
    ManagedDrawable() = default;

    // Factory method - creates a Drawable and wraps it
    static ManagedDrawable create(const char* svgData, size_t svgSize) {
        ManagedDrawable managed;
        if (svgData && svgSize > 0) {
            auto svgString = juce::String::fromUTF8(svgData, static_cast<int>(svgSize));
            auto svgXml = juce::XmlDocument::parse(svgString);
            if (svgXml) {
                managed.drawable_ = juce::Drawable::createFromSVG(*svgXml);
            }
        }
        return managed;
    }

    // Factory method - wraps an existing Drawable
    static ManagedDrawable wrap(std::unique_ptr<juce::Drawable> drawable) {
        ManagedDrawable managed;
        managed.drawable_ = std::move(drawable);
        return managed;
    }

    // Destructor - RAII cleanup (like Python's __exit__). Let unique_ptr handle
    // destruction naturally; no manual deleteAllChildren needed - Component's
    // destructor handles it correctly.
    ~ManagedDrawable() = default;

    // Move-only (like Python context managers)
    ManagedDrawable(ManagedDrawable&&) noexcept = default;
    ManagedDrawable& operator=(ManagedDrawable&&) noexcept = default;
    ManagedDrawable(const ManagedDrawable&) = delete;
    ManagedDrawable& operator=(const ManagedDrawable&) = delete;

    // Smart pointer interface
    juce::Drawable* get() const {
        return drawable_.get();
    }
    juce::Drawable* operator->() const {
        return drawable_.get();
    }
    juce::Drawable& operator*() const {
        return *drawable_;
    }
    explicit operator bool() const {
        return drawable_ != nullptr;
    }

    // Release ownership (use with caution)
    std::unique_ptr<juce::Drawable> release() {
        return std::move(drawable_);
    }

  private:
    std::unique_ptr<juce::Drawable> drawable_;
};

/**
 * @brief RAII wrapper for child components - prevents double-delete
 *
 * Problem: When you have both unique_ptr ownership AND addAndMakeVisible(),
 * the component can be deleted twice (once by unique_ptr, once by parent).
 *
 * Solution: This wrapper automatically removes the component from its parent
 * before unique_ptr destruction.
 *
 * Usage:
 * @code
 * class MyPanel : public juce::Component {
 *     ManagedChild<SvgButton> button_;
 *
 * public:
 *     MyPanel() {
 *         button_ = ManagedChild<SvgButton>::create("MyButton", svgData, svgSize);
 *         addAndMakeVisible(*button_);  // Safe to add as child
 *     }
 *     // Destructor automatically removes from parent before deletion
 * };
 * @endcode
 */
template <typename ComponentType> class ManagedChild {
    static_assert(std::is_base_of<juce::Component, ComponentType>::value,
                  "ManagedChild only works with JUCE Components");

  public:
    ManagedChild() = default;

    // Factory method - creates a component
    template <typename... Args> static ManagedChild create(Args&&... args) {
        ManagedChild managed;
        managed.component_ = std::make_unique<ComponentType>(std::forward<Args>(args)...);
        return managed;
    }

    // Factory method - wraps an existing component
    static ManagedChild wrap(std::unique_ptr<ComponentType> component) {
        ManagedChild managed;
        managed.component_ = std::move(component);
        return managed;
    }

    // Destructor - RAII cleanup
    ~ManagedChild() {
        if (component_) {
            // Critical: remove from parent before unique_ptr deletes it
            auto* parent = component_->getParentComponent();
            if (parent) {
                parent->removeChildComponent(component_.get());
            }
            component_.reset();
        }
    }

    // Move-only
    ManagedChild(ManagedChild&&) noexcept = default;
    ManagedChild& operator=(ManagedChild&&) noexcept = default;
    ManagedChild(const ManagedChild&) = delete;
    ManagedChild& operator=(const ManagedChild&) = delete;

    // Smart pointer interface
    ComponentType* get() const {
        return component_.get();
    }
    ComponentType* operator->() const {
        return component_.get();
    }
    ComponentType& operator*() const {
        return *component_;
    }
    explicit operator bool() const {
        return component_ != nullptr;
    }

    // Release ownership
    std::unique_ptr<ComponentType> release() {
        return std::move(component_);
    }

  private:
    std::unique_ptr<ComponentType> component_;
};

/**
 * @brief Scoped component lifecycle manager - like Python's 'with' statement
 *
 * Ensures components are properly cleaned up even if exceptions are thrown.
 *
 * Usage:
 * @code
 * void loadPlugin() {
 *     auto guard = ScopedComponentGuard::create(pluginWindow);
 *
 *     // Do work with pluginWindow
 *     pluginWindow->loadState();
 *
 *     // If exception thrown, guard ensures cleanup
 *     if (someCondition) {
 *         throw std::runtime_error("Failed");
 *     }
 *
 *     guard.release();  // Success - don't delete
 * }  // guard goes out of scope, deletes pluginWindow if not released
 * @endcode
 */
template <typename T> class ScopedComponentGuard {
  public:
    explicit ScopedComponentGuard(T* component) : component_(component), released_(false) {}

    static ScopedComponentGuard create(T* component) {
        return ScopedComponentGuard(component);
    }

    ~ScopedComponentGuard() {
        if (!released_ && component_) {
            if constexpr (std::is_base_of<juce::Component, T>::value) {
                auto* parent = component_->getParentComponent();
                if (parent) {
                    parent->removeChildComponent(component_);
                }
            }
            delete component_;
        }
    }

    // Release ownership - component won't be deleted
    void release() {
        released_ = true;
    }

    // Access the component
    T* get() const {
        return component_;
    }
    T* operator->() const {
        return component_;
    }
    T& operator*() const {
        return *component_;
    }

    // Move-only
    ScopedComponentGuard(ScopedComponentGuard&& other) noexcept
        : component_(other.component_), released_(other.released_) {
        other.component_ = nullptr;
        other.released_ = true;
    }

    ScopedComponentGuard& operator=(ScopedComponentGuard&& other) noexcept {
        if (this != &other) {
            if (!released_ && component_) {
                delete component_;
            }
            component_ = other.component_;
            released_ = other.released_;
            other.component_ = nullptr;
            other.released_ = true;
        }
        return *this;
    }

    ScopedComponentGuard(const ScopedComponentGuard&) = delete;
    ScopedComponentGuard& operator=(const ScopedComponentGuard&) = delete;

  private:
    T* component_;
    bool released_;
};

}  // namespace magda
