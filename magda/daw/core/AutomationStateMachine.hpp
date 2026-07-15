#pragma once

namespace magda {

/**
 * Which source currently has authority over an automated parameter.
 *
 * Reading and Disabled are stable states. Touching and Writing exist only for
 * the lifetime of a user gesture and are deliberately never persisted.
 */
enum class AutomationAuthorityState {
    Reading,
    Disabled,
    Touching,
    Writing,
};

enum class AutomationAuthorityEvent {
    Enable,
    Disable,
    BeginTouch,
    BeginWrite,
    EndGesture,
    ResetRuntime,
};

constexpr AutomationAuthorityState transitionAutomationAuthority(AutomationAuthorityState state,
                                                                 AutomationAuthorityEvent event) {
    switch (event) {
        case AutomationAuthorityEvent::Enable:
            return AutomationAuthorityState::Reading;
        case AutomationAuthorityEvent::Disable:
            return AutomationAuthorityState::Disabled;
        case AutomationAuthorityEvent::BeginTouch:
            return state == AutomationAuthorityState::Disabled ? state
                                                               : AutomationAuthorityState::Touching;
        case AutomationAuthorityEvent::BeginWrite:
            return state == AutomationAuthorityState::Disabled ? state
                                                               : AutomationAuthorityState::Writing;
        case AutomationAuthorityEvent::EndGesture:
        case AutomationAuthorityEvent::ResetRuntime:
            return state == AutomationAuthorityState::Touching ||
                           state == AutomationAuthorityState::Writing
                       ? AutomationAuthorityState::Reading
                       : state;
    }
    return state;
}

constexpr bool isAutomationPlaybackSuppressed(AutomationAuthorityState state) {
    return state != AutomationAuthorityState::Reading;
}

constexpr bool isAutomationPersistentlyDisabled(AutomationAuthorityState state) {
    return state == AutomationAuthorityState::Disabled;
}

constexpr bool isAutomationGestureActive(AutomationAuthorityState state) {
    return state == AutomationAuthorityState::Touching ||
           state == AutomationAuthorityState::Writing;
}

constexpr AutomationAuthorityState automationAuthorityForPersistence(
    AutomationAuthorityState state) {
    return isAutomationPersistentlyDisabled(state) ? AutomationAuthorityState::Disabled
                                                   : AutomationAuthorityState::Reading;
}

}  // namespace magda
