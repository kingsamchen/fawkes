// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include <tuple>

#include <doctest/doctest.h>
#include <esl/ignore_unused.h>

#include "fawkes/middleware.hpp"
#include "fawkes/request.hpp"
#include "fawkes/response.hpp"
#include "fawkes/router.hpp"

namespace {

TEST_SUITE_BEGIN("Routes");

TEST_CASE("Concept is_user_handler") {
    auto h = [](const fawkes::request& /*req*/, fawkes::response& /*resp*/) {
    };
    static_assert(fawkes::is_user_handler<decltype(h)>);

    SUBCASE("user handler must use const request&") {
        auto hd = [](fawkes::request& /*req*/, fawkes::response& /*resp*/) {
        };
        static_assert(!fawkes::is_user_handler<decltype(hd)>);
    }
}

TEST_SUITE_END();

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

    static_assert(fawkes::is_middleware<pre_handle_only>);
    static_assert(fawkes::is_middleware<post_handle_only>);
    static_assert(fawkes::is_middleware<pre_and_post_handle>);
    static_assert(fawkes::is_middleware<const_handles>);

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

struct m_count_pre_t {
    int* pre_cnt{nullptr};

    fawkes::middleware_result pre_handle(fawkes::request& /*req*/, fawkes::response& /*resp*/) {
        esl::ignore_unused(this);
        ++(*pre_cnt);
        return fawkes::middleware_result::proceed;
    }
};

struct m_count_post_t {
    int* post_cnt{nullptr};

    fawkes::middleware_result post_handle(fawkes::request& /*req*/, fawkes::response& /*resp*/) {
        esl::ignore_unused(this);
        ++(*post_cnt);
        return fawkes::middleware_result::proceed;
    }
};

struct m_count_both_t {
    int* pre_cnt{nullptr};
    int* post_cnt{nullptr};

    fawkes::middleware_result pre_handle(fawkes::request& /*req*/, fawkes::response& /*resp*/) {
        esl::ignore_unused(this);
        ++(*pre_cnt);
        return fawkes::middleware_result::proceed;
    }

