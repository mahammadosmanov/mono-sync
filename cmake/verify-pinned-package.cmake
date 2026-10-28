if(NOT DEFINED BUILD_DIR)
  message(FATAL_ERROR "BUILD_DIR must point to the configured build directory")
endif()

set(cpack_config "${BUILD_DIR}/CPackConfig.cmake")
if(NOT EXISTS "${cpack_config}")
  message(FATAL_ERROR "Missing CPack configuration: ${cpack_config}")
endif()

file(READ "${cpack_config}" cpack_contents)
if(NOT cpack_contents MATCHES "set\\(CPACK_PACKAGE_VERSION \"1\\.27\\.0\\.0\"\\)")
  message(FATAL_ERROR "Expected Deskflow project version 1.27.0.0 in ${cpack_config}")
endif()

if(NOT cpack_contents MATCHES "set\\(CPACK_PACKAGE_FILE_NAME \"deskflow-1\\.27\\.0-monosync-(win-x64|macos-arm64)\"\\)")
  message(FATAL_ERROR "Expected package basename deskflow-1.27.0-monosync-<platform> in ${cpack_config}")
endif()

message(STATUS "Verified Deskflow version 1.27.0.0 and pinned package basename")
