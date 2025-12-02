// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <string_view>

namespace fawkes {

struct mime {
    static constexpr std::string_view json = "application/json";
    static constexpr std::string_view text = "text/plain";
};

} // namespace fawkes