    fawkes::middleware_result post_handle(fawkes::request& /*req*/, fawkes::response& /*resp*/) {
        esl::ignore_unused(this);
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

TEST_CASE("Middleware_chain with both pre/post handle") {
    fawkes::middleware_chain mc;
    int pre_cnt = 0;
    int post_cnt = 0;
    mc.set(fawkes::middlewares::use(m_count_pre_t{&pre_cnt},
                                    m_count_post_t{&post_cnt},
                                    m_count_both_t{.pre_cnt = &pre_cnt, .post_cnt = &post_cnt}));
    fawkes::request req;
    fawkes::response resp;
    auto ret = mc.pre_handle(req, resp);
    REQUIRE_EQ(ret, fawkes::middleware_result::proceed);
    CHECK_EQ(pre_cnt, 2);
    CHECK_EQ(post_cnt, 0);

    ret = mc.post_handle(req, resp);
    REQUIRE_EQ(ret, fawkes::middleware_result::proceed);
    CHECK_EQ(pre_cnt, 2);
    CHECK_EQ(post_cnt, 2);
}

TEST_CASE("Middleware_chain with only pre handle") {
    fawkes::middleware_chain mc;
    int pre_cnt = 0;
    const int post_cnt = 0;
    mc.set(fawkes::middlewares::use(m_count_pre_t{&pre_cnt},
                                    m_count_pre_t{&pre_cnt},
                                    m_count_pre_t{&pre_cnt}));

    fawkes::request req;
    fawkes::response resp;
    auto ret = mc.pre_handle(req, resp);
    REQUIRE_EQ(ret, fawkes::middleware_result::proceed);
    CHECK_EQ(pre_cnt, 3);
    CHECK_EQ(post_cnt, 0);

    ret = mc.post_handle(req, resp);
    REQUIRE_EQ(ret, fawkes::middleware_result::proceed);
    CHECK_EQ(pre_cnt, 3);
    CHECK_EQ(post_cnt, 0);
}

TEST_CASE("Middleware_chain with only post handle") {
    fawkes::middleware_chain mc;
    const int pre_cnt = 0;
    int post_cnt = 0;
    mc.set(fawkes::middlewares::use(m_count_post_t{&post_cnt},
                                    m_count_post_t{&post_cnt},
                                    m_count_post_t{&post_cnt}));

    fawkes::request req;
    fawkes::response resp;
    auto ret = mc.pre_handle(req, resp);
    REQUIRE_EQ(ret, fawkes::middleware_result::proceed);
    CHECK_EQ(pre_cnt, 0);
    CHECK_EQ(post_cnt, 0);

    ret = mc.post_handle(req, resp);
    REQUIRE_EQ(ret, fawkes::middleware_result::proceed);
    CHECK_EQ(pre_cnt, 0);
    CHECK_EQ(post_cnt, 3);
}

TEST_CASE("Missing pre handle in the middle") {
    fawkes::middleware_chain mc;
    int pre_cnt = 0;
    int post_cnt = 0;
    mc.set(fawkes::middlewares::use(m_count_pre_t{&pre_cnt},
                                    m_count_post_t{&post_cnt},
                                    m_count_pre_t{&pre_cnt}));
    fawkes::request req;
    fawkes::response resp;
    auto ret = mc.pre_handle(req, resp);
    REQUIRE_EQ(ret, fawkes::middleware_result::proceed);
    CHECK_EQ(pre_cnt, 2);
    CHECK_EQ(post_cnt, 0);

    ret = mc.post_handle(req, resp);
    REQUIRE_EQ(ret, fawkes::middleware_result::proceed);
    CHECK_EQ(pre_cnt, 2);
    CHECK_EQ(post_cnt, 1);
}

TEST_CASE("Missing post handle in the middle") {
    fawkes::middleware_chain mc;
    int pre_cnt = 0;
    int post_cnt = 0;
    mc.set(fawkes::middlewares::use(m_count_post_t{&post_cnt},
                                    m_count_pre_t{&pre_cnt},
                                    m_count_post_t{&post_cnt}));
    fawkes::request req;
    fawkes::response resp;
    auto ret = mc.pre_handle(req, resp);
    REQUIRE_EQ(ret, fawkes::middleware_result::proceed);
    CHECK_EQ(pre_cnt, 1);
    CHECK_EQ(post_cnt, 0);

    ret = mc.post_handle(req, resp);
    REQUIRE_EQ(ret, fawkes::middleware_result::proceed);
    CHECK_EQ(pre_cnt, 1);
    CHECK_EQ(post_cnt, 2);
}

TEST_CASE("Abort from pre handle") {
    fawkes::middleware_chain mc;
    int pre_cnt = 0;
    mc.set(fawkes::middlewares::use(m_count_pre_t{&pre_cnt},
                                    m_abort_pre_t{},
                                    m_count_pre_t{&pre_cnt}));

    fawkes::request req;
    fawkes::response resp;
    auto ret = mc.pre_handle(req, resp);
    REQUIRE_EQ(ret, fawkes::middleware_result::abort);
    CHECK_EQ(pre_cnt, 1);
}

TEST_CASE("Abort from post handle") {
    fawkes::middleware_chain mc;
    int post_cnt = 0;
    mc.set(fawkes::middlewares::use(m_abort_post_t{},
                                    m_count_post_t{&post_cnt},
                                    m_count_post_t{&post_cnt}));

    fawkes::request req;
    fawkes::response resp;
    auto ret = mc.post_handle(req, resp);
    REQUIRE_EQ(ret, fawkes::middleware_result::abort);
    CHECK_EQ(post_cnt, 2);
}

TEST_CASE("No-op for empty middleware chain") {
    fawkes::middleware_chain mc;
    fawkes::request req;
    fawkes::response resp;
    CHECK_EQ(mc.pre_handle(req, resp), fawkes::middleware_result::proceed);
    CHECK_EQ(mc.post_handle(req, resp), fawkes::middleware_result::proceed);
}

TEST_CASE("Skip no middleware") {
    fawkes::request req;
    fawkes::response resp;
    auto t = std::make_tuple();
    CHECK_EQ(fawkes::detail::run_middlewares_pre_handle(t, req, resp),
             fawkes::middleware_result::proceed);
    CHECK_EQ(fawkes::detail::run_middlewares_post_handle(t, req, resp),
             fawkes::middleware_result::proceed);
}

TEST_SUITE_END();

} // namespace
