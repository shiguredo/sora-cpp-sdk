find_path(WebRTC_INCLUDE_DIR NAMES api/peer_connection_interface.h PATHS "${WEBRTC_INCLUDE_DIR}")
find_library(WebRTC_LIBRARY
  NAMES ${CMAKE_STATIC_LIBRARY_PREFIX}${WEBRTC_LIBRARY_NAME}${CMAKE_STATIC_LIBRARY_SUFFIX}
  PATHS "${WEBRTC_LIBRARY_DIR}" "${WEBRTC_LIBRARY_DIR}/${ANDROID_ABI}")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(WebRTC DEFAULT_MSG WebRTC_LIBRARY WebRTC_INCLUDE_DIR)

mark_as_advanced(WebRTC_INCLUDE_DIR WebRTC_LIBRARY)

if(WebRTC_FOUND)
  if(NOT TARGET WebRTC::webrtc)
    add_library(WebRTC::webrtc UNKNOWN IMPORTED)

    set(_DIRS
      ${WebRTC_INCLUDE_DIR}
      ${WebRTC_INCLUDE_DIR}/third_party/abseil-cpp
      ${WebRTC_INCLUDE_DIR}/third_party/boringssl/src/include
      ${WebRTC_INCLUDE_DIR}/third_party/libyuv/include
      ${WebRTC_INCLUDE_DIR}/third_party/zlib)

    if (APPLE)
      list(APPEND _DIRS
        ${WebRTC_INCLUDE_DIR}/sdk/objc
        ${WebRTC_INCLUDE_DIR}/sdk/objc/base)
    endif()

    set_target_properties(WebRTC::webrtc PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${_DIRS}"
      IMPORTED_LOCATION "${WebRTC_LIBRARY}")
  endif()
endif()