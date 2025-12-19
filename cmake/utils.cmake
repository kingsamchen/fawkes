
function(fawkes_get_target_type TARGET TARGET_TYPE)
  get_target_property(target_type ${TARGET} TYPE)

  if(target_type STREQUAL "EXECUTABLE")
    set(${TARGET_TYPE} "EXECUTABLE" PARENT_SCOPE)
  elseif(target_type STREQUAL "STATIC_LIBRARY")
    set(${TARGET_TYPE} "STATIC_LIBRARY" PARENT_SCOPE)
  elseif(target_type STREQUAL "SHARED_LIBRARY")
    set(${TARGET_TYPE} "SHARED_LIBRARY" PARENT_SCOPE)
  elseif(target_type STREQUAL "MODULE_LIBRARY")
    set(${TARGET_TYPE} "MODULE_LIBRARY" PARENT_SCOPE)
  elseif(target_type STREQUAL "OBJECT_LIBRARY")
    set(${TARGET_TYPE} "OBJECT_LIBRARY" PARENT_SCOPE)
  elseif(target_type STREQUAL "INTERFACE_LIBRARY")
    set(${TARGET_TYPE} "INTERFACE_LIBRARY" PARENT_SCOPE)
  else()
    message(FATAL_ERROR "Target ${TARGET} unknown type ${target_type}")
  endif()
endfunction()

function(fawkes_setup_compile_db)
  if(NOT CMAKE_GENERATOR MATCHES "Visual Studio")
    file(CREATE_LINK
      "${CMAKE_BINARY_DIR}/compile_commands.json"
      "${CMAKE_BINARY_DIR}/../compile_commands.json"
      SYMBOLIC
    )
  endif()
endfunction()

function(fawkes_source_folder TARGET)
  cmake_parse_arguments(ARGS "" "TARGET_FOLDER" "" ${ARGN})

  get_target_property(TARGET_SRC_FILES ${TARGET} SOURCES)
  source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${TARGET_SRC_FILES})

  if (DEFINED ARGS_TARGET_FOLDER AND NOT ARGS_TARGET_FOLDER STREQUAL "")
    set_target_properties(${TARGET} PROPERTIES FOLDER "${ARGS_TARGET_FOLDER}")
  endif()
endfunction()
