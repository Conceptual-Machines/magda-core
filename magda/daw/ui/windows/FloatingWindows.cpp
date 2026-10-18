#include "FloatingWindows.hpp"

#include <vector>

#if JUCE_WINDOWS
    #include <windows.h>
#elif JUCE_MAC
    #include <objc/message.h>
    #include <objc/runtime.h>
#endif

namespace magda {

namespace {
const juce::Identifier kFloatsOverMain{"magdaFloatsOverMain"};

juce::Component::SafePointer<juce::Component> owner;
std::vector<juce::Component::SafePointer<juce::Component>> hiddenWhileMinimised;

template <typename Fn> void forEachFloatingWindow(Fn&& fn) {
    auto& desktop = juce::Desktop::getInstance();
    for (int i = desktop.getNumComponents(); --i >= 0;)
        if (auto* window = desktop.getComponent(i); window->getProperties()[kFloatsOverMain])
            fn(*window);
}

#if JUCE_WINDOWS
/// Win32 keeps an owned window above its owner, and minimises and restores it with it.
void setOwner(juce::Component& window, juce::Component* newOwner) {
    auto* peer = window.getPeer();
    auto* ownerPeer = newOwner != nullptr ? newOwner->getPeer() : nullptr;
    if (peer == nullptr)
        return;
    SetWindowLongPtr(
        static_cast<HWND>(peer->getNativeHandle()), GWLP_HWNDPARENT,
        reinterpret_cast<LONG_PTR>(ownerPeer != nullptr ? ownerPeer->getNativeHandle() : nullptr));
}
#elif JUCE_MAC
/// A floating-level window otherwise stays over other apps while MAGDA is in the background.
void hideWhenAppInactive(juce::Component& window) {
    auto* peer = window.getPeer();
    if (peer == nullptr)
        return;
    auto* view = static_cast<id>(peer->getNativeHandle());
    auto* nsWindow =
        reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(view, sel_registerName("window"));
    if (nsWindow != nil)
        reinterpret_cast<void (*)(id, SEL, BOOL)>(objc_msgSend)(
            nsWindow, sel_registerName("setHidesOnDeactivate:"), YES);
}
#endif

void floatWindow(juce::Component& window) {
    window.getProperties().set(kFloatsOverMain, true);
#if JUCE_WINDOWS
    setOwner(window, owner);
#else
    window.setAlwaysOnTop(true);
    #if JUCE_MAC
    hideWhenAppInactive(window);
    #endif
#endif
}

void releaseWindow(juce::Component& window) {
#if JUCE_WINDOWS
    auto* peer = window.getPeer();
    auto* ownerPeer = owner != nullptr ? owner->getPeer() : nullptr;
    if (peer != nullptr && ownerPeer != nullptr &&
        GetForegroundWindow() == static_cast<HWND>(peer->getNativeHandle()))
        SetForegroundWindow(static_cast<HWND>(ownerPeer->getNativeHandle()));
#else
    juce::ignoreUnused(window);
#endif
}
}  // namespace

void setFloatingWindowOwner(juce::Component* mainWindow) {
    owner = mainWindow;
    detail::floatingWindowHook() = mainWindow != nullptr ? &floatWindow : nullptr;
    detail::floatingWindowReleaseHook() = mainWindow != nullptr ? &releaseWindow : nullptr;
#if JUCE_WINDOWS
    // Destroying an owner destroys its owned windows, plugin editors included.
    if (mainWindow == nullptr)
        forEachFloatingWindow([](juce::Component& window) { setOwner(window, nullptr); });
#endif
}

void floatingWindowOwnerMinimised(bool minimised) {
#if JUCE_WINDOWS
    juce::ignoreUnused(minimised);  // Owned windows follow their owner.
#else
    // Peer-level, so the windows' own visibility, which the plugin window state reads, is kept.
    if (minimised) {
        forEachFloatingWindow([](juce::Component& window) {
            auto* peer = window.getPeer();
            if (peer != nullptr && window.isVisible() && !peer->isMinimised()) {
                peer->setVisible(false);
                hiddenWhileMinimised.emplace_back(&window);
            }
        });
        return;
    }
    for (auto& window : hiddenWhileMinimised)
        if (window != nullptr && window->isVisible())
            if (auto* peer = window->getPeer())
                peer->setVisible(true);
    hiddenWhileMinimised.clear();
#endif
}

}  // namespace magda
