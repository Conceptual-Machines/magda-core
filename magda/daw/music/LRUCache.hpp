#pragma once

#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <unordered_map>

namespace magda::music {

template <typename Key, typename Value> class LRUCache {
  public:
    using KeyValuePair = std::pair<Key, Value>;
    using ListIterator = typename std::list<KeyValuePair>::iterator;

    explicit LRUCache(size_t capacity) : maxCapacity(capacity) {
        if (capacity == 0)
            throw std::invalid_argument("LRUCache capacity must be greater than 0");
    }

    std::optional<Value> get(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex);
        totalRequests++;
        auto it = cacheMap.find(key);
        if (it == cacheMap.end())
            return std::nullopt;
        hits++;
        moveToFront(it->second);
        return it->second->second;
    }

    void put(const Key& key, const Value& value) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = cacheMap.find(key);
        if (it != cacheMap.end()) {
            it->second->second = value;
            moveToFront(it->second);
            return;
        }
        if (cacheList.size() >= maxCapacity)
            evictLRU();
        cacheList.emplace_front(key, value);
        cacheMap[key] = cacheList.begin();
    }

    bool contains(const Key& key) const {
        std::lock_guard<std::mutex> lock(mutex);
        return cacheMap.find(key) != cacheMap.end();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex);
        return cacheList.size();
    }

    size_t capacity() const {
        return maxCapacity;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex);
        return cacheList.empty();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        cacheList.clear();
        cacheMap.clear();
    }

    std::pair<size_t, size_t> getStats() const {
        std::lock_guard<std::mutex> lock(mutex);
        return {hits, totalRequests};
    }

    double getHitRate() const {
        std::lock_guard<std::mutex> lock(mutex);
        if (totalRequests == 0)
            return 0.0;
        return static_cast<double>(hits) / static_cast<double>(totalRequests);
    }

    void resetStats() {
        std::lock_guard<std::mutex> lock(mutex);
        hits = 0;
        totalRequests = 0;
    }

  private:
    mutable std::mutex mutex;
    size_t maxCapacity;
    std::list<KeyValuePair> cacheList;
    std::unordered_map<Key, ListIterator> cacheMap;
    mutable size_t hits = 0;
    mutable size_t totalRequests = 0;

    void moveToFront(ListIterator it) {
        if (it != cacheList.begin())
            cacheList.splice(cacheList.begin(), cacheList, it);
    }

    void evictLRU() {
        if (!cacheList.empty()) {
            cacheMap.erase(cacheList.back().first);
            cacheList.pop_back();
        }
    }
};

}  // namespace magda::music
