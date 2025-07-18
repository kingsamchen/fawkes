cmake_minimum_required(VERSION 3.28)

# Add POLICY below.

# Detect if being bundled via sub-directory.
if(NOT DEFINED PROJECT_NAME)
  set(FAWKES_NOT_SUBPROJECT ON)
else()
  set(FAWKES_NOT_SUBPROJECT OFF)
endif()

message(STATUS "fawkes as root project = ${FAWKES_NOT_SUBPROJECT}")

project(fawkes CXX)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(FAWKES_NOT_SUBPROJECT)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

  set(ROOT_DIR ${CMAKE_SOURCE_DIR})
endif()

option(FAWKES_BUILD_EXAMPLES "If enabled, build examples" OFF)

set(FAWKES_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(FAWKES_CMAKE_DIR ${FAWKES_DIR}/cmake)

include(CTest)

message(STATUS "fawkes GENERATOR = " ${CMAKE_GENERATOR})
message(STATUS "FAWKES_BUILD_EXAMPLES = " ${FAWKES_BUILD_EXAMPLES})

# Output configurations.
get_property(MULTICONF_GENERATOR GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(FAWKES_NOT_SUBPROJECT)
  if(NOT MULTICONF_GENERATOR)
    if(NOT CMAKE_BUILD_TYPE)
      set(CMAKE_BUILD_TYPE "Release")
    endif()
    message(STATUS "fawkes BUILD_TYPE = " ${CMAKE_BUILD_TYPE})
  endif()
else()
  if(NOT MULTICONF_GENERATOR)
    message(STATUS "fawkes BUILD_TYPE = " ${CMAKE_BUILD_TYPE})
  endif()
endif()

# Compiler configurations.
if(MSVC)
  include(${FAWKES_CMAKE_DIR}/compiler_msvc.cmake)
else()
  include(${FAWKES_CMAKE_DIR}/compiler_posix.cmake)
endif()

include(${FAWKES_CMAKE_DIR}/pch.cmake)

include(${FAWKES_CMAKE_DIR}/clang_tidy.cmake)
include(${FAWKES_CMAKE_DIR}/sanitizer.cmake)

find_package(boost_asio CONFIG REQUIRED)
find_package(boost_beast CONFIG REQUIRED)
find_package(doctest CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(Threads REQUIRED)

target_compile_definitions(Boost::asio
  INTERFACE
    $<$<BOOL:${WIN32}>:
      _WIN32_WINNT=0x0601
    >
)

add_subdirectory(fawkes)

if(FAWKES_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
