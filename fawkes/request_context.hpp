// Copyright (c) 2026 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <any>
#include <concepts>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace fawkes {

class request_context {
public:
    // Provides strong exception guarantee, i.e. if constructing the new value throws,
    // the previous state is intact.
    template<typename T, typename... Args>
    requires(std::derived_from<T, typename T::base_type> &&
             !std::same_as<std::remove_cv_t<T>, std::remove_cv_t<typename T::base_type>>)
    T& emplace(Args&&... args) {
        using value_type = std::remove_cv_t<T>;
        std::any new_data(std::in_place_type<value_type>, std::forward<Args>(args)...);
        data_.swap(new_data);
        base_getter_ = &get_base<value_type>;
        return *std::any_cast<value_type>(&data_);
    }

    // Throws `std::bad_any_cast` on an empty context or a mismatched stored type.
    template<typename T>
    requires(!std::is_reference_v<T>)
    [[nodiscard]] T& as() {
        return std::any_cast<T&>(data_);
    }

    // Throws `std::bad_any_cast` on an empty context or a mismatched stored type.
    template<typename T>
    requires(!std::is_reference_v<T>)
    [[nodiscard]] const T& as() const {
        return std::any_cast<const T&>(data_);
    }

    // Returns nullptr on an empty context or a mismatched stored type.
    template<typename T>
    requires(!std::is_reference_v<T>)
    [[nodiscard]] T* as_if() noexcept {
        return std::any_cast<T>(&data_);
    }

    // Returns nullptr on an empty context or a mismatched stored type.
    template<typename T>
    requires(!std::is_reference_v<T>)
    [[nodiscard]] const T* as_if() const noexcept {
        return std::any_cast<T>(&data_);
    }

    // Throws `std::bad_any_cast` on an empty context or a mismatched base type.
    template<typename Base>
    requires(!std::is_reference_v<Base>)
    [[nodiscard]] Base& as_base() {
        return const_cast<Base&>(std::as_const(*this).as_base<Base>());
    }

    // Throws `std::bad_any_cast` on an empty context or a mismatched base type.
    template<typename Base>
    requires(!std::is_reference_v<Base>)
    [[nodiscard]] const Base& as_base() const {
        const auto* ptr = as_base_if<Base>();
        if (ptr == nullptr) {
            throw std::bad_any_cast{};
        }
        return *ptr;
    }

    // Returns nullptr on an empty context or a mismatched base type.
    template<typename Base>
    requires(!std::is_reference_v<Base>)
    [[nodiscard]] Base* as_base_if() noexcept {
        return const_cast<Base*>(std::as_const(*this).as_base_if<Base>());
    }

    // Returns nullptr on an empty context or a mismatched base type.
    template<typename Base>
    requires(!std::is_reference_v<Base>)
    [[nodiscard]] const Base* as_base_if() const noexcept {
        const auto* ptr = base_getter_ ? base_getter_(data_, typeid(Base)) : nullptr;
        return static_cast<const Base*>(ptr);
    }

private:
    using base_getter = const void* (*)(const std::any&, const std::type_info&) noexcept;

    template<typename T>
    static const void* get_base(const std::any& data, const std::type_info& requested) noexcept {
        using base_type = std::remove_cv_t<typename T::base_type>;
        if (requested != typeid(base_type)) {
            return nullptr;
        }

        // The base subobject may have a different address from the complete object.
        const base_type* base = std::any_cast<T>(&data);
        return base;
    }

    std::any data_;
    base_getter base_getter_{nullptr};
};

} // namespace fawkes
