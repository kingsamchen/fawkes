// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <semaphore>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/json/object.hpp>
#include <boost/json/serialize.hpp>
#include <boost/system/system_error.hpp>
#include <fmt/format.h>
#include <gflags/gflags.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "fawkes/errors.hpp"
#include "fawkes/io_thread_pool.hpp"
#include "fawkes/middleware.hpp"
#include "fawkes/request.hpp"
#include "fawkes/response.hpp"
#include "fawkes/server.hpp"

namespace asio = boost::asio;
namespace http = boost::beast::http;
namespace json = boost::json;

using namespace std::chrono_literals;

DEFINE_uint32(port, 0, "Loopback port selected by the test driver");
DEFINE_int32(idle_timeout_ms, 0, "Connection idle timeout in milliseconds");
DEFINE_int32(read_timeout_ms, 0, "Request read timeout in milliseconds");
DEFINE_int32(serve_timeout_ms, 0, "Request serve timeout in milliseconds");
DEFINE_uint64(body_limit,
              0,
              "Request body limit; zero uses the framework default");
DEFINE_uint32(header_limit,
              0,
              "Request header limit; zero uses the framework default");
DEFINE_int32(rst_avoidance_delay_ms,
             500,
             "Rejection close delay in milliseconds");
DEFINE_bool(rejection_hook,
            false,
            "Mark framework rejections with a test header");
DEFINE_uint32(hold_ms, 1000, "Duration of the asynchronous test handler");
DEFINE_uint32(worker_threads,
              0,
              "Independent test workers; zero disables them");

namespace {

constexpr std::string_view request_id_header = "X-Test-Request-Id";

// Stdout is a JSON-lines control channel; application logs go to stderr.
void emit_event(const json::object& event) {
    static std::mutex event_mutex;
    const std::scoped_lock lock(event_mutex);
    fmt::println("{}", json::serialize(event));
    std::fflush(stdout);
}

struct worker_gate {
    std::mutex mutex;
    std::condition_variable changed;
    std::size_t entered_count{0};
    bool released{false};

    void wait(const std::string& tag) {
        std::unique_lock lock(mutex);
        ++entered_count;
        emit_event({{"event", "worker_blocked"}, {"tag", tag}});
        if (!changed.wait_for(lock, 5s, [this] {
                return released;
            })) {
            throw std::runtime_error("Worker gate did not release the job");
        }
    }

    std::size_t release() {
        const std::scoped_lock lock(mutex);
        released = true;
        changed.notify_all();
        return entered_count;
    }
};

asio::awaitable<std::string> wait_on_worker(worker_gate& gate,
                                            std::string tag) {
    gate.wait(tag);
    co_return tag;
}

asio::awaitable<void> worker_response(asio::thread_pool& workers,
                                      worker_gate& gate,
                                      const fawkes::request& request,
                                      fawkes::response& response) {
    const auto tag = co_await asio::co_spawn(workers.get_executor(),
                                             wait_on_worker(gate, request.body()),
                                             asio::use_awaitable);
    response.json(http::status::ok, json::serialize(json::object{{"tag", tag}}));
}

asio::awaitable<void> release_workers(worker_gate& gate,
                                      fawkes::response& response) {
    response.json(http::status::ok,
                  json::serialize(json::object{{"released", gate.release()}}));
    co_return;
}

struct context_value {
    std::string marker;
    std::string request_id;
};

struct context_gate {
    std::vector<std::shared_ptr<asio::steady_timer>> pending;

    asio::awaitable<void> wait(const fawkes::request& request) {
        const auto executor = co_await asio::this_coro::executor;
        const auto timer = std::make_shared<asio::steady_timer>(executor, 5s);
        pending.push_back(timer);
        emit_event({{"event", "context_waiting"}, {"body", request.body()}});
        boost::system::error_code error;
        co_await timer->async_wait(asio::redirect_error(error));
        if (error != asio::error::operation_aborted) {
            throw std::runtime_error("Context gate did not release the request");
        }
    }

    std::size_t release() {
        const auto count = pending.size();
        for (const auto& timer : pending) {
            timer->cancel();
        }
        pending.clear();
        return count;
    }
};

void write_context_response(const fawkes::request& request,
                            fawkes::response& response,
                            http::status status) {
    json::object body{
        {"marker", request.header()["X-Test-Marker"]},
        {"request_id", request.header()[request_id_header]},
        {"body", request.body()},
        {"context", nullptr}};
    if (request.context().has_value()) {
        const auto& context = request.context_as<context_value>();
        body["context"] = json::object{{"marker", context.marker},
                                       {"request_id", context.request_id}};
    }
    response.json(status, json::serialize(body));
}

struct context_probe {
    std::reference_wrapper<context_gate> gate;

    asio::awaitable<fawkes::middleware_result> pre_handle(
        fawkes::request& request, fawkes::response& response) const {
        if (request.context().has_value()) {
            throw std::logic_error("Request arrived with stale context");
        }
        const auto marker = request.header()["X-Test-Marker"];
        if (!marker.empty()) {
            request.context() = context_value{
                .marker = std::string(marker),
                .request_id =
                    std::string(request.header()[request_id_header])};
        }
        co_await gate.get().wait(request);
        if (request.header()["X-Test-Mode"] == "abort") {
            write_context_response(request, response, http::status::bad_request);
            co_return fawkes::middleware_result::abort;
        }
        co_return fawkes::middleware_result::proceed;
    }

