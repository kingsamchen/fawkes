// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include <boost/asio/io_context.hpp>
#include <esl/ignore_unused.h>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <gflags/gflags.h>
#include <spdlog/spdlog.h>

#include "fawkes/middleware.hpp"
#include "fawkes/request.hpp"
#include "fawkes/response.hpp"
#include "fawkes/server.hpp"

DEFINE_uint32(port, 7890, "Port number to listen");

struct log_access {
    static fawkes::middleware_result pre_handle(fawkes::request& req, fawkes::response& /*resp*/) {
        SPDLOG_INFO("Entering {} {}", req.header().method_string(), req.target());
        return fawkes::middleware_result::proceed;
    }

    static fawkes::middleware_result post_handle(fawkes::request& req, fawkes::response& resp) {
        SPDLOG_INFO("Leave {} -> {}", req.target(), resp.as_impl().result_int());
        return fawkes::middleware_result::proceed;
    }
};

struct tracking_id {
    static fawkes::middleware_result pre_handle(fawkes::request& req, fawkes::response& resp) {
        constexpr std::string_view name = "x-tracking-id";
        if (const auto it = req.header().find(name); it == req.header().end()) {
            SPDLOG_INFO("Tracking-id not found in request, generate on the fly");
            const auto ts = std::chrono::system_clock::now().time_since_epoch().count();
            const std::string new_id = fmt::to_string(ts);
            req.mutable_header().insert(name, new_id);
            resp.mutable_header().insert(name, new_id);
        } else {
            resp.mutable_header().insert(name, it->value());
        }

        return fawkes::middleware_result::proceed;
    }
};

int main() {
    try {
        boost::asio::io_context io_ctx{1};

        fawkes::server svc(io_ctx);

        // Global middlewares, shared by all routes.
        svc.get_router().use(log_access{});

        // Per-route middlewares.
        svc.do_get(
                "/now",
                [](const fawkes::request& req, fawkes::response& resp) {
                    esl::ignore_unused(req);
                    auto tp = std::chrono::system_clock::now();
                    resp.mutable_body() = fmt::format("{}", tp);
                },
                fawkes::middlewares::use(tracking_id{}));

        svc.do_get("/healthcheck", [](const fawkes::request& req, fawkes::response& resp) {
            esl::ignore_unused(req);
            resp.mutable_body() = "pong";
        });

        svc.listen_and_serve("0.0.0.0", static_cast<std::uint16_t>(FLAGS_port));
        SPDLOG_INFO("Server is listening at {}", FLAGS_port);

        io_ctx.run();
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("Unexpected error: {}", ex.what());
    }

    return 0;
}
