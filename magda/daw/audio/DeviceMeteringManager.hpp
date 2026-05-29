#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <atomic>
#include <map>
#include <memory>

#include "../core/ChainNodePath.hpp"

namespace magda {

namespace te = tracktion;
class PluginManager;

/**
 * @brief Manages per-device LevelMeasurer instances for peak metering
 *
 * Track-level plugins use LevelMeasurer + Client pairs fed by the TE graph hook.
 * Wrapped instruments use a MAGDA-owned InstrumentMeterTapPlugin inside the rack,
 * which writes block peaks to realtime accumulators that updateAllClients()
 * consumes and clears on the message thread.
 *
 * Thread Safety:
 * - getOrCreateMeasurer(): called from message thread during graph building
 * - getRealtimeTap(): called from message thread when wiring a tap plugin
 * - updateAllClients(): called from message thread (timer)
 * - getLatestLevels(): called from message thread (UI)
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
     * @param devicePath The MAGDA device path
     * @return Reference to the LevelMeasurer for this device
     */
    te::LevelMeasurer& getOrCreateMeasurer(const ChainNodePath& devicePath);

    // Legacy bare-id entry point used by instrument meter taps that do not yet
    // persist a full ChainNodePath. Prefer the path overload for visible devices.
    te::LevelMeasurer& getOrCreateMeasurer(DeviceId deviceId);

    /**
     * @brief Remove the measurer for a device (called when device is removed)
     * @param devicePath The MAGDA device path
     */
    void removeMeasurer(const ChainNodePath& devicePath);
    void removeMeasurer(DeviceId deviceId);

    /**
     * @brief Look up DeviceId from a TE plugin pointer
     * @param plugin The TE plugin to look up
     * @return The DeviceId, or INVALID_DEVICE_ID if not found
     */
    DeviceId getDeviceIdForPlugin(te::Plugin* plugin) const;

    /**
     * @brief Look up ChainNodePath from a TE plugin pointer
     * @param plugin The TE plugin to look up
     * @return The ChainNodePath, or an invalid path if not found
     */
    ChainNodePath getDevicePathForPlugin(te::Plugin* plugin) const;

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
     * @param devicePath The MAGDA device path
     * @param out Output data
     * @return true if device was found
     */
    bool getLatestLevels(const ChainNodePath& devicePath, DeviceMeterData& out) const;
    bool getLatestLevels(DeviceId deviceId, DeviceMeterData& out) const;

    /**
     * @brief Set per-device gain (linear) for use in the audio graph
     * @param devicePath The MAGDA device path
     * @param gainLinear Gain value in linear scale (1.0 = unity)
     */
    void setGain(const ChainNodePath& devicePath, float gainLinear);
    void setGain(DeviceId deviceId, float gainLinear);

    /**
     * @brief Get pointer to gain atomic for a device (for DeviceGainNode in the graph)
     * @param devicePath The MAGDA device path
     * @return Pointer to the atomic, or nullptr if device not found
     */
    std::atomic<float>* getGainAtomic(const ChainNodePath& devicePath);
    std::atomic<float>* getGainAtomic(DeviceId deviceId);

    /**
     * @brief Directly set peak levels for a device (bypasses LevelMeasurer)
     *
     * Used when the device is inside a MAGDA rack where we can't intercept
     * per-plugin audio buffers.  Feed the rack's output levels instead.
     */
    void setDirectLevels(const ChainNodePath& devicePath, float peakL, float peakR);
    void setDirectLevels(DeviceId deviceId, float peakL, float peakR);

    /**
     * @brief Ensure an entry exists for a device (creates if missing)
     */
    void ensureEntry(const ChainNodePath& devicePath);
    void ensureEntry(DeviceId deviceId);

    struct RealtimeTapStorage {
        std::atomic<float> peakL{0.f};
        std::atomic<float> peakR{0.f};
        std::atomic<float> gainLinear{1.0f};
    };

    struct RealtimeTap {
        std::shared_ptr<RealtimeTapStorage> storage;
        std::atomic<float>* peakL = nullptr;
        std::atomic<float>* peakR = nullptr;
        std::atomic<float>* gainLinear = nullptr;

        bool isValid() const {
            return storage != nullptr && peakL != nullptr && peakR != nullptr &&
                   gainLinear != nullptr;
        }
    };

    /**
     * @brief Get stable atomic endpoints for audio-thread owned metering.
     *
     * The returned pointers are backed by storage shared with the tap plugin, so
     * removeMeasurer() and clear() can drop manager entries without invalidating
     * an already-wired audio-thread tap.
     */
    RealtimeTap getRealtimeTap(const ChainNodePath& devicePath);
    RealtimeTap getRealtimeTap(DeviceId deviceId);

    /**
     * @brief Directly set peak levels for a rack (bypasses LevelMeasurer)
     */
    void setRackDirectLevels(RackId rackId, float peakL, float peakR);

    /**
     * @brief Ensure a rack metering entry exists (creates if missing)
     */
    void ensureRackEntry(RackId rackId);

    /**
     * @brief Read latest level for a rack (called from UI thread)
     */
    bool getRackLatestLevels(RackId rackId, DeviceMeterData& out) const;

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
        std::shared_ptr<RealtimeTapStorage> realtimeTap;
        bool clientRegistered = false;
    };

    struct SimpleEntry {
        std::atomic<float> peakL{0.f};
        std::atomic<float> peakR{0.f};
    };

    static ChainNodePath legacyPathForDeviceId(DeviceId deviceId);
    Entry& ensureEntryLocked(const ChainNodePath& devicePath);

    std::map<ChainNodePath, std::unique_ptr<Entry>> entries_;
    std::map<RackId, std::unique_ptr<SimpleEntry>> rackEntries_;
    juce::CriticalSection lock_;
    PluginManager* pluginManager_ = nullptr;

    static std::map<te::Edit*, DeviceMeteringManager*> editMap_;
    static juce::CriticalSection editMapLock_;
};

}  // namespace magda
