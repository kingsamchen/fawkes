// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/core/ignore_unused.hpp>
#include <doctest/doctest.h>

#include "fawkes/middleware.hpp"
#include "fawkes/request.hpp"
#include "fawkes/response.hpp"
#include "fawkes/router.hpp"
#include "test_utils/run_awaitable_sync.hpp"

namespace {

namespace asio = boost::asio;
namespace http = boost::beast::http;

fawkes::request make_request(http::verb method, std::string_view target) {
    fawkes::request::impl_type raw;
    raw.method(method);
    raw.target(target);
    return fawkes::request(std::move(raw));
}

TEST_SUITE_BEGIN("Routes");

TEST_CASE("Type trait is_asio_awaitable") {
    static_assert(fawkes::is_asio_awaitable_v<asio::awaitable<void>>);
    static_assert(!fawkes::is_asio_awaitable_v<int>);
}

TEST_CASE("Concept is_user_handler") {
    auto h = [](const fawkes::request& /*req*/, fawkes::response& /*resp*/)
        -> asio::awaitable<void> {
        co_return;
    };
    static_assert(fawkes::is_user_handler<decltype(h)>);

    SUBCASE("user handler must use const request&") {
        auto hd = [](fawkes::request& /*req*/, fawkes::response& /*resp*/)
            -> asio::awaitable<void> {
            co_return;
        };
        static_assert(!fawkes::is_user_handler<decltype(hd)>);
    }
}

TEST_SUITE_END(); // Routes

TEST_SUITE_BEGIN("Middleware");

TEST_CASE("Concept is_middleware") {
    struct pre_handle_only {
        fawkes::middleware_result pre_handle(fawkes::request& /*req*/,
                                             fawkes::response& /*resp*/);
    };

    struct post_handle_only {
        fawkes::middleware_result post_handle(fawkes::request& /*req*/,
                                              fawkes::response& /*resp*/);
    };

    struct pre_and_post_handle {
        fawkes::middleware_result pre_handle(fawkes::request& /*req*/,
                                             fawkes::response& /*resp*/);
        fawkes::middleware_result post_handle(fawkes::request& /*req*/,
                                              fawkes::response& /*resp*/);
    };

    struct const_handles {
        fawkes::middleware_result pre_handle(fawkes::request& /*req*/,
                                             fawkes::response& /*resp*/) const;
        fawkes::middleware_result post_handle(fawkes::request& /*req*/,
                                              fawkes::response& /*resp*/) const;
    };

    struct coro_pre_post_handle {
        asio::awaitable<fawkes::middleware_result> pre_handle(fawkes::request& /*req*/,
                                                              fawkes::response& /*resp*/);
        asio::awaitable<fawkes::middleware_result> post_handle(fawkes::request& /*req*/,
                                                               fawkes::response& /*resp*/);
    };

    static_assert(fawkes::is_middleware<pre_handle_only>);
    static_assert(fawkes::is_middleware<post_handle_only>);
    static_assert(fawkes::is_middleware<pre_and_post_handle>);
    static_assert(fawkes::is_middleware<const_handles>);
    static_assert(fawkes::is_middleware<coro_pre_post_handle>);

    SUBCASE("return type mismatched") {
        struct middleware_handle {
            bool pre_handle(fawkes::request& /*req*/, fawkes::response& /*resp*/);
        };

        static_assert(!fawkes::is_middleware<middleware_handle>);
    }

    SUBCASE("argument mismatched") {
        SUBCASE("has only request argument") {
            struct middleware_handle {
                fawkes::middleware_result post_handle(fawkes::request& /*req*/);
            };

            static_assert(!fawkes::is_middleware<middleware_handle>);
        }

        SUBCASE("has only response argument") {
            struct middleware_handle {
                fawkes::middleware_result post_handle(fawkes::response& /*resp*/);
            };

            static_assert(!fawkes::is_middleware<middleware_handle>);
        }
    }
}

//
// WARNING: modifying non-local data in either middleware handler is dangerous, as data is likely
// subject to data-race, casued by concurrent access from multiple connections.
//

struct m_count_pre_t {
    int* pre_cnt{nullptr};

