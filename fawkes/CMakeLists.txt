
add_library(fawkes)

add_library(fawkes::fawkes ALIAS fawkes)

target_sources(fawkes
  PRIVATE
    cookie.cpp
    cookie.hpp
    errors.hpp
    io_thread_pool.cpp
    io_thread_pool.hpp
    is_asio_awaitable.hpp
    middleware.hpp
    middlewares/cors.cpp
    middlewares/cors.hpp
    mime.hpp
    path_params.hpp
    query_params.hpp
    request.cpp
    request.hpp
    response.cpp
    response.hpp
    router.cpp
    router.hpp
    server.cpp
    server.hpp
    tree.cpp
    tree.hpp

  $<$<BOOL:${WIN32}>:
  >

  $<$<NOT:$<BOOL:${WIN32}>>:
  >
)

target_include_directories(fawkes
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(fawkes
  PRIVATE
    esl::esl
    fmt::fmt
    spdlog::spdlog

  PUBLIC
    Boost::asio
    Boost::beast
    Boost::json
    Boost::url
    Threads::Threads
)

fawkes_common_compile_configs(fawkes)

fawkes_clang_tidy_on_build(fawkes)

fawkes_use_sanitizers(fawkes)

fawkes_source_folder(fawkes)

target_precompile_headers(fawkes REUSE_FROM fawkes::pch)

#
# fawkes_tests
#

if(NOT BUILD_TESTING)
  return()
endif()

add_executable(fawkes_tests)

target_sources(fawkes_tests
  PRIVATE
    test_main.cpp

    cookie_test.cpp
    io_thread_pool_test.cpp
    middlewares/cors_test.cpp
    request_test.cpp
    router_test.cpp
    server_options_test.cpp
    tree_test.cpp
)

target_link_libraries(fawkes_tests
  PRIVATE
    doctest::doctest
    fawkes::fawkes
    fawkes::test_utils
)

target_include_directories(fawkes_tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../
)

fawkes_common_compile_configs(fawkes_tests)

fawkes_clang_tidy_on_build(fawkes_tests)

fawkes_use_sanitizers(fawkes_tests)

fawkes_source_folder(fawkes_tests)

target_precompile_headers(fawkes_tests REUSE_FROM fawkes::pch)

add_test(NAME FawkesTests COMMAND fawkes_tests)
