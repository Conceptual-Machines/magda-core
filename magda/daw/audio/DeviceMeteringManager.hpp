#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <atomic>
#include <map>
#include <memory>

#include "../core/TypeIds.hpp"

namespace magda {

namespace te = tracktion;
class PluginManager;

/**
 * @brief Manages per-device LevelMeasurer instances for peak metering
 *
 * Each device in the chain gets a LevelMeasurer + Client pair. During TE graph
 * building, createNodeForPlugin() wraps each PluginNode with a LevelMeasuringNode
 * that feeds audio through the measurer. AudioBridge polls all clients on its
 * 30 FPS timer and stores peaks in atomics. DeviceSlotComponent reads the atomics
 * lock-free for UI painting.
 *
 * Thread Safety:
 * - getOrCreateMeasurer(): called from message thread during graph building
 * - updateAllClients(): called from message thread (timer)
 * - getLatestLevels(): called from message thread (UI) — reads atomics (lock-free)
 * - Static editMap_: protected by editMapLock_
 */
class DeviceMeteringManager {
  public:
    DeviceMeteringManager() = default;
    ~DeviceMeteringManager() = default;

    /**
     * @brief Set the PluginManager for plugin→device lookup
     */
    void setPluginManager(PluginManager* pm) {
        pluginManager_ = pm;
    }

    /**
     * @brief Get or create a LevelMeasurer for a device (called during graph building)
     * @param deviceId The MAGDA device ID
     * @return Reference to the LevelMeasurer for this device
     */
    te::LevelMeasurer& getOrCreateMeasurer(DeviceId deviceId);

    /**
     * @brief Remove the measurer for a device (called when device is removed)
     * @param deviceId The MAGDA device ID
     */
    void removeMeasurer(DeviceId deviceId);

    /**
     * @brief Look up DeviceId from a TE plugin pointer
     * @param plugin The TE plugin to look up
     * @return The DeviceId, or INVALID_DEVICE_ID if not found
     */
    DeviceId getDeviceIdForPlugin(te::Plugin* plugin) const;

    /**
     * @brief Poll all clients and store latest peaks (called from AudioBridge timer)
     */
    void updateAllClients();

    /**
     * @brief Level data for a single device
     */
    struct DeviceMeterData {
        float peakL = 0.f;
        float peakR = 0.f;
    };

    /**
     * @brief Read latest level for a device (called from UI thread, lock-free)
     * @param deviceId The MAGDA device ID
     * @param out Output data
     * @return true if device was found
     */
    bool getLatestLevels(DeviceId deviceId, DeviceMeterData& out) const;

    /**
     * @brief Set per-device gain (linear) for use in the audio graph
     * @param deviceId The MAGDA device ID
     * @param gainLinear Gain value in linear scale (1.0 = unity)
     */
    void setGain(DeviceId deviceId, float gainLinear);

    /**
     * @brief Get pointer to gain atomic for a device (for DeviceGainNode in the graph)
     * @param deviceId The MAGDA device ID
     * @return Pointer to the atomic, or nullptr if device not found
     */
    std::atomic<float>* getGainAtomic(DeviceId deviceId);

    /**
     * @brief Clear all measurers (called during shutdown)
     */
    void clear();

    // Static per-Edit accessor (for TE graph builder to find us without MAGDA headers)
    static DeviceMeteringManager* getInstanceForEdit(te::Edit& edit);
    static void registerForEdit(te::Edit& edit, DeviceMeteringManager* mgr);
    static void unregisterForEdit(te::Edit& edit);

  private:
    struct Entry {
        te::LevelMeasurer measurer;
        te::LevelMeasurer::Client client;
        std::atomic<float> peakL{0.f};
        std::atomic<float> peakR{0.f};
        std::atomic<float> gainLinear{1.0f};
        bool clientRegistered = false;
    };

    std::map<DeviceId, std::unique_ptr<Entry>> entries_;
    juce::CriticalSection lock_;
    PluginManager* pluginManager_ = nullptr;

    static std::map<te::Edit*, DeviceMeteringManager*> editMap_;
    static juce::CriticalSection editMapLock_;
};

}  // namespace magda
