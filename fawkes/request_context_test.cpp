// Copyright (c) 2026 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include <any>
#include <concepts>
#include <stdexcept>
#include <utility>

#include <doctest/doctest.h>

#include "fawkes/request_context.hpp"

namespace {

template<typename T, typename... Args>
concept can_emplace_context = requires(fawkes::request_context& ctx, Args&&... args) {
    ctx.emplace<T>(std::forward<Args>(args)...);
};

struct registered_base {
    int value = 0;
};

struct unrelated_context {
    using base_type = registered_base;
};

struct privately_derived_context : private registered_base {
    using base_type = registered_base;
};

struct self_registered_context {
    using base_type = const self_registered_context;
};

struct context_value : registered_base {
    using base_type = registered_base;

    context_value() = default;
    context_value(const context_value&) = default;
    context_value(context_value&&) noexcept = default;
    context_value& operator=(const context_value&) = default;
    context_value& operator=(context_value&&) noexcept = default;

    // A polymorphic derived object can have its base at a nonzero offset.
    virtual ~context_value() = default;
};

TEST_SUITE_BEGIN("Request Context");

TEST_CASE("An empty context returns null pointers and throws on reference access") {
    fawkes::request_context ctx;
    const auto& const_ctx = ctx;

    CHECK_EQ(ctx.as_if<context_value>(), nullptr);
    CHECK_EQ(const_ctx.as_if<context_value>(), nullptr);
    CHECK_EQ(ctx.as_base_if<registered_base>(), nullptr);
    CHECK_EQ(const_ctx.as_base_if<registered_base>(), nullptr);
    CHECK_THROWS_AS(static_cast<void>(ctx.as<context_value>()), std::bad_any_cast);
    CHECK_THROWS_AS(static_cast<void>(const_ctx.as<context_value>()), std::bad_any_cast);
    CHECK_THROWS_AS(static_cast<void>(ctx.as_base<registered_base>()), std::bad_any_cast);
    CHECK_THROWS_AS(static_cast<void>(const_ctx.as_base<registered_base>()), std::bad_any_cast);
}

TEST_CASE("Accessors preserve constness and expose the stored value and its registered base") {
    static_assert(can_emplace_context<context_value>);
    static_assert(!can_emplace_context<registered_base>);
    static_assert(!can_emplace_context<unrelated_context>);
    static_assert(!can_emplace_context<privately_derived_context>);
    static_assert(!can_emplace_context<self_registered_context>);

    fawkes::request_context ctx;
    const auto& const_ctx = ctx;
    static_assert(std::same_as<decltype(ctx.as<context_value>()), context_value&>);
    static_assert(std::same_as<decltype(const_ctx.as<context_value>()), const context_value&>);
    static_assert(std::same_as<decltype(ctx.as_if<context_value>()), context_value*>);
    static_assert(std::same_as<decltype(const_ctx.as_if<context_value>()), const context_value*>);
    static_assert(std::same_as<decltype(ctx.as_base<registered_base>()), registered_base&>);
    static_assert(std::same_as<decltype(const_ctx.as_base<registered_base>()),
                               const registered_base&>);
    static_assert(std::same_as<decltype(ctx.as_base_if<registered_base>()), registered_base*>);
    static_assert(std::same_as<decltype(const_ctx.as_base_if<registered_base>()),
                               const registered_base*>);
    static_assert(noexcept(ctx.as_if<context_value>()));
    static_assert(noexcept(const_ctx.as_if<context_value>()));
    static_assert(noexcept(ctx.as_base_if<registered_base>()));
    static_assert(noexcept(const_ctx.as_base_if<registered_base>()));

    auto& value = ctx.emplace<context_value>();
    auto& base = ctx.as_base<registered_base>();
    REQUIRE_EQ(&base, static_cast<context_value::base_type*>(&value));
    CHECK_EQ(ctx.as_if<context_value>(), &value);
    CHECK_EQ(ctx.as_if<const context_value>(), &value);
    CHECK_EQ(ctx.as_base_if<registered_base>(), &base);
    CHECK_EQ(ctx.as_base_if<const registered_base>(), &base);
    CHECK_EQ(&ctx.as_base<const registered_base>(), &base);
    base.value = 42;
    CHECK_EQ(const_ctx.as<context_value>().value, 42);
    CHECK_EQ(const_ctx.as_if<context_value>(), &value);
    CHECK_EQ(const_ctx.as_base_if<registered_base>(), &base);
    CHECK_EQ(&const_ctx.as_base<registered_base>(), &base);

    CHECK_EQ(ctx.as_if<registered_base>(), nullptr);
    CHECK_EQ(const_ctx.as_if<registered_base>(), nullptr);
    CHECK_EQ(ctx.as_base_if<context_value>(), nullptr);
    CHECK_EQ(const_ctx.as_base_if<context_value>(), nullptr);
    CHECK_THROWS_AS(static_cast<void>(ctx.as<registered_base>()), std::bad_any_cast);
    CHECK_THROWS_AS(static_cast<void>(const_ctx.as<registered_base>()), std::bad_any_cast);
    CHECK_THROWS_AS(static_cast<void>(ctx.as_base<context_value>()), std::bad_any_cast);
    CHECK_THROWS_AS(static_cast<void>(const_ctx.as_base<context_value>()), std::bad_any_cast);

    SUBCASE("A const context owns its value") {
        const auto frozen = ctx;
        ctx.as_base<registered_base>().value = 43;
        const auto& stored = frozen.as<context_value>();
        CHECK_EQ(frozen.as_if<context_value>(), &stored);
        CHECK_EQ(frozen.as_base_if<registered_base>(),
                 static_cast<const registered_base*>(&stored));
        CHECK_EQ(frozen.as_base<registered_base>().value, 42);
        CHECK_EQ(ctx.as<context_value>().value, 43);
    }

    SUBCASE("A copied context refers to its own value after a move") {
        auto copy = ctx;
        auto moved = std::move(copy);
        auto& moved_value = moved.as<context_value>();
        CHECK_EQ(&moved.as_base<registered_base>(), static_cast<registered_base*>(&moved_value));
        CHECK_NE(&moved.as_base<registered_base>(), &base);
    }

    SUBCASE("Replacing the value registers the new base") {
        struct replacement : context_value {
            using base_type = context_value;
        };

        auto& next = ctx.emplace<replacement>();
        CHECK_EQ(&next, &ctx.as<replacement>());
        CHECK_EQ(&ctx.as_base<context_value>(), static_cast<replacement::base_type*>(&next));
        CHECK_EQ(ctx.as_base_if<registered_base>(), nullptr);
        CHECK_THROWS_AS(static_cast<void>(ctx.as_base<registered_base>()), std::bad_any_cast);
    }

    SUBCASE("A replacement can be constructed from the previous base") {
        struct snapshot : registered_base {
            using base_type = registered_base;

            explicit snapshot(const base_type& previous) noexcept
                : base_type(previous) {}
        };

        auto& next = ctx.emplace<snapshot>(base);
        REQUIRE_EQ(&next, &ctx.as<snapshot>());
        CHECK_EQ(next.value, 42);
    }

    SUBCASE("A failed emplacement preserves the previous state") {
        struct throwing_value : context_value {
            using base_type = context_value;

            throwing_value() {
                throw std::runtime_error("construction failed");
            }
        };

        CHECK_THROWS_AS(ctx.emplace<throwing_value>(), std::runtime_error);
        REQUIRE_EQ(&ctx.as<context_value>(), &value);
        REQUIRE_EQ(&ctx.as_base<registered_base>(), &base);
        CHECK_EQ(base.value, 42);
        CHECK_EQ(ctx.as_base_if<throwing_value::base_type>(), nullptr);
        CHECK_THROWS_AS(static_cast<void>(ctx.as_base<throwing_value::base_type>()),
                        std::bad_any_cast);
        base.value = 43;
        CHECK_EQ(ctx.as<context_value>().value, 43);

        fawkes::request_context empty;
        CHECK_THROWS_AS(empty.emplace<throwing_value>(), std::runtime_error);
        CHECK_EQ(empty.as_if<throwing_value>(), nullptr);
        CHECK_EQ(empty.as_base_if<throwing_value::base_type>(), nullptr);
        CHECK_THROWS_AS(static_cast<void>(empty.as<throwing_value>()), std::bad_any_cast);
        CHECK_THROWS_AS(static_cast<void>(empty.as_base<throwing_value::base_type>()),
                        std::bad_any_cast);
        auto& retried = empty.emplace<context_value>();
        CHECK_EQ(&empty.as_base<registered_base>(), static_cast<registered_base*>(&retried));
    }
}

TEST_SUITE_END(); // Request Context

} // namespace
