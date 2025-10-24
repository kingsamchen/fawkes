// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <any>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <functional>
#include <tuple>
#include <utility>

#include <esl/ignore_unused.h>

namespace fawkes {

// TODO(KC): Consider make middlewares immutable.

// Forward declarations for concepts.
class request;
class response;

enum class middleware_result : std::uint8_t {
    abort,
    proceed,
};

template<typename T>
concept has_pre_handle = requires(T&& t, request& req, response& resp) {
    { std::forward<T>(t).pre_handle(req, resp) } -> std::same_as<middleware_result>;
};

template<typename T>
concept has_post_handle = requires(T&& t, request& req, response& resp) {
    { std::forward<T>(t).post_handle(req, resp) } -> std::same_as<middleware_result>;
};

template<typename T>
concept is_middleware = has_pre_handle<T> || has_post_handle<T>;

namespace detail {

template<bool IsForward, std::size_t... I, is_middleware... Mws, typename F>
middleware_result apply_middlewares_impl(std::tuple<Mws...>& middlewares,
                                         std::index_sequence<I...> idx_seq,
                                         request& req,
                                         response& resp,
                                         F&& fn) {
    constexpr auto N = idx_seq.size();
    auto result = middleware_result::proceed;
    // Short circuit if possible.
    auto&& f = std::forward<F>(fn);
    if constexpr (IsForward) {
        esl::ignore_unused((f(std::get<I>(middlewares), req, resp, result) && ...));
    } else {
        esl::ignore_unused((f(std::get<N - I - 1>(middlewares), req, resp, result) && ...));
    }
    return result;
}

template<bool IsForward, is_middleware... Mws, typename F>
middleware_result apply_middlewares(std::tuple<Mws...>& middlewares,
                                    request& req,
                                    response& resp,
                                    F&& fn) {
    using idx_seq_t = std::make_index_sequence<std::tuple_size_v<std::tuple<Mws...>>>;
    return apply_middlewares_impl<IsForward>(
        middlewares, idx_seq_t{}, req, resp, std::forward<F>(fn));
}

template<is_middleware... Mws>
middleware_result run_middlewares_pre_handle(std::tuple<Mws...>& middlewares,
                                             request& req,
                                             response& resp) {
    if constexpr (sizeof...(Mws) == 0) {
        return middleware_result::proceed;
    }

    return apply_middlewares<true>(
        middlewares,
        req,
        resp,
        []<is_middleware M>(M& middleware,
                            request& mw_req,
                            response& mw_resp,
                            middleware_result& ret) -> bool {
            if constexpr (has_pre_handle<M>) {
                ret = middleware.pre_handle(mw_req, mw_resp);
                return ret == middleware_result::proceed;
            }
            return true;
        });
}

template<is_middleware... Mws>
middleware_result run_middlewares_post_handle(std::tuple<Mws...>& middlewares,
                                              request& req,
                                              response& resp) {
    if constexpr (sizeof...(Mws) == 0) {
        return middleware_result::proceed;
    }

    return detail::apply_middlewares<false>(
        middlewares,
        req,
        resp,
        []<is_middleware M>(M& middleware,
                            request& mw_req,
                            response& mw_resp,
                            middleware_result& ret) -> bool {
            if constexpr (has_post_handle<M>) {
                ret = middleware.post_handle(mw_req, mw_resp);
                return ret == middleware_result::proceed;
            }
            return true;
        });
}

} // namespace detail

// A type-erased middleware set.
class middleware_chain {
public:
    template<is_middleware... Mws>
    void set(std::tuple<Mws...>&& middlewares) {
        using middlewares_t = std::tuple<Mws...>;
        static_assert(std::tuple_size_v<middlewares_t> > 0, "middlewares cannot be empty");

        middlewares_ = std::move(middlewares);
        void* const ptr = std::any_cast<middlewares_t>(&middlewares_);
        assert(ptr != nullptr);
        auto& mws = *static_cast<middlewares_t*>(ptr);

        pre_impl_ = [&mws](request& req, response& resp) -> middleware_result {
            return detail::run_middlewares_pre_handle(mws, req, resp);
        };

        post_impl_ = [&mws](request& req, response& resp) -> middleware_result {
            return detail::run_middlewares_post_handle(mws, req, resp);
        };
    }

    middleware_result pre_handle(request& req, response& resp) {
        if (!pre_impl_) {
            return middleware_result::proceed;
        }

        return pre_impl_(req, resp);
    }

    middleware_result post_handle(request& req, response& resp) {
        if (!post_impl_) {
            return middleware_result::proceed;
        }

        return post_impl_(req, resp);
    }

private:
    std::any middlewares_;
    std::function<middleware_result(request&, response&)> pre_impl_;
    std::function<middleware_result(request&, response&)> post_impl_;
};

struct middlewares {
    template<is_middleware... Mws>
    static auto use(Mws... mws) -> std::tuple<Mws...> {
        return std::make_tuple(std::move(mws)...);
    }
};

} // namespace fawkes
