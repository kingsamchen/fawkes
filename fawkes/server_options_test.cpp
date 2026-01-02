// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#include <chrono>

#include <doctest/doctest.h>

#include "fawkes/server.hpp"

namespace {

TEST_SUITE_BEGIN("Server/Options");

TEST_CASE("Effective read timeout") {
    using namespace std::chrono_literals;

    fawkes::server::options opts;

    SUBCASE("no timeout") {
        CHECK_EQ(opts.effective_read_timeout().count(), 0);
    }

    struct {
        std::chrono::milliseconds rt;
        std::chrono::milliseconds st;
        std::chrono::milliseconds ert;
    } const cfgs[]{
        {.rt = 5s, .st = 10s, .ert = 5s},   // read-timeout is applied
        {.rt = 10s, .st = 5s, .ert = 5s},   // serve-timeout is applied
        {.rt = 5s, .st = 0s, .ert = 5s},    // serve-timeout is not enabled
        {.rt = 5s, .st = -5s, .ert = 5s},   // serve-timeout is not enabled
        {.rt = 0s, .st = 5s, .ert = 5s},    // serve-timeout is still applied
        {.rt = -5s, .st = 5s, .ert = 5s},   // serve-timeout is still applied
        {.rt = -5s, .st = 0s, .ert = 0s},   // both are not enabled.
        {.rt = 0s, .st = -5s, .ert = 0s},   // both are not enabled.
        {.rt = -5s, .st = -10s, .ert = 0s}, // both are not enabled.
        {.rt = -10s, .st = -5s, .ert = 0s}, // both are not enabled.
    };

    for (auto cfg : cfgs) {
        opts.read_timeout = cfg.rt;
        opts.serve_timeout = cfg.st;
        CHECK_EQ(opts.effective_read_timeout(), cfg.ert);
    }
}

TEST_SUITE_END(); // Server/Options

} // namespace
