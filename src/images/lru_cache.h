// Least-recently-used map bounded by entry count and total bytes (host-testable).
// Used for decoded logo textures: values are shared_ptrs, so evicting an entry only drops the cache's
// reference; a widget still showing the image keeps it alive until it shows something else.

#ifndef PS4IPTV_IMAGES_LRU_CACHE_H
#define PS4IPTV_IMAGES_LRU_CACHE_H

#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace images {

    template<typename V>
    class LruCache {

    public:

        LruCache(size_t maxEntries, int64_t maxBytes) : maxEntries(maxEntries), maxBytes(maxBytes) {}

        // inserts or replaces; returns the keys evicted to stay within the bounds (never `key` itself)
        std::vector<std::string> put(const std::string &key, V value, int64_t bytes) {
            erase(key);
            order.push_front(Node{key, std::move(value), bytes});
            index[key] = order.begin();
            total += bytes;
            std::vector<std::string> evicted;
            while ((order.size() > maxEntries || total > maxBytes) && order.size() > 1) {
                Node &last = order.back();
                evicted.push_back(last.key);
                total -= last.bytes;
                index.erase(last.key);
                order.pop_back();
            }
            return evicted;
        }

        // nullptr when absent; marks the entry as most recently used
        V *get(const std::string &key) {
            auto it = index.find(key);
            if (it == index.end()) {
                return nullptr;
            }
            order.splice(order.begin(), order, it->second);
            return &it->second->value;
        }

        bool contains(const std::string &key) const { return index.count(key) != 0; }

        void erase(const std::string &key) {
            auto it = index.find(key);
            if (it != index.end()) {
                total -= it->second->bytes;
                order.erase(it->second);
                index.erase(it);
            }
        }

        void clear() {
            order.clear();
            index.clear();
            total = 0;
        }

        size_t size() const { return order.size(); }

        int64_t bytes() const { return total; }

    private:

        struct Node {
            std::string key;
            V value;
            int64_t bytes;
        };

        size_t maxEntries;
        int64_t maxBytes;
        int64_t total = 0;
        std::list<Node> order;   // front = most recently used
        std::unordered_map<std::string, typename std::list<Node>::iterator> index;
    };
}

#endif // PS4IPTV_IMAGES_LRU_CACHE_H
