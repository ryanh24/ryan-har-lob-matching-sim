#pragma once
#include "lob/types.hpp"
#include "lob/order.hpp"
#include "lob/limit.hpp"
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

// std:: baselines exposing the same surface as the hand-rolled PriceTree / OrderIndex, so Book can
// be instantiated on either backend for a whole-engine head-to-head. Only used by the benchmark.

namespace lob {

// std::unordered_map baseline for the order-id index (chaining + per-node allocation).
struct StdIndex {
    std::unordered_map<OrderId, Order*> m;
    void   insert(OrderId k, Order* v) { m[k] = v; }
    Order* find(OrderId k) const { auto it = m.find(k); return it == m.end() ? nullptr : it->second; }
    void   erase(OrderId k) { m.erase(k); }
    size_t size() const { return m.size(); }
};

// std::map baseline for the price-level tree (node-allocated red-black tree). Limit lives in the map
// node, so a Limit* into it is stable (Order.parent stays valid across other inserts/erases).
struct StdTree {
    std::map<Price, Limit> m;
    Limit* find(Price p) { auto it = m.find(p); return it == m.end() ? nullptr : &it->second; }
    Limit* find_or_create(Price p) { Limit& l = m[p]; l.price = p; return &l; }
    void   erase(Price p) { m.erase(p); }
    Limit* min() { return m.empty() ? nullptr : &m.begin()->second; }   // best ask
    Limit* max() { return m.empty() ? nullptr : &m.rbegin()->second; }  // best bid
    void top_ascending(int n, std::vector<std::pair<Price, Qty>>& out) const {
        for (auto it = m.begin(); it != m.end() && (int)out.size() < n; ++it)
            out.emplace_back(it->first, it->second.volume);
    }
    void top_descending(int n, std::vector<std::pair<Price, Qty>>& out) const {
        for (auto it = m.rbegin(); it != m.rend() && (int)out.size() < n; ++it)
            out.emplace_back(it->first, it->second.volume);
    }
};

} // namespace lob
