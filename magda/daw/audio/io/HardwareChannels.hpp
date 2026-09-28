#pragma once

/**
 * @file HardwareChannels.hpp
 * @brief The hardware channels an engine renders through, as the routing menus read them (#2748).
 */

#include <juce_core/juce_core.h>

#include <map>

namespace magda {

/**
 * @brief What is open on the audio interface, and word when that changes.
 *
 * AudioIOService answers under the native engine, and an adapter over Tracktion's wave devices
 * under Tracktion, so the menus hold nothing of either.
 */
class HardwareChannels {
  public:
    /** @brief One direction's open channels. */
    struct Direction {
        juce::BigInteger open;
        /// Every channel the interface has, by its own names (#2737).
        juce::StringArray channelNames;
        /// The name saved routes give each open channel (#2747).
        std::map<int, juce::String> routeNames;
    };

    /** @brief Told when channels open or close, on the message thread. */
    class Listener {
      public:
        virtual ~Listener() = default;
        virtual void hardwareChannelsChanged() = 0;
    };

    virtual ~HardwareChannels() = default;

    /** @brief Whether an interface is open at all. */
    virtual bool isOpen() const = 0;
    virtual Direction inputs() const = 0;
    virtual Direction outputs() const = 0;

    void addListener(Listener* listener) {
        listeners_.add(listener);
    }
    void removeListener(Listener* listener) {
        listeners_.remove(listener);
    }

  protected:
    void notifyChanged() {
        listeners_.call([](Listener& listener) { listener.hardwareChannelsChanged(); });
    }

  private:
    juce::ListenerList<Listener> listeners_;
};

}  // namespace magda
