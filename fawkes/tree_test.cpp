// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include <string>
#include <utility>
#include <vector>

#include <doctest/doctest.h>
#include <fmt/format.h>

#include "fawkes/tree.hpp"
#include "test_utils/stringification.hpp" // IWYU pragma: keep

namespace fawkes {

class node_test_inspector {
public:
    explicit node_test_inspector(const node& node)
        : node_(node) {}

    ~node_test_inspector() = default;

    node_test_inspector(const node_test_inspector&) = delete;
    node_test_inspector(node_test_inspector&&) = delete;
    node_test_inspector& operator=(const node_test_inspector&) = delete;
    node_test_inspector& operator=(node_test_inspector&&) = delete;

    int check_priority() const { // NOLINT(modernize-use-nodiscard)
        int prio = 0;
        if (node_.handler_) {
            ++prio;
        }

        for (const auto& child : node_.children_) {
            auto inspector = node_test_inspector(*child);
            prio += inspector.check_priority();
        }

        if (prio != node_.priority_) {
            throw std::runtime_error(
                    fmt::format("Priority of node mismatch; path={}\n EXPECT={} ACTUAL={}",
                                node_.path_, prio, node_.priority_));
        }

        return prio;
    }

private:
    const node& node_;
};

} // namespace fawkes

template<>
struct fmt::formatter<fawkes::param> {
    constexpr auto parse(auto& ctx) {
        return ctx.begin();
    }

    auto format(const fawkes::param& p, auto& ctx) const {
        return fmt::format_to(ctx.out(), "(key={}, value={})", p.key, p.value);
    }
};

namespace {

auto fake_handler() {
    return [](const fawkes::request&, fawkes::response&) {
    };
}

struct locate_request {
    std::string test_path;
    bool handler_found;
    std::string hit_route;
    std::vector<fawkes::param> params;

    locate_request(std::string_view test,
                   bool found)
        : test_path(test), handler_found(found) {}