    static fawkes::middleware_result post_handle(fawkes::request& request,
                                                 fawkes::response& response) {
        const auto marker = request.header()["X-Test-Marker"];
        const auto request_id =
            request.header()[request_id_header];
        if (!marker.empty()) {
            response.header().set("X-Test-Header-Marker", marker);
        }
        if (!request_id.empty()) {
            response.header().set("X-Test-Header-Request-Id", request_id);
        }
        const bool has_context = request.context().has_value();
        response.header().set("X-Test-Has-Context", has_context ? "true" : "false");
        if (has_context) {
            const auto& context = request.context_as<context_value>();
            response.header().set("X-Test-Context-Marker", context.marker);
            if (!context.request_id.empty()) {
                response.header().set("X-Test-Context-Request-Id", context.request_id);
            }
        }
        return fawkes::middleware_result::proceed;
    }
};

asio::awaitable<void> release_contexts(context_gate& gate,
                                       fawkes::response& response) {
    response.json(http::status::ok,
                  json::serialize(json::object{{"released", gate.release()}}));
    co_return;
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

asio::awaitable<void> finish_after_delay(fawkes::response& response) {
    const auto executor = co_await asio::this_coro::executor;
    asio::steady_timer waiter(executor, std::chrono::milliseconds(FLAGS_hold_ms));
    emit_event({{"event", "holding"}});
    co_await waiter.async_wait(asio::deferred);
    emit_event({{"event", "handled"}});
    response.text(http::status::ok, std::string{"finished"});
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
    spdlog::set_pattern("[%l] [%s] %v");
    try {
        if (FLAGS_port == 0 || !std::in_range<std::uint16_t>(FLAGS_port)) {
            throw std::invalid_argument("port must be between 1 and 65535");
        }
        if (!std::in_range<std::size_t>(FLAGS_body_limit)) {
            throw std::invalid_argument("body_limit does not fit in size_t");
        }
        if (FLAGS_hold_ms == 0 || FLAGS_hold_ms > 10000) {
            throw std::invalid_argument("hold_ms must be between 1 and 10000");
        }
        if (FLAGS_worker_threads > 8) {
            throw std::invalid_argument("worker_threads must be at most 8");
        }
        asio::io_context listener_context{1};
        std::binary_semaphore resume_session{0};
        // A single worker makes HTTP barriers and the paused write deterministic.
        fawkes::io_thread_pool session_pool{1};
        context_gate context_waiter;
        worker_gate worker_waiter;
        std::unique_ptr<asio::thread_pool> workers;
        if (FLAGS_worker_threads > 0) {
            workers = std::make_unique<asio::thread_pool>(FLAGS_worker_threads);
        }
        fawkes::server server(listener_context, session_pool);
        server.set_options({
            .idle_timeout = std::chrono::milliseconds(FLAGS_idle_timeout_ms),
            .read_timeout = std::chrono::milliseconds(FLAGS_read_timeout_ms),
            .serve_timeout = std::chrono::milliseconds(FLAGS_serve_timeout_ms),
            .request_body_limit =
                FLAGS_body_limit == 0
                    ? std::nullopt
                    : std::make_optional(static_cast<std::size_t>(FLAGS_body_limit)),
            .request_header_limit = FLAGS_header_limit == 0
                                        ? std::nullopt
                                        : std::make_optional(FLAGS_header_limit),
            .rst_avoidance_delay =
                std::chrono::milliseconds(FLAGS_rst_avoidance_delay_ms),
        });
        if (FLAGS_rejection_hook) {
            server.set_rejection_response_hook([](fawkes::response& response) {
                response.header().set("X-Fawkes-Test-Rejection", "applied");
            });
        }
        asio::signal_set signals(listener_context, SIGINT, SIGTERM);
        signals.async_wait([&](const boost::system::error_code& error, int) {
            if (error) {
                throw boost::system::system_error(error);
            }
            server.stop();
            // Release test jobs too so failed assertions cannot strand workers.
            worker_waiter.release();
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
        if (workers) {
            server.do_post(
                "/work",
                [&pool = *workers, &worker_waiter](const fawkes::request& request,
                                                   fawkes::response& response) {
                    return worker_response(pool, worker_waiter, request, response);
                });
            server.do_post(
                "/work/release",
                [&worker_waiter](const fawkes::request&, fawkes::response& response) {
                    return release_workers(worker_waiter, response);
                });
        }
        server.do_post(
            "/context",
            fawkes::middlewares::use(context_probe{.gate = std::ref(context_waiter)}),
            [](const fawkes::request& request,
               fawkes::response& response) -> asio::awaitable<void> {
                response.header().set("X-Test-Handler", "executed");
                if (request.header()["X-Test-Mode"] == "error") {
                    throw fawkes::http_error(http::status::unprocessable_entity,
                                             "Context probe failure");
                }
                if (request.header()["X-Test-Mode"] == "exception") {
                    throw std::runtime_error(
                        fmt::format("Context probe internal failure: {}", request.body()));
                }
                write_context_response(request, response, http::status::ok);
                co_return;
            });
        server.do_post(
            "/context/release",
            [&context_waiter](const fawkes::request&, fawkes::response& response) {
                return release_contexts(context_waiter, response);
            });
        server.do_get("/wait",
                      [](const fawkes::request&, fawkes::response& response) {
                          return finish_after_delay(response);
                      });
        server.do_get("/large",
                      [](const fawkes::request&,
                         fawkes::response& response) -> asio::awaitable<void> {
                          response.text(http::status::ok,
                                        std::string(8UZ * 1024 * 1024, 'x'));
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
        if (workers) {
            workers->join();
        }
    } catch (const std::exception& error) {
        SPDLOG_ERROR("Test server failed: {}", error.what());
        return 1;
    }
    return 0;
}