    fawkes::middleware_result pre_handle(fawkes::request& /*req*/,
                                         fawkes::response& /*resp*/) const {
        boost::ignore_unused(this);
        ++(*pre_cnt);
        return fawkes::middleware_result::proceed;
    }
};

struct m_count_post_t {
    int* post_cnt{nullptr};

    fawkes::middleware_result post_handle(fawkes::request& /*req*/,
                                          fawkes::response& /*resp*/) const {
        boost::ignore_unused(this);
        ++(*post_cnt);
        return fawkes::middleware_result::proceed;
    }
};

struct m_count_both_t {
    int* pre_cnt{nullptr};
    int* post_cnt{nullptr};

    fawkes::middleware_result pre_handle(fawkes::request& /*req*/,
                                         fawkes::response& /*resp*/) const {
        boost::ignore_unused(this);
        ++(*pre_cnt);
        return fawkes::middleware_result::proceed;
    }

    fawkes::middleware_result post_handle(fawkes::request& /*req*/,
                                          fawkes::response& /*resp*/) const {
        boost::ignore_unused(this);
        ++(*post_cnt);
        return fawkes::middleware_result::proceed;
    }
};

struct m_abort_pre_t {
    static fawkes::middleware_result pre_handle(fawkes::request& /*req*/,
                                                fawkes::response& /*resp*/) {
        return fawkes::middleware_result::abort;
    }
};

struct m_abort_post_t {
    static fawkes::middleware_result post_handle(fawkes::request& /*req*/,
                                                 fawkes::response& /*resp*/) {
        return fawkes::middleware_result::abort;
    }
};

struct m_coro_abort_t {
    static asio::awaitable<fawkes::middleware_result> pre_handle(fawkes::request& /*req*/,
                                                                 fawkes::response& /*resp*/) {
        co_return fawkes::middleware_result::abort;
    }
};

struct m_coro_append_t {
    std::string append_str;

    asio::awaitable<fawkes::middleware_result> pre_handle(fawkes::request& /*req*/,
                                                          fawkes::response& resp) const {
        resp.body() += append_str;
        co_return fawkes::middleware_result::proceed;
    }
};

fawkes::route_handler_t make_route_handler() {
    return [](fawkes::request& /*req*/, fawkes::response& /*resp*/)
               -> asio::awaitable<fawkes::middleware_result> {
        co_return fawkes::middleware_result::proceed;
    };
}

fawkes::middleware_result run_middleware_chain(asio::io_context& ioc,
                                               const fawkes::middleware_chain& chain,
                                               fawkes::request& req,
                                               fawkes::response& resp) {
    const auto inner_handler = make_route_handler();
    return test_util::run_awaitable_sync(ioc, chain.run(req, resp, inner_handler));
}

struct m_trace_t {
    std::string name;
    std::vector<std::string>* trace{nullptr};
    fawkes::middleware_result pre_result{fawkes::middleware_result::proceed};
    bool throw_from_pre{false};
    bool throw_from_post{false};

    fawkes::middleware_result pre_handle(fawkes::request& /*req*/,
                                         fawkes::response& /*resp*/) const {
        trace->push_back(name + ".pre");
        if (throw_from_pre) {
            throw std::runtime_error(name);
        }
        return pre_result;
    }

