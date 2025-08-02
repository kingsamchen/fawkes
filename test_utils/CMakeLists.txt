
add_library(test_utils OBJECT)

add_library(fawkes::test_utils ALIAS test_utils)

target_sources(test_utils
  PRIVATE
    stringification.hpp
)

target_include_directories(test_utils
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../
)

target_link_libraries(test_utils
  PUBLIC
    doctest::doctest
    fmt::fmt
)

fawkes_common_compile_configs(test_utils)

fawkes_clang_tidy_on_build(test_utils)

fawkes_use_sanitizers(test_utils)

get_target_property(test_utils_FILES test_utils SOURCES)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${test_utils_FILES})
