// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <semaphore>
#include <stdexcept>
#include <string>
#include <utility>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/json/object.hpp>
#include <boost/json/serialize.hpp>
#include <boost/system/system_error.hpp>
#include <fmt/format.h>
#include <gflags/gflags.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "fawkes/io_thread_pool.hpp"
#include "fawkes/request.hpp"
#include "fawkes/response.hpp"
#include "fawkes/server.hpp"

namespace asio = boost::asio;
namespace http = boost::beast::http;
namespace json = boost::json;

using namespace std::chrono_literals;

DEFINE_uint32(port, 0, "Loopback port selected by the test driver");

namespace {

// Stdout is a JSON-lines control channel; application logs go to stderr.
void emit_event(const json::object& event) {
    fmt::println("{}", json::serialize(event));
    std::fflush(stdout);
}

void pause_until_stop(std::binary_semaphore& resume_session,
                      const char* event) {
    emit_event({{"event", event}});
    if (!resume_session.try_acquire_for(10s)) {
        fmt::println(stderr, "Timed out waiting for stop during {}", event);
        std::abort();
    }
}

asio::awaitable<void> finish_after_stop(std::binary_semaphore& resume_session,
                                        fawkes::response& response) {
    // Block only the session worker; the listener still handles SIGTERM.
    pause_until_stop(resume_session, "handling");
    response.text(http::status::ok, std::string{"finished"});
    co_return;
}

asio::awaitable<void> finish_with_queued_pause(
    asio::io_context::executor_type executor,
    std::binary_semaphore& resume_session,
    fawkes::response& response) {
    // This runs after initiating the small final write, before its completion.
    // Stop on the listener queues cancellation behind that completion. Once
    // resumed, the session exits before the cancellation callback executes.
    asio::post(executor, [&resume_session] {
        pause_until_stop(resume_session, "paused");
    });
    response.text(http::status::ok, std::string{"finished"});
    co_return;
}

} // namespace

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    spdlog::set_default_logger(spdlog::stderr_color_mt("fawkes_e2e"));
    try {
        if (FLAGS_port == 0 || !std::in_range<std::uint16_t>(FLAGS_port)) {
            throw std::invalid_argument("port must be between 1 and 65535");
        }
        asio::io_context listener_context{1};
        std::binary_semaphore resume_session{0};
        // A single worker makes HTTP barriers and the paused write deterministic.
        fawkes::io_thread_pool session_pool{1};
        fawkes::server server(listener_context, session_pool);
        asio::signal_set signals(listener_context, SIGINT, SIGTERM);
        signals.async_wait([&](const boost::system::error_code& error, int) {
            if (error) {
                throw boost::system::system_error(error);
            }
            server.stop();
            emit_event({{"event", "stopping"}});
            // Release only after stop() has queued every session's cancellation.
            resume_session.release();
        });
        server.do_get("/ping",
                      [](const fawkes::request&,
                         fawkes::response& response) -> asio::awaitable<void> {
                          response.text(http::status::ok, std::string{"Pong!"});
                          co_return;
                      });
        server.do_post("/echo",
                       [](const fawkes::request& request,
                          fawkes::response& response) -> asio::awaitable<void> {
                           response.text(http::status::ok, request.body());
                           co_return;
                       });
        server.do_get(
            "/active",
            [&resume_session](const fawkes::request&, fawkes::response& response) {
                return finish_after_stop(resume_session, response);
            });
        const auto session_executor = session_pool.get_executor();
        server.do_get("/stop",
                      [&resume_session, session_executor](
                          const fawkes::request&, fawkes::response& response) {
                          return finish_with_queued_pause(
                              session_executor, resume_session, response);
                      });
        server.listen_and_serve("127.0.0.1",
                                static_cast<std::uint16_t>(FLAGS_port));
        emit_event({{"event", "ready"}, {"port", FLAGS_port}});
        listener_context.run();
        // Drain queued callbacks too, so ASan can catch cancellation after exit.
        session_pool.join();
    } catch (const std::exception& error) {
        SPDLOG_ERROR("Test server failed: {}", error.what());
        return 1;
    }
    return 0;
}
