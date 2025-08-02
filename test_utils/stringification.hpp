// Copyright (c) 2025 - present, Kingsley Chen. All rights reserved.
// This file is subject to the terms of license that can be found
// in the LICENSE file.

#pragma once

#include <deque>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <doctest/doctest.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

namespace doctest {

template<typename T>
struct StringMaker<std::vector<T>> {
    static String convert(const std::vector<T>& in) {
        return fmt::to_string(in).c_str();
    }
};

template<typename T>
struct StringMaker<std::list<T>> {
    static String convert(const std::list<T>& in) {
        auto s = fmt::to_string(in);
        return String(s.data(), s.size());
    }
};

template<typename T>
struct StringMaker<std::deque<T>> {
    static String convert(const std::deque<T>& in) {
        auto s = fmt::to_string(in);
        return String(s.data(), s.size());
    }
};

template<typename T>
struct StringMaker<std::set<T>> {
    static String convert(const std::set<T>& in) {
        auto s = fmt::to_string(in);
        return String(s.data(), s.size());
    }
};

template<typename T>
struct StringMaker<std::unordered_set<T>> {
    static String convert(const std::unordered_set<T>& in) {
        auto s = fmt::to_string(in);
        return String(s.data(), s.size());
    }
};

template<typename K, typename V>
struct StringMaker<std::map<K, V>> {
    static String convert(const std::map<K, V>& in) {
        auto s = fmt::to_string(in);
        return String(s.data(), s.size());
    }
};

template<typename K, typename V>
struct StringMaker<std::unordered_map<K, V>> {
    static String convert(const std::unordered_map<K, V>& in) {
        auto s = fmt::to_string(in);
        return String(s.data(), s.size());
    }
};

} // namespace doctest
