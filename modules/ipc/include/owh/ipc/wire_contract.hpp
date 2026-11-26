#pragma once
#include <boost/pfr/core.hpp>
#include <type_traits>

namespace overwolf_hook::ipc {
/** Inspect every nested field at compile time. A new pointer, owner, reference
 * or bool cannot silently become part of a memcpy-based service message.
 * Handle tokens are explicit fixed-width integers in the Core contracts. */
template <class T> constexpr bool wire_safe() {
    using Value = std::remove_cvref_t<T>;
    if constexpr (std::is_integral_v<Value>) {
        return !std::is_same_v<Value, bool> && sizeof(Value) <= 8;
    } else if constexpr (std::is_enum_v<Value>) {
        return wire_safe<std::underlying_type_t<Value>>();
    } else if constexpr (std::is_array_v<Value>) {
        return wire_safe<std::remove_extent_t<Value>>();
    } else if constexpr (std::is_aggregate_v<Value> && std::is_standard_layout_v<Value> &&
                         std::is_trivially_copyable_v<Value>) {
        bool valid = true;
        boost::pfr::for_each_field(
            Value{}, [&](const auto &field) { valid = valid && wire_safe<decltype(field)>(); });
        return valid;
    } else {
        return false;
    }
}
} // namespace overwolf_hook::ipc
