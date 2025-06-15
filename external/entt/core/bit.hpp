#ifndef ENTT_CORE_BIT_HPP
#define ENTT_CORE_BIT_HPP

#include <cstddef>
#include <limits>
#include <type_traits>
#include <cstdio>          // for fprintf
#include "../config/config.h"

namespace entt {

    /**
     * @brief Returns the number of set bits in a value (waiting for C++20 and
     * `std::popcount`).
     */
    template<typename Type>
    [[nodiscard]] constexpr std::enable_if_t<std::is_unsigned_v<Type>, int>
        popcount(const Type value) noexcept {
        return value ? (int(value & 1) + popcount(static_cast<Type>(value >> 1))) : 0;
    }

    /**
     * @brief Checks whether a value is a power of two or not (waiting for C++20 and
     * `std::has_single_bit`).
     */
    template<typename Type>
    [[nodiscard]] constexpr std::enable_if_t<std::is_unsigned_v<Type>, bool>
        has_single_bit(const Type value) noexcept {
        return value && ((value & (value - 1)) == 0);
    }

    /**
     * @brief Computes the smallest power of two greater than or equal to a value
     * (waiting for C++20 and `std::bit_ceil`).
     */
    template<typename Type>
    [[nodiscard]] constexpr std::enable_if_t<std::is_unsigned_v<Type>, Type>
        next_power_of_two(const Type value) noexcept {
        ENTT_ASSERT_CONSTEXPR(
            value < (Type{ 1u } << (std::numeric_limits<Type>::digits - 1)),
            "Numeric limits exceeded"
        );
        Type curr = value - (value != 0u);
        for (int next = 1; next < std::numeric_limits<Type>::digits; next <<= 1) {
            curr |= (curr >> next);
        }
        return ++curr;
    }

    /**
     * @brief Fast modulo utility function (powers of two only).
     * @param value A value of unsigned integer type.
     * @param mod Modulus, must be a power of two.
     * @return The remainder value % mod.
     */
    template<typename Type>
    [[nodiscard]] constexpr std::enable_if_t<std::is_unsigned_v<Type>, Type>
        fast_mod(const Type value, const std::size_t mod) noexcept {
        // Diagnostic hook: log any non-power-of-two mod at runtime
        if (!has_single_bit(mod)) {
            std::fprintf(
                stderr,
                "[Entt Debug] fast_mod called with value=%zu mod=%zu (not power-of-two)\n",
                static_cast<size_t>(value),
                static_cast<size_t>(mod)
            );
        }
        ENTT_ASSERT_CONSTEXPR(has_single_bit(mod), "Value must be a power of two");
        return static_cast<Type>(value & (mod - 1u));
    }

} // namespace entt

#endif // ENTT_CORE_BIT_HPP
