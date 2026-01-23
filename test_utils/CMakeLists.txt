
add_library(test_utils OBJECT)

add_library(fawkes::test_utils ALIAS test_utils)

target_sources(test_utils
  PRIVATE
    stringification.hpp
    run_awaitable_sync.cpp
    run_awaitable_sync.hpp
)

target_include_directories(test_utils
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../
)

target_link_libraries(test_utils
  PUBLIC
    doctest::doctest
    fmt::fmt
    Boost::asio
)

fawkes_common_compile_configs(test_utils)

fawkes_clang_tidy_on_build(test_utils)

fawkes_use_sanitizers(test_utils)

fawkes_source_folder(test_utils)

# Force MSVC use CXX linker.
set_target_properties(test_utils PROPERTIES LINKER_LANGUAGE CXX)