    fawkes::middleware_result post_handle(fawkes::request& /*req*/,
                                          fawkes::response& /*resp*/) const {
        trace->push_back(name + ".post");
        if (throw_from_post) {
            throw std::runtime_error(name);
        }
        return fawkes::middleware_result::proceed;
    }
};

// All middlewares pre_handle
//   -> inner handler
//        -> all middlewares post_handle in reversed order.
TEST_CASE("Middleware chain runs in onion order") {
    asio::io_context ioc;
    std::vector<std::string> trace;
    const auto middleware_tuple =
        fawkes::middlewares::use(m_trace_t{.name = "A", .trace = &trace},
                                 m_trace_t{.name = "B", .trace = &trace},
                                 m_trace_t{.name = "C", .trace = &trace});
    // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
    const auto inner_handler = [&trace](fawkes::request& /*req*/, fawkes::response& /*resp*/)
        -> asio::awaitable<fawkes::middleware_result> {
        trace.emplace_back("handler");
        co_return fawkes::middleware_result::proceed;
    };
    fawkes::request req;
    fawkes::response resp;

    const auto result = test_util::run_awaitable_sync(
        ioc, fawkes::detail::run_middlewares(middleware_tuple, req, resp, inner_handler));

    CHECK_EQ(result, fawkes::middleware_result::proceed);
    const std::vector<std::string> expected_trace{
        "A.pre", "B.pre", "C.pre", "handler", "C.post", "B.post", "A.post"};
    CHECK_EQ(trace, expected_trace);
}

TEST_CASE("Middleware_chain with both pre/post handle") {
    asio::io_context ioc;

    fawkes::middleware_chain mc;
    int pre_cnt = 0;
    int post_cnt = 0;
    mc.set(fawkes::middlewares::use(m_count_pre_t{&pre_cnt},
                                    m_count_post_t{&post_cnt},
                                    m_count_both_t{.pre_cnt = &pre_cnt, .post_cnt = &post_cnt}));
    fawkes::request req;
    fawkes::response resp;
    const auto ret = run_middleware_chain(ioc, mc, req, resp);
    REQUIRE_EQ(ret, fawkes::middleware_result::proceed);
    CHECK_EQ(pre_cnt, 2);
    CHECK_EQ(post_cnt, 2);
}

TEST_CASE("Middleware_chain with only pre handle") {
    asio::io_context ioc;

    fawkes::middleware_chain mc;
    int pre_cnt = 0;
    const int post_cnt = 0;
    mc.set(fawkes::middlewares::use(m_count_pre_t{&pre_cnt},
                                    m_count_pre_t{&pre_cnt},
                                    m_count_pre_t{&pre_cnt}));

    fawkes::request req;
    fawkes::response resp;
    const auto ret = run_middleware_chain(ioc, mc, req, resp);
    REQUIRE_EQ(ret, fawkes::middleware_result::proceed);
    CHECK_EQ(pre_cnt, 3);
    CHECK_EQ(post_cnt, 0);
}

TEST_CASE("Middleware_chain with only post handle") {
    asio::io_context ioc;

    fawkes::middleware_chain mc;
    const int pre_cnt = 0;
    int post_cnt = 0;
    mc.set(fawkes::middlewares::use(m_count_post_t{&post_cnt},
                                    m_count_post_t{&post_cnt},
                                    m_count_post_t{&post_cnt}));

    fawkes::request req;
    fawkes::response resp;
    const auto ret = run_middleware_chain(ioc, mc, req, resp);
    REQUIRE_EQ(ret, fawkes::middleware_result::proceed);
    CHECK_EQ(pre_cnt, 0);
    CHECK_EQ(post_cnt, 3);
}

TEST_CASE("Missing pre handle in the middle") {
    asio::io_context ioc;

    fawkes::middleware_chain mc;
    int pre_cnt = 0;
    int post_cnt = 0;
    mc.set(fawkes::middlewares::use(m_count_pre_t{&pre_cnt},
                                    m_count_post_t{&post_cnt},
                                    m_count_pre_t{&pre_cnt}));
    fawkes::request req;
    fawkes::response resp;
    const auto ret = run_middleware_chain(ioc, mc, req, resp);
    REQUIRE_EQ(ret, fawkes::middleware_result::proceed);
    CHECK_EQ(pre_cnt, 2);
    CHECK_EQ(post_cnt, 1);
}

TEST_CASE("Missing post handle in the middle") {
    asio::io_context ioc;

    fawkes::middleware_chain mc;
    int pre_cnt = 0;
    int post_cnt = 0;
    mc.set(fawkes::middlewares::use(m_count_post_t{&post_cnt},
                                    m_count_pre_t{&pre_cnt},
                                    m_count_post_t{&post_cnt}));
    fawkes::request req;
    fawkes::response resp;
    const auto ret = run_middleware_chain(ioc, mc, req, resp);
    REQUIRE_EQ(ret, fawkes::middleware_result::proceed);
    CHECK_EQ(pre_cnt, 1);
    CHECK_EQ(post_cnt, 2);
}

TEST_CASE("Abort from pre handle") {
    asio::io_context ioc;

    fawkes::middleware_chain mc;
    int pre_cnt = 0;
    int post_cnt = 0;
    mc.set(fawkes::middlewares::use(m_count_pre_t{&pre_cnt},
                                    m_count_post_t{&post_cnt},
                                    m_abort_pre_t{},
                                    m_count_post_t{&post_cnt}));

    fawkes::request req;
    fawkes::response resp;
    const auto ret = run_middleware_chain(ioc, mc, req, resp);
    REQUIRE_EQ(ret, fawkes::middleware_result::abort);
    CHECK_EQ(pre_cnt, 1);
    CHECK_EQ(post_cnt, 1);
}

TEST_CASE("Abort from post handle") {
    asio::io_context ioc;

    fawkes::middleware_chain mc;
    int post_cnt = 0;
    mc.set(fawkes::middlewares::use(m_count_post_t{&post_cnt},
                                    m_abort_post_t{},
                                    m_count_post_t{&post_cnt}));

    fawkes::request req;
    fawkes::response resp;
    const auto ret = run_middleware_chain(ioc, mc, req, resp);
    REQUIRE_EQ(ret, fawkes::middleware_result::abort);
    CHECK_EQ(post_cnt, 2);
}

// Post middleware throws -> remaining entered middleware still unwind.
TEST_CASE("Post exception continues middleware unwind") {
    asio::io_context ioc;
    std::vector<std::string> trace;
    fawkes::middleware_chain mc;
    mc.set(fawkes::middlewares::use(
        m_trace_t{.name = "A", .trace = &trace},
        m_trace_t{.name = "B", .trace = &trace, .throw_from_post = true},
        m_trace_t{.name = "C", .trace = &trace}));
    fawkes::request req;
    fawkes::response resp;

    CHECK_THROWS_AS(run_middleware_chain(ioc, mc, req, resp), std::runtime_error);
    CHECK_EQ(resp.status(), http::status::internal_server_error);
    const std::vector<std::string> expected_trace{
        "A.pre", "B.pre", "C.pre", "C.post", "B.post", "A.post"};
    CHECK_EQ(trace, expected_trace);
}

TEST_CASE("No-op for empty middleware chain") {
    asio::io_context ioc;

    const fawkes::middleware_chain mc;
    fawkes::request req;
    fawkes::response resp;
    CHECK_EQ(run_middleware_chain(ioc, mc, req, resp), fawkes::middleware_result::proceed);
}

TEST_CASE("Skip no middleware") {
    asio::io_context ioc;

    fawkes::request req;
    fawkes::response resp;
    auto t = std::make_tuple();
    const auto inner_handler = make_route_handler();
    auto result = fawkes::detail::run_middlewares(t, req, resp, inner_handler);
    CHECK_EQ(test_util::run_awaitable_sync(ioc, std::move(result)),
             fawkes::middleware_result::proceed);
}

TEST_CASE("Coroutine middlewares are invoked sequentially") {
    asio::io_context ioc;

    fawkes::middleware_chain mc;
    mc.set(fawkes::middlewares::use(m_coro_append_t{"A"},
                                    m_coro_append_t{"B"},
                                    m_coro_append_t{"C"}));

    fawkes::request req;
    fawkes::response resp;
    const auto ret = run_middleware_chain(ioc, mc, req, resp);
    REQUIRE_EQ(ret, fawkes::middleware_result::proceed);
    CHECK_EQ(resp.body(), "ABC");
}

TEST_CASE("Mixing coroutine and normal middlewares") {
    asio::io_context ioc;

    fawkes::middleware_chain mc;
    int pre_cnt = 0;
    mc.set(fawkes::middlewares::use(m_count_pre_t{&pre_cnt},
                                    m_coro_append_t{"X"},
                                    m_count_pre_t{&pre_cnt},
                                    m_coro_append_t{"Y"}));

    fawkes::request req;
    fawkes::response resp;
    const auto ret = run_middleware_chain(ioc, mc, req, resp);
    REQUIRE_EQ(ret, fawkes::middleware_result::proceed);
    CHECK_EQ(pre_cnt, 2);
    CHECK_EQ(resp.body(), "XY");
}

TEST_CASE("Abort coroutine middleware after normal middleware") {
    asio::io_context ioc;

    fawkes::middleware_chain mc;
    int pre_cnt = 0;
    mc.set(fawkes::middlewares::use(m_count_pre_t{&pre_cnt},
                                    m_coro_abort_t{},
                                    m_count_pre_t{&pre_cnt}));

    fawkes::request req;
    fawkes::response resp;
    const auto ret = run_middleware_chain(ioc, mc, req, resp);
    REQUIRE_EQ(ret, fawkes::middleware_result::abort);
    CHECK_EQ(pre_cnt, 1);
}

TEST_CASE("Middleware abort unwinds all prior but skips inner") {
    asio::io_context ioc;
    std::vector<std::string> trace;
    fawkes::router router;
    router.use(m_trace_t{.name = "G1", .trace = &trace},
               m_trace_t{.name = "G2", .trace = &trace});
    router.add_route(
        http::verb::get,
        "/items",
        fawkes::middlewares::use(
            m_trace_t{.name = "R1", .trace = &trace},
            m_trace_t{.name = "R2",
                      .trace = &trace,
                      .pre_result = fawkes::middleware_result::abort},
            m_trace_t{.name = "R3", .trace = &trace}),
        // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
        [&trace](const fawkes::request& /*req*/,
                 fawkes::response& /*resp*/) -> asio::awaitable<void> {
            trace.emplace_back("handler");
            co_return;
        });
    auto req = make_request(http::verb::get, "/items");
    fawkes::response resp;

    test_util::run_awaitable_sync(ioc, router.dispatch(req, resp));
    // Abort is also a kind of completion.
    const std::vector<std::string> expected_trace{"G1.pre",
                                                  "G2.pre",
                                                  "R1.pre",
                                                  "R2.pre",
                                                  "R2.post",
                                                  "R1.post",
                                                  "G2.post",
                                                  "G1.post"};
    CHECK_EQ(trace, expected_trace);
}

TEST_CASE("Middleware pre exception skips post and inner but unwinds prior") {
    asio::io_context ioc;
    std::vector<std::string> trace;
    fawkes::router router;
    router.use(m_trace_t{.name = "G1", .trace = &trace},
               m_trace_t{.name = "G2", .trace = &trace});
    router.add_route(
        http::verb::get,
        "/items",
        fawkes::middlewares::use(
            m_trace_t{.name = "R1", .trace = &trace},
            m_trace_t{.name = "R2", .trace = &trace, .throw_from_pre = true},
            m_trace_t{.name = "R3", .trace = &trace}),
        // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
        [&trace](const fawkes::request& /*req*/,
                 fawkes::response& /*resp*/) -> asio::awaitable<void> {
            trace.emplace_back("handler");
            co_return;
        });
    auto req = make_request(http::verb::get, "/items");
    fawkes::response resp;

    CHECK_THROWS_AS(test_util::run_awaitable_sync(ioc, router.dispatch(req, resp)),
                    std::runtime_error);
    CHECK_EQ(resp.status(), http::status::internal_server_error);
    const std::vector<std::string> expected_trace{
        "G1.pre", "G2.pre", "R1.pre", "R2.pre", "R1.post", "G2.post", "G1.post"};
    CHECK_EQ(trace, expected_trace);
}

TEST_SUITE_END(); // Middleware

} // namespace
