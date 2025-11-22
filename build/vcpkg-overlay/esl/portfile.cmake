vcpkg_from_github(
  OUT_SOURCE_PATH SOURCE_PATH
  REPO kingsamchen/esl
  REF 7769ebc4400dddf7a15c32e66307469d57d19d3c
  SHA512 7263de05bdb49302edb9f18f1cd40b5e8793d554cc9a58d1215f23a54166a1a4963816c25f45ffb176e17ca0958bca8dc4bb579c00adfb9d0dea3518b706be32
  HEAD_REF master
)

vcpkg_cmake_configure(
  SOURCE_PATH "${SOURCE_PATH}"
  OPTIONS
    -DESL_INSTALL_CMAKE_DIR=share/esl
    -DBUILD_TESTING=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup()

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")

file(REMOVE_RECURSE
  "${CURRENT_PACKAGES_DIR}/debug"
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
