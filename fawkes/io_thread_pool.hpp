// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>

namespace fawkes {

namespace asio = boost::asio;

class io_thread_pool {
public:
    using executor_type = asio::io_context::executor_type;

    // Throws `std::invalid_argument` if `num_threads` is 0.
    explicit io_thread_pool(std::size_t num_threads);

    ~io_thread_pool() {
        stop();
    }

    io_thread_pool(const io_thread_pool&) = delete;
    io_thread_pool(io_thread_pool&&) = delete;
    io_thread_pool& operator=(const io_thread_pool&) = delete;
    io_thread_pool& operator=(io_thread_pool&&) = delete;

    // Round-robin scheduling.
    [[nodiscard]] executor_type get_executor() {
        const auto idx = next_io_executor_.fetch_add(1, std::memory_order_relaxed) % size();
        return pool_[idx].io_ctx_ptr->get_executor();
    }

    // Blocks until the threads in the pool have completed all outstanding works.
    // After `join()` is called, it is unspecified whether new works submitted to the pool
    // would be executed.
    void join() {
        for (auto& ctx : pool_) {
            ctx.guard.reset();
            ctx.thd.join();
        }
    }

    // Stops the pool as soon as possible.
    // Pending tasks would not be handled.
    void stop() {
        for (auto& ctx : pool_) {
            ctx.io_ctx_ptr->stop();
        }
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return pool_.size();
    }

private:
    struct thread_context {
        std::unique_ptr<asio::io_context> io_ctx_ptr;
        asio::executor_work_guard<executor_type> guard;
        std::jthread thd;

        thread_context()
            : io_ctx_ptr(std::make_unique<asio::io_context>(1)),
              guard(io_ctx_ptr->get_executor()) {}
    };

    static_assert(std::is_move_constructible_v<thread_context>);

    std::vector<thread_context> pool_;
    std::atomic<std::size_t> next_io_executor_{0U};
};

} // namespace fawkes
