#include "UserAlert.hpp"

#include <utility>

namespace magda {

namespace {
UserAlertHandler& handler() {
    static UserAlertHandler instance;
    return instance;
}
}  // namespace

void setUserAlertHandler(UserAlertHandler newHandler) {
    handler() = std::move(newHandler);
}

void notifyUserAlert(const juce::String& message) {
    if (auto& h = handler())
        h(message);
}

}  // namespace magda