    locate_request(std::string_view test,
                   bool found,
                   std::string_view route,
                   std::vector<fawkes::param> ps)
        : test_path(test), handler_found(found), hit_route(route), params(std::move(ps)) {}
};

TEST_SUITE_BEGIN("Router");

TEST_CASE("Find wildcard in path") {
    SUBCASE("no wildcard") {
        constexpr auto result = fawkes::detail::find_wildcard("/hello/name");
        static_assert(!result.found());
        static_assert(!result.valid_name());
    }

    SUBCASE("wildcard is last segment") {
        constexpr auto param = fawkes::detail::find_wildcard("/hello/:name");
        static_assert(param.found());
        static_assert(param.valid_name());
        static_assert(param.pos == 7);
        static_assert(param.name == ":name");

        constexpr auto catch_all = fawkes::detail::find_wildcard("/hello/*name");
        static_assert(catch_all.found());
        static_assert(catch_all.valid_name());
        static_assert(catch_all.pos == 7);
        static_assert(catch_all.name == "*name");
    }

    SUBCASE("wildcard is in the middle") {
        // find the first wildcard.
        constexpr auto param = fawkes::detail::find_wildcard("/hello/:name/:age");
        static_assert(param.found());
        static_assert(param.valid_name());
        static_assert(param.pos == 7);
        static_assert(param.name == ":name");
    }

    SUBCASE("found wildcard but invalid wildcard name") {
        {
            constexpr auto r = fawkes::detail::find_wildcard("/hello/:na:me");
            static_assert(r.found());
            static_assert(!r.valid_name());
        }

        {
            constexpr auto r = fawkes::detail::find_wildcard("/hello/:na*me");
            static_assert(r.found());
            static_assert(!r.valid_name());
        }

        {
            constexpr auto r = fawkes::detail::find_wildcard("/hello/*na:me");
            static_assert(r.found());
            static_assert(!r.valid_name());
        }

        {
            constexpr auto r = fawkes::detail::find_wildcard("/hello/*na*me");
            static_assert(r.found());
            static_assert(!r.valid_name());
        }
    }

    SUBCASE("empty wildcard name is also invalid") {
        {
            constexpr auto r = fawkes::detail::find_wildcard("/hello:");
            static_assert(r.found());
            static_assert(!r.valid_name());
        }

        {
            constexpr auto r = fawkes::detail::find_wildcard("/hello:/");
            static_assert(r.found());
            static_assert(!r.valid_name());
        }

        {
            constexpr auto r = fawkes::detail::find_wildcard("/hello/:/");
            static_assert(r.found());
            static_assert(!r.valid_name());
        }

        {
            constexpr auto r = fawkes::detail::find_wildcard("/hello/*/");
            static_assert(r.found());
            static_assert(!r.valid_name());
        }

        {
            constexpr auto r = fawkes::detail::find_wildcard("/src/*");
            static_assert(r.found());
            static_assert(!r.valid_name());
        }
    }
}

TEST_CASE("Longest common prefix") {
    SUBCASE("s1 is sub-string of s2") {
        static_assert(fawkes::detail::longest_common_prefix("abc", "abcdef") == 3);
    }

    SUBCASE("s2 is sub-string of s1") {
        static_assert(fawkes::detail::longest_common_prefix("abcdef", "abc") == 3);
    }

    SUBCASE("not substring but have common prefix") {
        static_assert(fawkes::detail::longest_common_prefix("foobar", "foobaz") == 5);
    }

    SUBCASE("one of str is empty") {
        static_assert(fawkes::detail::longest_common_prefix("", "foobar") == 0);
        static_assert(fawkes::detail::longest_common_prefix("foobar", "") == 0);
    }

    SUBCASE("have none common prefix") {
        static_assert(fawkes::detail::longest_common_prefix("hello", "foobar") == 0);
    }
}

//
// Part 1: build tree
//

TEST_CASE("Only one wildcard per path segment is allowed") {
    fawkes::node tree;

    SUBCASE("case-1") {
        CHECK_THROWS_AS(tree.add_route("/:foo:bar", fake_handler()), std::invalid_argument);
    }

    SUBCASE("case-2") {
        CHECK_THROWS_AS(tree.add_route("/:foo:bar/", fake_handler()), std::invalid_argument);
    }

    SUBCASE("case-3") {
        CHECK_THROWS_AS(tree.add_route("/:foo*bar", fake_handler()), std::invalid_argument);
    }

    SUBCASE("case-4") {
        CHECK_THROWS_AS(tree.add_route("/:foo*bar/", fake_handler()), std::invalid_argument);
    }
}

TEST_CASE("Path conflicts with whildcard") {
    fawkes::node tree;

    SUBCASE("case-1") {
        tree.add_route("/cmd/:tool/:sub", fake_handler());
        CHECK_THROWS_AS(tree.add_route("/cmd/vet", fake_handler()), std::invalid_argument);
    }

    SUBCASE("case-2") {
        tree.add_route("/search/:query", fake_handler());
        CHECK_THROWS_AS(tree.add_route("/search/invalid", fake_handler()),
                        std::invalid_argument);
    }

    SUBCASE("case-3") {
        tree.add_route("/user_:name", fake_handler());
        CHECK_THROWS_AS(tree.add_route("/user_x", fake_handler()), std::invalid_argument);
    }

    SUBCASE("case-4") {
        tree.add_route("/id:id", fake_handler());
        CHECK_THROWS_AS(tree.add_route("/id/:id", fake_handler()), std::invalid_argument);
    }

    SUBCASE("case-5") {
        tree.add_route("/con:tact", fake_handler());
        CHECK_THROWS_AS(tree.add_route("/conxxx", fake_handler()),
                        std::invalid_argument);
        CHECK_THROWS_AS(tree.add_route("/conooo/xxx", fake_handler()),
                        std::invalid_argument);
    }

    SUBCASE("case-6") {
        tree.add_route("/src/*filepath", fake_handler());
        CHECK_THROWS_AS(tree.add_route("/src/*filepathx", fake_handler()),
                        std::invalid_argument);
        CHECK_THROWS_AS(tree.add_route("/src/", fake_handler()),
                        std::invalid_argument);
    }

    SUBCASE("case-7") {
        tree.add_route("/src1/", fake_handler());
        CHECK_THROWS_AS(tree.add_route("/src1/*filepath", fake_handler()),
                        std::invalid_argument);
        CHECK_THROWS_AS(tree.add_route("/src2*filepath", fake_handler()),
                        std::invalid_argument);
    }

    SUBCASE("case-8") {
        tree.add_route("/who/are/*you", fake_handler());
        CHECK_THROWS_AS(tree.add_route("/who/are/foo", fake_handler()),
                        std::invalid_argument);
        CHECK_THROWS_AS(tree.add_route("/who/are/foo/", fake_handler()),
                        std::invalid_argument);
        CHECK_THROWS_AS(tree.add_route("/who/are/foo/bar", fake_handler()),
                        std::invalid_argument);
    }

    SUBCASE("no conflicts") {
        CHECK_NOTHROW(tree.add_route("/cmd/:tool/:sub", fake_handler()));
        CHECK_NOTHROW(tree.add_route("/search/:query", fake_handler()));
        CHECK_NOTHROW(tree.add_route("/user_:name", fake_handler()));
        CHECK_NOTHROW(tree.add_route("/id:id", fake_handler()));
        CHECK_NOTHROW(tree.add_route("/src/*filepath", fake_handler()));
        CHECK_NOTHROW(tree.add_route("/src1/", fake_handler()));
        CHECK_NOTHROW(tree.add_route("/con:tact", fake_handler()));
        CHECK_NOTHROW(tree.add_route("/who/are/*you", fake_handler()));
        CHECK_NOTHROW(tree.add_route("/who/foo/hello", fake_handler()));
    }
}

TEST_CASE("Catch-all conflicts") {
    fawkes::node tree;

    SUBCASE("case-1: conflicts with root") {
        tree.add_route("/", fake_handler());
        CHECK_THROWS_AS(tree.add_route("/*filepath", fake_handler()), std::invalid_argument);
    }

    SUBCASE("case-2: catch-all must be the last segment") {
        CHECK_THROWS_AS(tree.add_route("/src/*filepath/x", fake_handler()),
                        std::invalid_argument);
    }

    SUBCASE("case-3: catch-all is not the last segment and has prefix with plain path") {
        tree.add_route("/src2/", fake_handler());
        CHECK_THROWS_AS(tree.add_route("/src2/*filepath/x", fake_handler()),
                        std::invalid_argument);
    }

    SUBCASE("case-4: catch-all is not the last segment and has prefix with another catch-all") {
        tree.add_route("/src3/*filepath", fake_handler());
        CHECK_THROWS_AS(tree.add_route("/src3/*filepath/x", fake_handler()),
                        std::invalid_argument);
    }
}

TEST_CASE("Wildcard conflict error message") {
    auto render_err_msg = [](std::string_view segment,
                             std::string_view full_path,
                             std::string_view wildcard,
                             std::string_view exist_prefix) {
        return fmt::format("'{}' in path '{}' conflicts with existing wildcard '{}' in '{}'",
                           segment, full_path, wildcard, exist_prefix);
    };

    fawkes::node tree;

    SUBCASE("case-1") {
        tree.add_route("/con:tact", fake_handler());
        CHECK_THROWS_WITH_AS(tree.add_route("/conxxx", fake_handler()),
                             render_err_msg("xxx", "/conxxx", ":tact", "/con:tact").c_str(),
                             std::invalid_argument);
        CHECK_THROWS_WITH_AS(tree.add_route("/conooo/xxx", fake_handler()),
                             render_err_msg("ooo", "/conooo/xxx", ":tact", "/con:tact").c_str(),
                             std::invalid_argument);
    }

    SUBCASE("case-2") {
        tree.add_route("/who/are/*you", fake_handler());
        CHECK_THROWS_WITH_AS(
                tree.add_route("/who/are/foo", fake_handler()),
                render_err_msg("/foo", "/who/are/foo", "/*you", "/who/are/*you").c_str(),
                std::invalid_argument);
        CHECK_THROWS_WITH_AS(
                tree.add_route("/who/are/foo/", fake_handler()),
                render_err_msg("/foo/", "/who/are/foo/", "/*you", "/who/are/*you").c_str(),
                std::invalid_argument);
        CHECK_THROWS_WITH_AS(
                tree.add_route("/who/are/foo/bar", fake_handler()),
                render_err_msg("/foo/bar", "/who/are/foo/bar", "/*you", "/who/are/*you").c_str(),
                std::invalid_argument);
    }
}

TEST_CASE("Child path conflicts") {
    fawkes::node tree;

    SUBCASE("case-1") {
        tree.add_route("/cmd/vet", fake_handler());
        CHECK_THROWS_AS(tree.add_route("/cmd/:tool/:sub", fake_handler()),
                        std::invalid_argument);
    }

    SUBCASE("case-2") {
        tree.add_route("/user_x", fake_handler());
        CHECK_THROWS_AS(tree.add_route("/user_:name", fake_handler()),
                        std::invalid_argument);
    }

    SUBCASE("case-3") {
        tree.add_route("/id/:id", fake_handler());
        CHECK_THROWS_AS(tree.add_route("/id:id", fake_handler()),
                        std::invalid_argument);
        CHECK_THROWS_AS(tree.add_route("/:id", fake_handler()),
                        std::invalid_argument);
    }

    SUBCASE("case-4") {
        tree.add_route("/src/AUTHORS", fake_handler());
        CHECK_THROWS_AS(tree.add_route("/src/*filepath", fake_handler()),
                        std::invalid_argument);
    }

    SUBCASE("case-5") {
        tree.add_route("/cmd/vet", fake_handler());
        tree.add_route("/src/AUTHORS", fake_handler());
        tree.add_route("/user_x", fake_handler());
        tree.add_route("/id/:id", fake_handler());
        CHECK_THROWS_AS(tree.add_route("/*filepath", fake_handler()),
                        std::invalid_argument);
    }
}

TEST_CASE("Path duplicates") {
    constexpr std::string_view paths[]{
        "/",
        "/doc/",
        "/src/*filepath",
        "/search/:query",
        "/user_:name",
    };

    fawkes::node tree;
    for (const auto path : paths) {
        CHECK_NOTHROW(tree.add_route(path, fake_handler()));
    }

    for (const auto path : paths) {
        CHECK_THROWS_AS(tree.add_route(path, fake_handler()), std::invalid_argument);
    }
}

TEST_CASE("Priorities of tree") {
    fawkes::node tree;

    SUBCASE("simple routes") {
        constexpr std::string_view paths[]{
            "/hi",
            "/contact",
            "/co",
            "/c",
            "/a",
            "/ab",
            "/doc/",
            "/doc/go_faq.html",
            "/doc/go1.html",
        };

        for (const auto path : paths) {
            tree.add_route(path, fake_handler());
        }

        const fawkes::node_test_inspector inspector(tree);
        CHECK_NOTHROW(inspector.check_priority());
    }

    SUBCASE("wild routes") {
        constexpr std::string_view paths[]{
            "/",
            "/cmd/:tool/:sub",
            "/cmd/:tool/",
            "/src/*filepath",
            "/search/",
            "/search/:query",
            "/user_:name",
            "/user_:name/about",
            "/files/:dir/*filepath",
            "/doc/",
            "/doc/go_faq.html",
            "/doc/go1.html",
            "/info/:user/public",
            "/info/:user/project/:project",
        };

        for (const auto path : paths) {
            tree.add_route(path, fake_handler());
        }

        const fawkes::node_test_inspector inspector(tree);
        CHECK_NOTHROW(inspector.check_priority());
    }
}

//
// Part 2: Locate
//

TEST_CASE("Locate non-wild path") {
    constexpr std::string_view paths[]{
        "/hi",
        "/contact",
        "/co",
        "/c",
        "/a",
        "/ab",
        "/doc/",
        "/doc/go_faq.html",
        "/doc/go1.html",
    };

    fawkes::node tree;
    std::string handler_path;
    for (const auto path : paths) {
        tree.add_route(path, [&handler_path, path](const auto&, auto&) {
            handler_path = path;
        });
    }

    const locate_request requests[]{
        {"/a", true},
        {"/", false},
        {"/hi", true},
        {"/contact", true},
        {"/co", true},
        {"/con", false},
        {"/cona", false},
        {"/no", false},
        {"/ab", true},
        {"/doc", false},
        {"/doc/", true},
    };

    std::vector<fawkes::param> params;
    for (const auto& req : requests) {
        const auto* handler = tree.locate(req.test_path, params);
        CHECK_EQ(handler != nullptr, req.handler_found);
        if (handler) {
            const fawkes::request fake_req;
            fawkes::response fake_resp;
            (*handler)(fake_req, fake_resp);
            CHECK_EQ(handler_path, req.test_path);
        }
    }
}

TEST_CASE("Locate wildcard path") {
    constexpr std::string_view paths[]{
        "/",
        "/cmd/:tool/:sub",
        "/cmd/:tool/",
        "/src/*filepath",
        "/search/",
        "/search/:query",
        "/user_:name",
        "/user_:name/about",
        "/files/:dir/*filepath",
        "/doc/",
        "/doc/go_faq.html",
        "/doc/go1.html",
        "/info/:user/public",
        "/info/:user/project/:project",
    };

    fawkes::node tree;
    std::string handler_path;
    for (const auto path : paths) {
        tree.add_route(path, [&handler_path, path](const auto&, auto&) {
            handler_path = path;
        });
    }

    const locate_request requests[]{
        {"/", true, "/", {}},
        {"/cmd/test/", true, "/cmd/:tool/", {{.key = "tool", .value = "test"}}},
        {"/cmd/test", false, "", {{.key = "tool", .value = "test"}}},
        {"/cmd/test/3",
         true,
         "/cmd/:tool/:sub",
         {{.key = "tool", .value = "test"}, {.key = "sub", .value = "3"}}},
        {"/src/", true, "/src/*filepath", {{.key = "filepath", .value = "/"}}},
        {"/src/some/file.png",
         true,
         "/src/*filepath",
         {{.key = "filepath", .value = "/some/file.png"}}},
        {"/search/", true, "/search/", {}},
        {"/search/someth!ng+in+ünìcodé",
         true,
         "/search/:query",
         {{.key = "query", .value = "someth!ng+in+ünìcodé"}}},
        {"/search/someth!ng+in+ünìcodé/",
         false,
         "",
         {{.key = "query", .value = "someth!ng+in+ünìcodé"}}},
        {"/user_test", true, "/user_:name", {{.key = "name", .value = "test"}}},
        {"/user_test/about", true, "/user_:name/about", {{.key = "name", .value = "test"}}},
        {"/files/js/inc/framework.js",
         true,
         "/files/:dir/*filepath",
         {{.key = "dir", .value = "js"}, {.key = "filepath", .value = "/inc/framework.js"}}},
        {"/info/gordon/public", true, "/info/:user/public", {{.key = "user", .value = "gordon"}}},
        {"/info/gordon/project/go",
         true,
         "/info/:user/project/:project",
         {{.key = "user", .value = "gordon"}, {.key = "project", .value = "go"}}},
    };

    for (const auto& req : requests) {
        INFO("Test path=", req.test_path);
        std::vector<fawkes::param> params;
        const auto* handler = tree.locate(req.test_path, params);
        CHECK_EQ(req.params, params);
        CHECK_EQ(handler != nullptr, req.handler_found);
        if (handler) {
            const fawkes::request fake_req;
            fawkes::response fake_resp;
            (*handler)(fake_req, fake_resp);
            CHECK_EQ(handler_path, req.hit_route);
        }
    }
}

TEST_SUITE_END(); // Router

} // namespace
