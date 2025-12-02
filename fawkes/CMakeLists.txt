
add_library(fawkes)

add_library(fawkes::fawkes ALIAS fawkes)

target_sources(fawkes
  PRIVATE
    errors.hpp
    middleware.hpp
    middlewares/cors.cpp
    middlewares/cors.hpp
    mime.hpp
    path_params.hpp
    query_params.hpp
    request.cpp
    request.hpp
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

get_target_property(fawkes_FILES fawkes SOURCES)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${fawkes_FILES})

target_precompile_headers(fawkes REUSE_FROM fawkes.pch)

#
# fawkes.tests
#

if(NOT BUILD_TESTING)
  return()
endif()

add_executable(fawkes.tests)

target_sources(fawkes.tests
  PRIVATE
    test_main.cpp

    middlewares/cors_test.cpp
    request_test.cpp
    router_test.cpp
    tree_test.cpp
)

target_link_libraries(fawkes.tests
  PRIVATE
    doctest::doctest
    fawkes::fawkes
    fawkes::test_utils
)

target_include_directories(fawkes.tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../
)

fawkes_common_compile_configs(fawkes.tests)

fawkes_clang_tidy_on_build(fawkes.tests)

fawkes_use_sanitizers(fawkes.tests)

get_target_property(fawkes.tests_FILES fawkes.tests SOURCES)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${fawkes.tests_FILES})

target_precompile_headers(fawkes.tests REUSE_FROM fawkes.pch)

add_test(NAME FawkesTests COMMAND fawkes.tests)
