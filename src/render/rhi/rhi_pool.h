#pragma once

// Backend-internal slot pool behind the generational handles of rhi.h.
// raw = (generation << 20) | (index + 1); a stale handle fails get() instead of aliasing a new resource.

#include "render/rhi/rhi.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace sh::rhi::detail {

template <class H, class T>
class Pool {
public:
    H add(T value) {
        std::uint32_t index;
        if (!free_.empty()) {
            index = free_.back();
            free_.pop_back();
        } else {
            index = static_cast<std::uint32_t>(slots_.size());
            slots_.push_back({});
        }
        Slot& slot = slots_[index];
        slot.value = std::move(value);
        return H{(slot.generation << kIndexBits) | (index + 1)};
    }

    T* get(H handle) {
        if (!handle) return nullptr;
        const std::uint32_t index = (handle.raw & kIndexMask) - 1;
        if (index >= slots_.size()) return nullptr;
        Slot& slot = slots_[index];
        if (!slot.value || slot.generation != (handle.raw >> kIndexBits)) return nullptr;
        return &*slot.value;
    }

    std::optional<T> remove(H handle) {
        T* value = get(handle);
        if (!value) return std::nullopt;
        const std::uint32_t index = (handle.raw & kIndexMask) - 1;
        Slot& slot = slots_[index];
        std::optional<T> out = std::move(slot.value);
        slot.value.reset();
        slot.generation = (slot.generation + 1) & kGenerationMask;
        free_.push_back(index);
        return out;
    }

    template <class F>
    void forEach(F&& f) {
        for (Slot& slot : slots_)
            if (slot.value) f(*slot.value);
    }

private:
    static constexpr std::uint32_t kIndexBits = 20;
    static constexpr std::uint32_t kIndexMask = (1u << kIndexBits) - 1;
    static constexpr std::uint32_t kGenerationMask = (1u << (32 - kIndexBits)) - 1;

    struct Slot {
        std::optional<T> value;
        std::uint32_t generation = 1;
    };
    // A deque: the backends keep the pointers get() returns (the bound pipeline, the pipeline whose vertex layout is
    // set) while they create further resources, and a deque never moves a slot when it grows.
    std::deque<Slot> slots_;
    std::vector<std::uint32_t> free_;
};

}  // namespace sh::rhi::detail
