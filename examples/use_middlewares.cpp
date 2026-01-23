// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/url.hpp>
#include <esl/ignore_unused.h>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <gflags/gflags.h>
#include <spdlog/cfg/env.h>
#include <spdlog/spdlog.h>

#include "fawkes/middleware.hpp"
#include "fawkes/middlewares/cors.hpp"
#include "fawkes/request.hpp"
#include "fawkes/response.hpp"
#include "fawkes/server.hpp"

namespace asio = boost::asio;
namespace urls = boost::urls;
namespace http = boost::beast::http;

DEFINE_uint32(port, 7890, "Port number to listen on");

struct log_access {
    static fawkes::middleware_result pre_handle(fawkes::request& req, fawkes::response& /*resp*/) {
        SPDLOG_INFO("Entering {} {}", req.header().method_string(), req.target());
        return fawkes::middleware_result::proceed;
    }

    static fawkes::middleware_result post_handle(fawkes::request& req, fawkes::response& resp) {
        SPDLOG_INFO("Leave {} -> {}", req.target(), resp.status_code());
        return fawkes::middleware_result::proceed;
    }
};

struct tracking_id {
    static fawkes::middleware_result pre_handle(fawkes::request& req, fawkes::response& resp) {
        static constexpr std::string_view name = "x-tracking-id";
        if (const auto it = req.header().find(name); it == req.header().end()) {
            SPDLOG_INFO("Tracking-id not found in request, generate on the fly");
            const auto ts = std::chrono::system_clock::now().time_since_epoch().count();
            const std::string new_id = fmt::to_string(ts);
            req.header().set(name, new_id);
            resp.header().set(name, new_id);
        } else {
            resp.header().set(name, it->value());
        }

        return fawkes::middleware_result::proceed;
    }
};

struct coro_delayed {
    static asio::awaitable<fawkes::middleware_result> pre_handle(fawkes::request& /*req*/,
                                                                 fawkes::response& /*resp*/) {
        asio::steady_timer timer(co_await asio::this_coro::executor);
        timer.expires_after(std::chrono::seconds(1));
        co_await timer.async_wait();
        co_return fawkes::middleware_result::proceed;
    }
};

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    spdlog::cfg::load_env_levels();

    try {
        asio::io_context io_ctx{1};

        fawkes::server svc(io_ctx);

        fawkes::cors cors{fawkes::cors::options{
            .allow_origin_policy = fawkes::cors::allow_origin_if(
                [](std::string_view origin) {
                    const urls::url_view url(origin);
                    return url.host() == "deadbeef.me";
                }),
            .allow_methods{http::verb::get, http::verb::post, http::verb::options},
            .allow_headers{http::field::content_type},
            .expose_headers{}}};

        // Global middlewares, shared by all routes.
        svc.get_router().use(log_access{}, std::move(cors));

        // Per-route middlewares.
        svc.do_get("/now",
                   fawkes::middlewares::use(tracking_id{}),
                   [](const fawkes::request& req, fawkes::response& resp)
                       -> asio::awaitable<void> {
                       esl::ignore_unused(req);
                       auto tp = std::chrono::system_clock::now();
                       resp.text(http::status::ok, fmt::format("{}", tp));
                       co_return;
                   });

        svc.do_get("/healthcheck",
                   fawkes::middlewares::use(coro_delayed{}),
                   [](const fawkes::request& req, fawkes::response& resp)
                       -> asio::awaitable<void> {
                       esl::ignore_unused(req);
                       resp.text(http::status::ok, std::string{"Pong after 1s delay"});
                       co_return;
                   });

        // CORS support via the global middleware.

        svc.do_get("/simple",
                   [](const fawkes::request& req, fawkes::response& resp)
                       -> asio::awaitable<void> {
                       esl::ignore_unused(req);
                       resp.text(http::status::ok, std::string{"response for simple request"});
                       co_return;
                   });

        svc.do_post("/preflight",
                    [](const fawkes::request& req, fawkes::response& resp)
                        -> asio::awaitable<void> {
                        auto body = fmt::format("response for request that needs preflight: {}",
                                                req.body());
                        resp.text(http::status::ok, std::move(body));
                        co_return;
                    });

        svc.listen_and_serve("0.0.0.0", static_cast<std::uint16_t>(FLAGS_port));
        SPDLOG_INFO("Server is listening at {}", FLAGS_port);

        io_ctx.run();
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("Unexpected error: {}", ex.what());
    }

    return 0;
}
