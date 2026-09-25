#pragma once
// Generational handle table (engine-internal). Handle layout: [ generation:16 | index:32 ]
// packed into the low 48 bits of the public 64-bit value, never 0 for a live handle.
// A destroyed slot bumps its generation, so stale handles fail isValid()/lookups
// deterministically instead of dereferencing freed state.

#include <cstdint>
#include <vector>

namespace krsg::core
{

template <typename T> class HandleTable
{
public:
    std::uint64_t create(T&& item)
    {
        std::uint32_t index;
        if (!freeList_.empty()) {
            index = freeList_.back();
            freeList_.pop_back();
        } else {
            index = static_cast<std::uint32_t>(slots_.size());
            slots_.push_back(Slot{});
        }
        Slot& slot = slots_[index];
        slot.item = static_cast<T&&>(item);
        slot.live = true;
        return pack(index, slot.generation);
    }

    bool destroy(std::uint64_t handle)
    {
        Slot* slot = resolve(handle);
        if (slot == nullptr) {
            return false;
        }
        slot->live = false;
        slot->generation = static_cast<std::uint16_t>(slot->generation + 1u);
        slot->item = T{};
        freeList_.push_back(indexOf(handle));
        return true;
    }

    T* get(std::uint64_t handle)
    {
        Slot* slot = resolve(handle);
        return slot != nullptr ? &slot->item : nullptr;
    }

    bool isValid(std::uint64_t handle) const { return const_cast<HandleTable*>(this)->resolve(handle) != nullptr; }

    std::size_t liveCount() const
    {
        std::size_t n = 0;
        for (const Slot& s : slots_) {
            n += s.live ? 1u : 0u;
        }
        return n;
    }

private:
    struct Slot {
        T item{};
        std::uint16_t generation = 1; // starts at 1 so a packed handle is never 0
        bool live = false;
    };

    static std::uint64_t pack(std::uint32_t index, std::uint16_t generation)
    {
        return (static_cast<std::uint64_t>(generation) << 32) | (static_cast<std::uint64_t>(index) + 1u);
    }
    static std::uint32_t indexOf(std::uint64_t handle)
    {
        return static_cast<std::uint32_t>((handle & 0xFFFFFFFFu) - 1u);
    }

    Slot* resolve(std::uint64_t handle)
    {
        if ((handle & 0xFFFFFFFFu) == 0) {
            return nullptr;
        }
        const std::uint32_t index = indexOf(handle);
        if (index >= slots_.size()) {
            return nullptr;
        }
        Slot& slot = slots_[index];
        const auto generation = static_cast<std::uint16_t>(handle >> 32);
        if (!slot.live || slot.generation != generation) {
            return nullptr;
        }
        return &slot;
    }

    std::vector<Slot> slots_;
    std::vector<std::uint32_t> freeList_;
};

} // namespace krsg::core
