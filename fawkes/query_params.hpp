// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <concepts>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <boost/url/params_ref.hpp>
#include <boost/url/params_view.hpp>
#include <esl/ignore_unused.h>

namespace fawkes {

namespace urls = boost::urls;

namespace detail {

static_assert(std::is_nothrow_copy_constructible_v<urls::params_view>);
static_assert(std::is_nothrow_copy_constructible_v<urls::params_ref>);

template<typename Impl>
requires std::same_as<Impl, urls::params_view> || std::same_as<Impl, urls::params_ref>
struct query_params_base {
    explicit query_params_base(const Impl& impl) noexcept
        : impl_(impl) {}

    [[nodiscard]] std::optional<std::string> get(std::string_view key) const {
        auto it = impl_.find(key);
        if (it == impl_.end()) {
            return std::nullopt;
        }

        return std::move((*it).value);
    }

    [[nodiscard]] std::string get_or(std::string_view key, std::string_view value) const {
        auto it = impl_.find(key);
        if (it == impl_.end()) {
            return std::string{value};
        }

        return std::move((*it).value);
    }

    Impl impl_;
};

} // namespace detail

class query_params_view : private detail::query_params_base<urls::params_view> {
public:
    using query_params_base::query_params_base;

    using query_params_base::get;
    using query_params_base::get_or;
};

class query_params_ref : private detail::query_params_base<urls::params_ref> {
public:
    using query_params_base::query_params_base;

    using query_params_base::get;
    using query_params_base::get_or;

    // If `key` is not contained, insert the param {key, value}.
    // Otherwise, one of the matching elements has its value changed to `value`; remaining elements
    // that match `key` are erased.
    // Key comparison is case-sensitive.
    void set(std::string_view key, std::string_view value) {
        esl::ignore_unused(impl_.set(key, value));
    }

    // Remove all matched elements, and returns the number of elements removed.
    // Key comparison is case-sensitive.
    std::size_t del(std::string_view key) noexcept {
        return impl_.erase(key);
    }
};

} // namespace fawkes
