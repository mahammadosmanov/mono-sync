if(NOT DEFINED BUILD_DIR)
  message(FATAL_ERROR "BUILD_DIR must point to the configured build directory")
endif()

set(cpack_config "${BUILD_DIR}/CPackConfig.cmake")
if(NOT EXISTS "${cpack_config}")
  message(FATAL_ERROR "Missing CPack configuration: ${cpack_config}")
endif()

file(READ "${cpack_config}" cpack_contents)
if(NOT cpack_contents MATCHES "set\\(CPACK_PACKAGE_VERSION \"0\\.2\\.1\\.0\"\\)")
  message(FATAL_ERROR "Expected Mono Sync product version 0.2.1.0 in ${cpack_config}")
endif()

if(NOT cpack_contents MATCHES "set\\(CPACK_PACKAGE_FILE_NAME \"mono-sync-0\\.2\\.1-(win-x64|macos-arm64)\"\\)")
  message(FATAL_ERROR "Expected package basename mono-sync-0.2.1-<platform> in ${cpack_config}")
endif()

message(STATUS "Verified Mono Sync version 0.2.1.0 and package basename")
